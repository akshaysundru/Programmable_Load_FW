#include "discharge_session.h"
#include "load_control.h"
#include "storage.h"
#include "sd_spi.h"
#include "buttons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 2 Hz nominal leaves scheduling margin for the >=1 Hz requirement.
 * Stop if writes prevent this cadence; never pretend skipped data were sampled. */
#define LOG_PERIOD_MS 500U
#define LOG_SYNC_MS 2000U
static FIL logfile;
static bool opened;
static bool preparing;
static bool cancelled;
static DischargeStatus status;
static unsigned next_index;
static uint32_t next_record_ms;
static uint32_t synced_ms;

static void ServiceDuringSD(void)
{
    LoadControl_Process();
    Buttons_Update();
    if (Buttons_WasPressed(BUTTON_2)) {cancelled = true;LoadControl_Stop();}
}

void Discharge_Init(void)
{
    SD_SPI_SetServiceHook(ServiceDuringSD);
}

DischargeStatus Discharge_GetStatus(void) { return status; }

static void RecordError(FRESULT r)
{
    if (r != FR_OK && status.sd_error == FR_OK) status.sd_error = r;
}

static bool WriteText(const char *text)
{
    UINT written = 0U;
    UINT count = (UINT)strlen(text);
    FRESULT result = f_write(&logfile, text, count, &written);
    if (result == FR_OK && written != count) result = FR_DISK_ERR;
    RecordError(result);
    return result == FR_OK;
}

static bool WriteRecord(const char *event)
{
    LoadStatus t = LoadControl_GetStatus();
    LoadSettings s = LoadControl_GetSettings();
    LoadSessionSettings limits = LoadControl_GetSessionSettings();
    char line[512];
    int n = snprintf(line, sizeof line,
        "%s,%lu,%lu,%s,%lu,%lu,%lu,%ld,%ld,%lu,%lu,%lu,%u",
        event, (unsigned long)HAL_GetTick(), (unsigned long)t.elapsed_ms,
        s.mode == LOAD_MODE_CC ? "CC" : "CV",
        (unsigned long)(s.current_a * 1000.0f + 0.5f),
        (unsigned long)(s.voltage_v * 1000.0f + 0.5f),
        (unsigned long)limits.max_duration_s,
        (long)(limits.external_limit_c * 100.0f), (long)(limits.internal_limit_c * 100.0f),
        (unsigned long)(t.dut_voltage_v * 1000.0f),
        (unsigned long)(t.measured_current_a * 1000.0f),
        (unsigned long)(t.dut_voltage_v * t.measured_current_a * 1000.0f),
        (unsigned)t.dac_code);
    if (n < 0 || (size_t)n >= sizeof line) {RecordError(FR_INT_ERR);return false;}
    size_t used = (size_t)n;
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        if ((t.temperature.valid_mask & (1U << i)) && isfinite(t.temperature.c[i]))
            n = snprintf(line + used, sizeof line - used, ",%ld", (long)(t.temperature.c[i] * 100.0f));
        else
            n = snprintf(line + used, sizeof line - used, ",");
        if (n < 0 || (size_t)n >= sizeof line - used) {RecordError(FR_INT_ERR);return false;}
        used += (size_t)n;
    }
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        n = snprintf(line + used, sizeof line - used, ",%u", (unsigned)t.temperature.raw[i]);
        if (n < 0 || (size_t)n >= sizeof line - used) {RecordError(FR_INT_ERR);return false;}
        used += (size_t)n;
    }
    n = snprintf(line + used, sizeof line - used, ",%u,%u,%lu,%u\r\n",
        (unsigned)t.temperature.valid_mask, (unsigned)t.temperature.hot_mask,
        (unsigned long)t.faults, (unsigned)t.stop_reason);
    if (n < 0 || (size_t)n >= sizeof line - used) {RecordError(FR_INT_ERR);return false;}
    return WriteText(line);
}

