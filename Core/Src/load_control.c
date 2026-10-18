#include "load_control.h"
#include "load_config.h"
#include "sensors.h"
#include "dac.h"
#include <math.h>
#include <string.h>

static LoadSettings settings = {LOAD_MODE_CC, 0.0f, 5.0f};
static LoadRegulator regulator;
static volatile bool dac_ready;
static bool initialized;
static volatile bool running;
static volatile uint32_t faults;
static volatile uint32_t last_sample_ms;
static bool have_sample;
static bool pending;
static uint32_t last_start_ms;
static float voltage_v;
static float current_a;
static uint16_t dac_code;
static LoadSessionSettings session = {3600U, 40.0f, 60.0f};
static TemperatureState temperatures;
static volatile uint32_t session_start_ms;
static volatile uint32_t stopped_elapsed_ms;
static volatile LoadStopReason stop_reason;

static void EndSession(LoadStopReason reason)
{
    if (running)
        stopped_elapsed_ms = HAL_GetTick() - session_start_ms;
    running = false;
    stop_reason = reason;
    LoadControl_EmergencyZero();
}

static uint32_t Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

void LoadControl_EmergencyZero(void)
{
    if (dac_ready)
        DAC1->DHR12R1 = 0U; /* Trigger NONE transfers this to the enabled output. */
}

static void Trip(uint32_t reason)
{
    uint32_t mask = Lock();
    faults |= reason;
    EndSession(LOAD_STOP_FAULT);
    __set_PRIMASK(mask);
}

void LoadControl_ReportFault(uint32_t reason) { Trip(reason); }

void LoadControl_Tick1ms(void)
{
    if (!dac_ready)
        return;
    if (HAL_GPIO_ReadPin(FAULT_ACTIVE_GPIO_Port, FAULT_ACTIVE_Pin) == GPIO_PIN_SET)
        Trip(LOAD_FAULT_EXTERNAL);
    if (running && (uint32_t)(HAL_GetTick() - last_sample_ms) >= LOAD_STALE_MS)
        Trip(LOAD_FAULT_STALE);
    if (running && (uint32_t)(HAL_GetTick() - session_start_ms) >= session.max_duration_s * 1000U)
        EndSession(LOAD_STOP_TIMER);
}

HAL_StatusTypeDef LoadControl_Init(void)
{
    if (initialized || dac_ready)
        return HAL_ERROR;
    /* Preload zero before enabling the output; leave it enabled on all stops. */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0U) != HAL_OK ||
        HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK)
    {
        faults = LOAD_FAULT_DAC;
        return HAL_ERROR;
    }
    dac_ready = true;
    LoadControl_EmergencyZero();
    if (Sensors_Init() != HAL_OK)
    {
        Trip(LOAD_FAULT_SENSOR);
        return HAL_ERROR;
    }
    initialized = true;
    last_start_ms = HAL_GetTick() - LOAD_SAMPLE_PERIOD_MS;
    LoadRegulator_Reset(&regulator);
    return HAL_OK;
}

bool LoadControl_SetSettings(const LoadSettings *s)
{
    if (running || !LoadSettings_Valid(s))
        return false;
    settings = *s;
    LoadRegulator_Reset(&regulator);
    return true;
}

LoadSettings LoadControl_GetSettings(void) { return settings; }

bool LoadControl_SetSessionSettings(const LoadSessionSettings *s)
{
    if (s == NULL || running || s->max_duration_s < 1U || s->max_duration_s > 86400U ||
        !isfinite(s->external_limit_c) || s->external_limit_c < -40.0f || s->external_limit_c > 150.0f ||
        !isfinite(s->internal_limit_c) || s->internal_limit_c < -40.0f || s->internal_limit_c > 150.0f)
        return false;
    session = *s;
    /* Re-evaluate current temperatures when the user lowers a threshold. */
    if (have_sample)
    {
        uint16_t raw[TEMPERATURE_COUNT];
        memcpy(raw, temperatures.raw, sizeof raw);
        Temperature_Update(raw, session.external_limit_c, session.internal_limit_c, &temperatures);
    }
    return true;
}
LoadSessionSettings LoadControl_GetSessionSettings(void) { return session; }

bool LoadControl_IsReady(void)
{
    return initialized && !running && have_sample && temperatures.config_ok &&
        temperatures.valid_mask == temperatures.required_mask && temperatures.hot_mask == 0U &&
        (uint32_t)(HAL_GetTick() - last_sample_ms) < LOAD_STALE_MS &&
        HAL_GPIO_ReadPin(FAULT_ACTIVE_GPIO_Port, FAULT_ACTIVE_Pin) == GPIO_PIN_RESET;
}

bool LoadControl_Start(void)
{
    uint32_t mask = Lock();
    bool ok = LoadControl_IsReady();
    if (ok)
    {
        faults = 0U;
        LoadRegulator_Reset(&regulator);
        dac_code = 0U;
        LoadControl_EmergencyZero();
        session_start_ms = HAL_GetTick();
        stopped_elapsed_ms = 0U;
        stop_reason = LOAD_STOP_NONE;
        running = true;
    }
    __set_PRIMASK(mask);
    return ok;
}

