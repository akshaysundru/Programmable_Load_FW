#ifndef LOAD_CONFIG_H
#define LOAD_CONFIG_H

/* Nominal calibration. Replace with measured values before full-load use. */
#define LOAD_ADC_VREF_V              3.3f
#define LOAD_DAC_VREF_V              3.3f
#define LOAD_VOLTAGE_DIVIDER         (43010.0f / 3010.0f)
#define LOAD_CURRENT_GAIN            50.0f
#define LOAD_TOTAL_SHUNT_OHM         0.002f
#define LOAD_BRANCH_SHUNT_OHM        0.1f
#define LOAD_BRANCH_COUNT            10.0f
#define LOAD_REF_V_PER_A             (LOAD_BRANCH_SHUNT_OHM / LOAD_BRANCH_COUNT)
#define LOAD_MAX_CURRENT_A           30.0f
#define LOAD_MAX_POWER_W             500.0f
/* Chosen UI range within the nominal ADC voltage span; not a hardware rating. */
#define LOAD_MAX_CV_V                45.0f
#define LOAD_SAMPLE_PERIOD_MS        10U
#define LOAD_STALE_MS                50U
#define LOAD_ADC_SATURATION_RAW      4090U
#define LOAD_OVERCURRENT_TRIP_A      30.5f
#define LOAD_OVERPOWER_TRIP_W        525.0f
/* Provisional CV gains: positive (Vdut - Vset) increases sink current. */
#define LOAD_CV_KP_A_PER_V           0.25f
#define LOAD_CV_KI_A_PER_V_S         0.5f
#define LOAD_RISE_A_PER_S            2.0f

#endif
