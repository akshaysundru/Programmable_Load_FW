#ifndef TEMPERATURE_H
#define TEMPERATURE_H
#include <stdbool.h>
#include <stdint.h>
#define TEMPERATURE_COUNT 6U

typedef enum {TEMP_UNASSIGNED, TEMP_UNUSED, TEMP_EXTERNAL, TEMP_INTERNAL} TemperatureRole;
typedef struct {
    TemperatureRole role;
    bool ntc_to_ground; /* true: fixed resistor to VDDA, NTC to GND. */
    float fixed_ohm;
    float ntc_25c_ohm;
    float beta_k;       /* Beta-model reference temperature is 25 C. */
    float min_valid_c;
    float max_valid_c;
    float system_limit_c;
} TemperatureChannel;
typedef struct {
    float c[TEMPERATURE_COUNT];
    uint16_t raw[TEMPERATURE_COUNT];
    uint8_t required_mask;
    uint8_t valid_mask;
    uint8_t hot_mask;
    bool config_ok;
} TemperatureState;
/* Define actual fitted parts and channel roles in temperature_board.c. */
extern const TemperatureChannel temperature_channels[TEMPERATURE_COUNT];
bool Temperature_ConfigValid(void);
bool Temperature_Convert(const TemperatureChannel *channel, uint16_t raw, float *c);
void Temperature_Update(const uint16_t raw[TEMPERATURE_COUNT], float external_limit_c,
                        float internal_limit_c, TemperatureState *state);
#endif
