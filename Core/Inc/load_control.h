#ifndef LOAD_CONTROL_H
#define LOAD_CONTROL_H
#include "main.h"
#include "load_regulator.h"
#include "temperature.h"

enum {
    LOAD_FAULT_EXTERNAL = 1U << 0,
    LOAD_FAULT_SENSOR = 1U << 1,
    LOAD_FAULT_STALE = 1U << 2,
    LOAD_FAULT_OVERCURRENT = 1U << 3,
    LOAD_FAULT_OVERPOWER = 1U << 4,
    LOAD_FAULT_DAC = 1U << 5,
    LOAD_FAULT_TEMPERATURE = 1U << 6,
    LOAD_FAULT_PROBE = 1U << 7,
    LOAD_FAULT_LOGGING = 1U << 8,
    LOAD_FAULT_TEMP_CONFIG = 1U << 9
};
typedef enum {LOAD_STOP_NONE, LOAD_STOP_USER, LOAD_STOP_TIMER, LOAD_STOP_FAULT} LoadStopReason;
typedef struct {
    uint32_t max_duration_s; /* 1..86400 seconds; no implicit unlimited mode. */
    float external_limit_c;
    float internal_limit_c;
} LoadSessionSettings;
typedef struct {
    bool running;
    uint32_t faults;
    float dut_voltage_v;
    float measured_current_a;
    float command_current_a; /* Before DAC quantization. */
    uint16_t dac_code;
    uint32_t elapsed_ms;
    uint32_t remaining_ms;
    LoadStopReason stop_reason;
    TemperatureState temperature;
} LoadStatus;

/* Main-context API; init after GPIO/DMA/ADCs/DAC init. Boots stopped at zero.
 * Owns Sensors acquisition and DAC1 channel 1. Settings editable only stopped. */
HAL_StatusTypeDef LoadControl_Init(void);
bool LoadControl_SetSettings(const LoadSettings *settings);
LoadSettings LoadControl_GetSettings(void);
bool LoadControl_SetSessionSettings(const LoadSessionSettings *settings);
LoadSessionSettings LoadControl_GetSessionSettings(void);
bool LoadControl_IsReady(void);
/* For application storage failures; does not drive protection hardware. */
void LoadControl_ReportFault(uint32_t reason);
/* Explicit restart acknowledges software faults only with LOW PA10 and fresh
 * valid sensing. Never drives PA5 or resets any external protection. */
bool LoadControl_Start(void);
void LoadControl_Stop(void);
void LoadControl_Process(void); /* Call continuously; no blocking work. */
LoadStatus LoadControl_GetStatus(void);
/* Call once after HAL_IncTick in SysTick. Polls fault and sample watchdog.
 * Uses only integer operations and a direct DAC zero write. */
void LoadControl_Tick1ms(void);
/* Fatal-handler safe. Commands zero only after DAC was initialized. */
void LoadControl_EmergencyZero(void);
#endif