void LoadControl_Stop(void)
{
    uint32_t mask = Lock();
    EndSession(LOAD_STOP_USER);
    __set_PRIMASK(mask);
    LoadRegulator_Reset(&regulator);
    dac_code = 0U;
    /* Fault reasons persist until an accepted explicit Start. */
}

static void AcceptSample(const SensorsSample *s)
{
    uint32_t elapsed = s->timestamp_ms - last_sample_ms;
    Temperature_Update(s->temperature_raw, session.external_limit_c,
                       session.internal_limit_c, &temperatures);
    if ((uint32_t)(HAL_GetTick() - s->timestamp_ms) >= LOAD_STALE_MS ||
        s->voltage_raw >= LOAD_ADC_SATURATION_RAW ||
        s->current_raw >= LOAD_ADC_SATURATION_RAW)
    {
        have_sample = false;
        Trip(LOAD_FAULT_SENSOR);
        return;
    }
    current_a = Sensors_RawToPinVoltage(s->current_raw, LOAD_ADC_VREF_V) /
        (LOAD_CURRENT_GAIN * LOAD_TOTAL_SHUNT_OHM);
    voltage_v = Sensors_RawToPinVoltage(s->voltage_raw, LOAD_ADC_VREF_V) *
        LOAD_VOLTAGE_DIVIDER + current_a * LOAD_TOTAL_SHUNT_OHM;
    if (!temperatures.config_ok || temperatures.valid_mask != temperatures.required_mask ||
        temperatures.hot_mask != 0U)
    {
        have_sample = false;
        Trip(!temperatures.config_ok ? LOAD_FAULT_TEMP_CONFIG :
             (temperatures.valid_mask != temperatures.required_mask ? LOAD_FAULT_PROBE :
              LOAD_FAULT_TEMPERATURE));
        return;
    }
    if (current_a > LOAD_OVERCURRENT_TRIP_A ||
        voltage_v * current_a > LOAD_OVERPOWER_TRIP_W)
    {
        have_sample = false;
        Trip(current_a > LOAD_OVERCURRENT_TRIP_A ?
             LOAD_FAULT_OVERCURRENT : LOAD_FAULT_OVERPOWER);
        return;
    }
    last_sample_ms = s->timestamp_ms;
    have_sample = true;
    if (!running)
        return;
    float demand = LoadRegulator_Step(&regulator, &settings, voltage_v,
                                      (float)elapsed / 1000.0f);
    uint16_t code = LoadRegulator_DacCode(demand);
    /* Prevent a foreground update from restoring output after an ISR trip. */
    uint32_t mask = Lock();
    if (HAL_GPIO_ReadPin(FAULT_ACTIVE_GPIO_Port, FAULT_ACTIVE_Pin) == GPIO_PIN_SET)
        Trip(LOAD_FAULT_EXTERNAL);
    if (running && faults == 0U)
    {
        if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code) != HAL_OK)
            Trip(LOAD_FAULT_DAC);
        else
            dac_code = code;
    }
    __set_PRIMASK(mask);
}

void LoadControl_Process(void)
{
    if (!initialized)
        return;
    LoadControl_Tick1ms();
    if (!running)
    {
        LoadRegulator_Reset(&regulator);
        dac_code = 0U;
    }
    if (pending)
    {
        SensorsSample sample;
        HAL_StatusTypeDef result = Sensors_ReadResult(&sample);
        if (result != HAL_BUSY)
        {
            pending = false;
            if (result == HAL_OK)
                AcceptSample(&sample);
            else
            {
                have_sample = false;
                Trip(LOAD_FAULT_SENSOR);
            }
        }
    }
    uint32_t now = HAL_GetTick();
    if (!pending && (uint32_t)(now - last_start_ms) >= LOAD_SAMPLE_PERIOD_MS)
    {
        last_start_ms = now;
        if (Sensors_StartRead() == HAL_OK)
            pending = true;
        else
        {
            have_sample = false;
            Trip(LOAD_FAULT_SENSOR);
        }
    }
}

LoadStatus LoadControl_GetStatus(void)
{
    uint32_t mask = Lock();
    uint32_t elapsed = running ? HAL_GetTick() - session_start_ms : stopped_elapsed_ms;
    uint32_t duration = session.max_duration_s * 1000U;
    LoadStatus s = {.running = running, .faults = faults,
        .dut_voltage_v = voltage_v, .measured_current_a = current_a,
        .command_current_a = running ? regulator.command_a : 0.0f,
        .dac_code = running ? dac_code : 0U, .elapsed_ms = elapsed,
        .remaining_ms = elapsed < duration ? duration - elapsed : 0U,
        .stop_reason = stop_reason, .temperature = temperatures};
    __set_PRIMASK(mask);
    return s;
}
