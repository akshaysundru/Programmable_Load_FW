/* STM32 HAL SPI2 disk layer for FatFs R0.15.
 * Main-loop-only, blocking, single-sector transfers. No RTOS or SPI DMA.
 * Requires mode 0, 8-bit, MSB-first SPI, PB14 pull-up, both CS outputs high.
 * FatFs calls these functions through diskio.h; no separate diskio.c is needed.
 */
#include "ff.h"
#include "diskio.h"
#include "spi.h"
#include "sd_spi.h"
#include <stdbool.h>
#include <stddef.h>

static DSTATUS card_status = STA_NOINIT;
static bool block_addressed;
static bool io_failed;
static void (*service_hook)(void);
static uint32_t serviced_ms;

void SD_SPI_SetServiceHook(void (*hook)(void)) { service_hook = hook; }

static void ServiceControl(void)
{
    uint32_t now = HAL_GetTick();
    if (service_hook != NULL && now != serviced_ms)
    {
        serviced_ms = now;
        service_hook();
    }
}

static uint8_t Exchange(uint8_t tx)
{
    ServiceControl();
    uint8_t rx = 0xFF;
    if (!io_failed &&
        HAL_SPI_TransmitReceive(&hspi2, &tx, &rx, 1U, 20U) != HAL_OK)
        io_failed = true;
    return rx;
}

static bool WaitReady(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    do {
        uint8_t value = Exchange(0xFF);
        if (io_failed) return false;
        if (value == 0xFF) return true;
    } while ((uint32_t)(HAL_GetTick() - start) < timeout);
    return false;
}

static void Deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    /* Extra clocks let the card release MISO before using the display. */
    (void)Exchange(0xFF);
}

static bool Select(void)
{
    /* Caller must ensure no asynchronous display transaction is active. */
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
    (void)Exchange(0xFF);
    return WaitReady(500U);
}

static uint8_t Command(uint8_t cmd, uint32_t arg)
{
    Deselect();
    if (!Select()) return 0xFF;
    (void)Exchange((uint8_t)(0x40U | cmd));
    (void)Exchange((uint8_t)(arg >> 24));
    (void)Exchange((uint8_t)(arg >> 16));
    (void)Exchange((uint8_t)(arg >> 8));
    (void)Exchange((uint8_t)arg);
    (void)Exchange(cmd == 0U ? 0x95U : (cmd == 8U ? 0x87U : 0x01U));
    for (unsigned i = 0; i < 10U; ++i) {
        uint8_t response = Exchange(0xFF);
        if (io_failed) return 0xFF;
        if ((response & 0x80U) == 0U) return response;
    }
    return 0xFF;
}

static uint8_t AppCommand41(uint32_t arg)
{
    uint8_t response = Command(55U, 0U);
    return response <= 1U ? Command(41U, arg) : response;
}

static bool ReceiveBlock(uint8_t *buffer)
{
    uint32_t start = HAL_GetTick();
    uint8_t token;
    do {
        token = Exchange(0xFF);
        if (io_failed) return false;
        if (token != 0xFF) break;
    } while ((uint32_t)(HAL_GetTick() - start) < 200U);
    if (token != 0xFE) return false;
    /* Transmit FF while receiving: do not send uninitialized buffer bytes. */
    for (unsigned i = 0; i < 512U; ++i) {
        buffer[i] = Exchange(0xFF);
        if (io_failed) return false;
    }
    (void)Exchange(0xFF); /* CRC disabled in SPI mode; consume both bytes. */
    (void)Exchange(0xFF);
    return !io_failed;
}

static bool SendBlock(const uint8_t *buffer)
{
    if (!WaitReady(500U)) return false;
    (void)Exchange(0xFE);
    for (unsigned i = 0; i < 512U; ++i) {
        (void)Exchange(buffer[i]);
        if (io_failed) return false;
    }
    (void)Exchange(0xFF);
    (void)Exchange(0xFF);
    uint8_t response = Exchange(0xFF);
    if (io_failed || (response & 0x1FU) != 0x05U) return false;
    return WaitReady(1000U);
}

