#include "sensors.h"
#include "adc.h"
#include <stdbool.h>
#include <stddef.h>

/* Matches the uploaded adc.c: normal DMA, halfword memory/peripheral width,
 * software trigger, single scan, 12-bit right-aligned ADC results.
 * Four-byte alignment accommodates the HAL API's uint32_t pointer.
 */
static volatile uint16_t adc1_raw[6] __attribute__((aligned(4)));
static volatile uint16_t adc2_raw[2] __attribute__((aligned(4)));
static volatile bool adc1_done;
static volatile bool adc2_done;
static volatile bool adc_failed;
static bool initialized;
static bool pending;
static uint32_t started_at;

static HAL_StatusTypeDef StopAcquisition(void)
{
    HAL_StatusTypeDef first = HAL_ADC_Stop_DMA(&hadc1);
    HAL_StatusTypeDef second = HAL_ADC_Stop_DMA(&hadc2);
    pending = false;
    if (first != HAL_OK || second != HAL_OK)
    {
        initialized = false;
        return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef Sensors_Init(void)
{
    if (pending)
    {
        return HAL_BUSY;
    }
    initialized = false;
    adc1_done = false;
    adc2_done = false;
    adc_failed = false;

    HAL_StatusTypeDef status =
        HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    if (status != HAL_OK)
    {
        return status;
    }
    status = HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    if (status != HAL_OK)
    {
        return status;
    }
    initialized = true;
    return HAL_OK;
}

HAL_StatusTypeDef Sensors_StartRead(void)
{
    if (!initialized)
    {
        return HAL_ERROR;
    }
    if (pending)
    {
        return HAL_BUSY;
    }

    adc1_done = false;
    adc2_done = false;
    adc_failed = false;
    started_at = HAL_GetTick();
    pending = true;

    HAL_StatusTypeDef status =
        HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_raw, 6U);
    if (status == HAL_OK)
    {
        status = HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_raw, 2U);
    }
    if (status != HAL_OK)
    {
        if (StopAcquisition() != HAL_OK)
        {
            return HAL_ERROR;
        }
    }
    return status;
}

HAL_StatusTypeDef Sensors_ReadResult(SensorsSample *sample)
{
    if (sample == NULL || !pending)
    {
        return HAL_ERROR;
    }
    if (adc_failed)
        {
            (void)StopAcquisition();
            return HAL_ERROR;
        }
        if (!(adc1_done && adc2_done))
        {
            if ((uint32_t)(HAL_GetTick() - started_at) >= SENSORS_READ_TIMEOUT_MS)
            {
                if (StopAcquisition() != HAL_OK)
                {
                    return HAL_ERROR;
                }
                return HAL_TIMEOUT;
            }
            return HAL_BUSY;
        }

        /* Stop both ADCs before exposing the completed sample. */
        if (StopAcquisition() != HAL_OK || adc_failed)
        {
            return HAL_ERROR;
        }
        sample->timestamp_ms = started_at;
        sample->voltage_raw = adc1_raw[0];       /* PA2, ADC1 rank 1 */
        sample->current_raw = adc1_raw[1];       /* PA3, ADC1 rank 2 */
        sample->temperature_raw[0] = adc2_raw[0]; /* PA6, ADC2 rank 1 */
        sample->temperature_raw[1] = adc2_raw[1]; /* PA7, ADC2 rank 2 */
        sample->temperature_raw[2] = adc1_raw[2]; /* PB0, ADC1 rank 3 */
        sample->temperature_raw[3] = adc1_raw[3]; /* PB1, ADC1 rank 4 */
        sample->temperature_raw[4] = adc1_raw[4]; /* PB11, ADC1 rank 5 */
        sample->temperature_raw[5] = adc1_raw[5]; /* PB12, ADC1 rank 6 */
        return HAL_OK;
    }

    float Sensors_RawToPinVoltage(uint16_t raw, float vref_volts)
    {
        return ((float)raw * vref_volts) / 4095.0f;
    }

    /* Define these HAL callbacks only once in the project.
     * If callbacks already exist, merge these branches into them.
     */
    void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
    {
        if (hadc == &hadc1)
        {
            adc1_done = true;
        }
        else if (hadc == &hadc2)
        {
            adc2_done = true;
        }
    }

    void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
    {
        if (hadc == &hadc1 || hadc == &hadc2)
        {
            adc_failed = true;
        }
    }

