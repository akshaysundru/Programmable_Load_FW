#include "temperature.h"
#include <math.h>
#include <stddef.h>

static bool ValidChannel(const TemperatureChannel *p)
{
    return (p->role == TEMP_EXTERNAL || p->role == TEMP_INTERNAL) &&
        isfinite(p->fixed_ohm) && p->fixed_ohm > 0.0f &&
        isfinite(p->ntc_25c_ohm) && p->ntc_25c_ohm > 0.0f &&
        isfinite(p->beta_k) && p->beta_k > 0.0f &&
        isfinite(p->min_valid_c) && p->min_valid_c > -273.15f &&
        isfinite(p->max_valid_c) && p->max_valid_c > p->min_valid_c &&
        isfinite(p->system_limit_c) && p->system_limit_c > p->min_valid_c &&
        p->system_limit_c <= p->max_valid_c;
}

bool Temperature_ConfigValid(void)
{
    unsigned external = 0U, internal = 0U;
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        const TemperatureChannel *p = &temperature_channels[i];
        if (p->role == TEMP_UNUSED) continue;
        if (!ValidChannel(p)) return false;
        if (p->role == TEMP_EXTERNAL) ++external;
        else ++internal;
    }
    return external >= 4U && internal >= 1U;
}

bool Temperature_Convert(const TemperatureChannel *p, uint16_t raw, float *c)
{
    if (c == NULL) return false;
    *c = NAN;
    /* Ratiometric VDDA-fed divider only. ADC rails indicate open/short.
     * A plausibly in-range failure cannot be diagnosed by this check alone. */
    if (p == NULL || !ValidChannel(p) || raw <= 8U || raw >= 4087U)
        return false;
    float ratio = (float)raw / (4095.0f - (float)raw);
    float resistance = p->fixed_ohm * (p->ntc_to_ground ? ratio : 1.0f / ratio);
    float inverse_k = 1.0f / 298.15f + logf(resistance / p->ntc_25c_ohm) / p->beta_k;
    if (!isfinite(inverse_k) || inverse_k <= 0.0f) return false;
    float result = 1.0f / inverse_k - 273.15f;
    if (!isfinite(result) || result < p->min_valid_c || result > p->max_valid_c)
        return false;
    *c = result;
    return true;
}

void Temperature_Update(const uint16_t raw[TEMPERATURE_COUNT], float external_limit,
                        float internal_limit, TemperatureState *s)
{
    s->config_ok = Temperature_ConfigValid();
    s->required_mask = s->valid_mask = s->hot_mask = 0U;
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        const TemperatureChannel *p = &temperature_channels[i];
        uint8_t bit = (uint8_t)(1U << i);
        s->raw[i] = raw[i];
        s->c[i] = NAN;
        if (p->role == TEMP_UNUSED) continue;
        s->required_mask |= bit;
        if (!Temperature_Convert(p, raw[i], &s->c[i])) continue;
        s->valid_mask |= bit;
        float user_limit = p->role == TEMP_EXTERNAL ? external_limit : internal_limit;
        if (!isfinite(user_limit)) {s->config_ok = false; continue;}
        if (s->c[i] >= fminf(user_limit, p->system_limit_c)) s->hot_mask |= bit;
    }
}