DSTATUS disk_status(BYTE drive)
{
    /* No card-detect switch: removal is detected on the next I/O failure. */
    return drive == 0U ? card_status : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive != 0U) return STA_NOINIT;
    card_status = STA_NOINIT;
    block_addressed = false;
    io_failed = false;
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

    /* Compute a startup divider for 100-400 kHz from the actual APB1 clock.
     * At your 16 MHz PCLK1 this selects /64 = 250 kHz.
     * Keep this speed for the initial test; do not change it during a transfer.
     */
    const uint32_t dividers[] = {2,4,8,16,32,64,128,256};
    const uint32_t presets[] = {
        SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4,
        SPI_BAUDRATEPRESCALER_8, SPI_BAUDRATEPRESCALER_16,
        SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
        SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
    };
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    unsigned i = 0;
    while (i < 8U && clock / dividers[i] > 400000U) ++i;
    if (i == 8U || clock / dividers[i] < 100000U) return card_status;
    if (HAL_SPI_GetState(&hspi2) != HAL_SPI_STATE_READY) return card_status;
    hspi2.Init.BaudRatePrescaler = presets[i];
    if (HAL_SPI_Init(&hspi2) != HAL_OK) return card_status;
    HAL_Delay(10U);
    for (i = 0; i < 10U; ++i) (void)Exchange(0xFF); /* 80 clocks, CS high */

    bool ok = Command(0U, 0U) == 1U;
    uint32_t hcs = 0;
    if (ok) {
        uint8_t r = Command(8U, 0x1AAU);
        if (r == 1U) {
            uint8_t r7[4];
            for (i = 0; i < 4U; ++i) r7[i] = Exchange(0xFF);
            ok = r7[2] == 0x01U && r7[3] == 0xAAU;
            hcs = 0x40000000U;
        } else {
            ok = r == 0x05U; /* SD version 1: CMD8 is illegal. */
        }
    }
    if (ok) {
        uint32_t start = HAL_GetTick();
        uint8_t r;
        do {
            r = AppCommand41(hcs);
            if (r != 1U || io_failed) break;
            HAL_Delay(1U);
        } while ((uint32_t)(HAL_GetTick() - start) < 2000U);
        ok = r == 0U;
    }
    if (ok) {
        ok = Command(58U, 0U) == 0U;
        if (ok) {
            uint32_t ocr = 0;
            for (i = 0; i < 4U; ++i) ocr = (ocr << 8) | Exchange(0xFF);
            ok = (ocr & 0x80000000U) != 0U &&
                 (ocr & 0x00300000U) != 0U; /* Ready, supports 3.2-3.4 V */
            block_addressed = hcs != 0U && (ocr & 0x40000000U) != 0U;
        }
    }
    if (ok && !block_addressed) ok = Command(16U, 512U) == 0U;
    Deselect();
    if (ok && !io_failed) card_status = 0;
    return card_status;
}

static DRESULT Finish(bool ok)
{
    Deselect();
    if (!ok || io_failed) {
        card_status = STA_NOINIT;
        return RES_ERROR;
    }
    return RES_OK;
}

static bool ValidRange(LBA_t sector, UINT count)
{
    uint32_t max_sector = block_addressed ? UINT32_MAX : UINT32_MAX / 512U;
    return count != 0U && sector <= max_sector &&
           (uint32_t)(count - 1U) <= max_sector - sector;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0U || buffer == NULL || count == 0U) return RES_PARERR;
    if (card_status & STA_NOINIT) return RES_NOTRDY;
    if (!ValidRange(sector, count)) return RES_PARERR;
    io_failed = false;
    for (UINT i = 0; i < count; ++i, ++sector, buffer += 512U) {
        uint32_t address = block_addressed ? sector : sector * 512U;
        if (Command(17U, address) != 0U || !ReceiveBlock(buffer))
            return Finish(false);
    }
    return Finish(true);
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0U || buffer == NULL || count == 0U) return RES_PARERR;
    if (card_status & STA_NOINIT) return RES_NOTRDY;
    if (!ValidRange(sector, count)) return RES_PARERR;
    io_failed = false;
    for (UINT i = 0; i < count; ++i, ++sector, buffer += 512U) {
        uint32_t address = block_addressed ? sector : sector * 512U;
        if (Command(24U, address) != 0U || !SendBlock(buffer))
            return Finish(false);
        /* CMD13's two-byte status catches card-side programming errors. */
        uint8_t r1 = Command(13U, 0U);
        uint8_t r2 = Exchange(0xFF);
        if (r1 != 0U || r2 != 0U) return Finish(false);
    }
    return Finish(true);
}

DRESULT disk_ioctl(BYTE drive, BYTE cmd, void *buffer)
{
    if (drive != 0U) return RES_PARERR;
    if (card_status & STA_NOINIT) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
        io_failed = false;
        return Finish(Select());
    }
    if (cmd == GET_SECTOR_SIZE && buffer != NULL) {
        *(WORD *)buffer = 512U;
        return RES_OK;
    }
    /* Sector count / erase size / trim not needed with supplied ffconf.h.
     * Formatting on the MCU is intentionally disabled.
     */
    return RES_PARERR;
}