static void CloseLog(void)
{
    if (opened)
    {
        RecordError(f_sync(&logfile));
        RecordError(f_close(&logfile));
        opened = false;
    }
    RecordError(Storage_Unmount());
    status.logging = false;
    if (status.sd_error != FR_OK || status.rate_error)
        LoadControl_ReportFault(LOAD_FAULT_LOGGING);
}

static bool BeginSession(void)
{
    if (opened || LoadControl_GetStatus().running || !LoadControl_IsReady()) return false;
    status = (DischargeStatus){0};
    RecordError(Storage_Mount());
    if (status.sd_error != FR_OK) {CloseLog();return false;}
    FRESULT result = FR_EXIST;
    for (; next_index < 10000U; ++next_index)
    {
        (void)snprintf(status.filename, sizeof status.filename, "0:/LOAD%04u.CSV", next_index);
        result = f_open(&logfile, status.filename, FA_WRITE | FA_CREATE_NEW);
        if (result != FR_EXIST) {++next_index;break;}
    }
    RecordError(result);
    if (result != FR_OK) {CloseLog();return false;}
    opened = true;
    if (!WriteText("event,uptime_ms,elapsed_ms,mode,set_mA,set_mV,max_s,external_limit_centiC,internal_limit_centiC,dut_mV,current_mA,power_mW,dac_code,t1_centiC,t2_centiC,t3_centiC,t4_centiC,t5_centiC,t6_centiC,t1_raw,t2_raw,t3_raw,t4_raw,t5_raw,t6_raw,valid_mask,hot_mask,faults,stop_reason\r\n"))
    {CloseLog();return false;}
    RecordError(f_sync(&logfile)); /* Commit the header before allowing current. */
    if (status.sd_error != FR_OK || cancelled || !LoadControl_Start())
    {
        (void)WriteRecord("REJECTED");
        CloseLog();return false;
    }
    status.logging = true;
    uint32_t now = HAL_GetTick();
    next_record_ms = now + LOG_PERIOD_MS;
    synced_ms = now;
    if (!WriteRecord("START")) LoadControl_ReportFault(LOAD_FAULT_LOGGING);
    Discharge_Process(); /* Handles fault/expiry that occurred during SD service. */
    return LoadControl_GetStatus().running;
}

bool Discharge_Start(void)
{
    if (preparing) return false;
    preparing = true;
    cancelled = false;
    bool ok = BeginSession();
    preparing = false;
    return ok;
}

void Discharge_Stop(void) { cancelled = true;LoadControl_Stop(); }

void Discharge_Process(void)
{
    if (!opened) return;
    uint32_t now = HAL_GetTick();
    if (LoadControl_GetStatus().running && (int32_t)(now - next_record_ms) >= 0)
    {
        if ((uint32_t)(now - next_record_ms) >= LOG_PERIOD_MS)
        {
            status.rate_error = true;
            LoadControl_ReportFault(LOAD_FAULT_LOGGING);
        }
        else
        {
            next_record_ms = now + LOG_PERIOD_MS;
            if (!WriteRecord("SAMPLE")) LoadControl_ReportFault(LOAD_FAULT_LOGGING);
            if (LoadControl_GetStatus().running &&
                (uint32_t)(HAL_GetTick() - now) >= LOG_PERIOD_MS)
            {
                status.rate_error = true;
                LoadControl_ReportFault(LOAD_FAULT_LOGGING);
            }
        }
    }
    if (LoadControl_GetStatus().running &&
        (uint32_t)(HAL_GetTick() - synced_ms) >= LOG_SYNC_MS)
    {
        uint32_t start = HAL_GetTick();
        RecordError(f_sync(&logfile));
        synced_ms = HAL_GetTick();
        if ((uint32_t)(synced_ms - start) >= LOG_PERIOD_MS) status.rate_error = true;
        if (status.sd_error != FR_OK || status.rate_error)
            LoadControl_ReportFault(LOAD_FAULT_LOGGING);
    }
    if (!LoadControl_GetStatus().running)
    {
        (void)WriteRecord("STOP");
        CloseLog();
    }
}
