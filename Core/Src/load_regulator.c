#include "load_regulator.h"
#include "load_config.h"
#include <math.h>
#include <stddef.h>

static float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

bool LoadSettings_Valid(const LoadSettings *s)
{
    return s != NULL && (s->mode == LOAD_MODE_CC || s->mode == LOAD_MODE_CV) &&
        isfinite(s->current_a) && s->current_a >= 0.0f &&
        s->current_a <= LOAD_MAX_CURRENT_A && isfinite(s->voltage_v) &&
        s->voltage_v >= 0.1f && s->voltage_v <= LOAD_MAX_CV_V;
}

void LoadRegulator_Reset(LoadRegulator *r)
{
    r->integral_a = 0.0f;
    r->command_a = 0.0f;
}

float LoadRegulator_Step(LoadRegulator *r, const LoadSettings *s,
                         float voltage, float dt)
{
    if (!LoadSettings_Valid(s) || !isfinite(voltage) || voltage < 0.0f ||
        !isfinite(dt) || dt <= 0.0f || dt > (float)LOAD_STALE_MS / 1000.0f)
    {
        LoadRegulator_Reset(r);
        return 0.0f;
    }
    float limit = s->current_a;
    if (voltage > 0.0f)
        limit = fminf(limit, LOAD_MAX_POWER_W / voltage);
    float demand = limit;
    if (s->mode == LOAD_MODE_CV)
    {
        float error = voltage - s->voltage_v;
        float proportional = LOAD_CV_KP_A_PER_V * error;
        /* Clamp integral against the actual actuator ceiling, including ramp.
         * This prevents windup while current or power limited and during startup. */
        float ceiling = fminf(limit, r->command_a + LOAD_RISE_A_PER_S * dt);
        r->integral_a = Clamp(r->integral_a + LOAD_CV_KI_A_PER_V_S * error * dt,
                              0.0f, ceiling);
        demand = Clamp(proportional + r->integral_a, 0.0f, ceiling);
        if (proportional + r->integral_a > ceiling && error > 0.0f)
            r->integral_a = fmaxf(0.0f, ceiling - proportional);
        /* A disconnected/low-voltage DUT must not retain a positive demand. */
        if (voltage < s->voltage_v - 0.1f)
        {
            demand = 0.0f;
            r->integral_a = 0.0f;
        }
    }
    /* Rising commands are ramped; reductions and new ceilings apply immediately. */
    r->command_a = fminf(demand, r->command_a + LOAD_RISE_A_PER_S * dt);
    r->command_a = Clamp(r->command_a, 0.0f, limit);
    return r->command_a;
}

uint16_t LoadRegulator_DacCode(float current)
{
    if (!isfinite(current) || current <= 0.0f)
        return 0U;
    current = fminf(current, LOAD_MAX_CURRENT_A);
    /* Floor, rather than round, so quantization cannot exceed the command cap.
     * 12-bit DAC uses 4096 steps; no dithering is applied. */
    return (uint16_t)(current * LOAD_REF_V_PER_A * 4096.0f / LOAD_DAC_VREF_V);
}
