#ifndef SENSORS_H
#define SENSORS_H

#include "main.h"
#include <stdint.h>

#define SENSORS_TEMPERATURE_COUNT 6U
#define SENSORS_READ_TIMEOUT_MS 100U

typedef struct
{
    uint32_t timestamp_ms; /* HAL tick when acquisition was started. */
    uint16_t voltage_raw;
    uint16_t current_raw;
    uint16_t temperature_raw[SENSORS_TEMPERATURE_COUNT]; /* TEMP1 to TEMP6 */
} SensorsSample;

/* Call after MX_DMA_Init(), MX_ADC1_Init() and MX_ADC2_Init().
 * Calibrates both ADCs. This module exclusively owns ADC1 and ADC2.
 * All public functions must be called from the main loop, not an ISR.
 */
HAL_StatusTypeDef Sensors_Init(void);

/* Start one scan on each ADC. HAL_BUSY means a sample is still pending. */
HAL_StatusTypeDef Sensors_StartRead(void);

/* Call frequently after StartRead.
 * HAL_BUSY: still collecting; HAL_OK: complete sample copied and consumed.
 * HAL_TIMEOUT / HAL_ERROR: acquisition failed; output remains unchanged.
 * Calling without a pending acquisition returns HAL_ERROR.
 */
HAL_StatusTypeDef Sensors_ReadResult(SensorsSample *sample);

/* Converts a raw count to voltage AT THE ADC PIN using supplied VREF+.
 * Does not account for sensor gain, divider ratio or temperature response.
 */
float Sensors_RawToPinVoltage(uint16_t raw, float vref_volts);

#endif
