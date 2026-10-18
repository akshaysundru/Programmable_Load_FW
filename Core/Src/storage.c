#include "storage.h"
#include <stdio.h>
#include <string.h>

static FATFS filesystem; /* Must outlive f_mount(). */

FRESULT Storage_Mount(void)
{
    return f_mount(&filesystem, "0:", 1);
}

FRESULT Storage_Unmount(void)
{
    return f_mount(NULL, "0:", 0);
}

FRESULT Storage_TestWriteRead(void)
{
    static const char text[] = "STM32 SPI SD write/read test passed.\r\n";
    FIL file;
    char filename[16];
    FRESULT result = FR_EXIST;
    for (unsigned i = 0; i < 1000U; ++i) {
        (void)snprintf(filename, sizeof filename, "0:/SDT%03u.TXT", i);
        result = f_open(&file, filename, FA_WRITE | FA_CREATE_NEW);
        if (result != FR_EXIST) break;
    }
    if (result != FR_OK) return result;
    UINT written = 0;
    result = f_write(&file, text, sizeof text - 1U, &written);
    if (result == FR_OK && written != sizeof text - 1U) result = FR_DISK_ERR;
    FRESULT close_result = f_close(&file);
    if (result != FR_OK) return result;
    if (close_result != FR_OK) return close_result;

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK) return result;
    char actual[sizeof text - 1U];
    UINT received = 0;
    result = f_read(&file, actual, sizeof actual, &received);
    close_result = f_close(&file);
    if (result != FR_OK) return result;
    if (close_result != FR_OK) return close_result;
    if (received != sizeof actual || memcmp(actual, text, sizeof actual) != 0)
        return FR_DISK_ERR;
    return FR_OK;
}
