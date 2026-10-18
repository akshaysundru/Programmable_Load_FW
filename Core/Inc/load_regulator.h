#ifndef LOAD_REGULATOR_H
#define LOAD_REGULATOR_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { LOAD_MODE_CC = 0, LOAD_MODE_CV } LoadMode;
typedef struct {
    LoadMode mode;
    float current_a; /* CC demand, or CV current ceiling. */
    float voltage_v; /* CV target at MAIN_IN, compensated for main shunt. */
} LoadSettings;
typedef struct {
    float integral_a;
    float command_a;
} LoadRegulator;

bool LoadSettings_Valid(const LoadSettings *settings);
void LoadRegulator_Reset(LoadRegulator *regulator);
float LoadRegulator_Step(LoadRegulator *regulator, const LoadSettings *settings,
                         float dut_voltage_v, float dt_s);
uint16_t LoadRegulator_DacCode(float current_a);
#endif
