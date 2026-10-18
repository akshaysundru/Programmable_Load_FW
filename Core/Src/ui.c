#include "ui.h"
#include "buttons.h"
#include "encoder.h"
#include "st7789.h"
#include "load_config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define UI_ROWS 14U
#define UI_COLS 20U
#define UI_REFRESH_MS 200U
#define UI_CV_MAX_MV 30000
#define UI_CV_MIN_MV 100 /* Existing CV regulator's supported minimum. */
#define UI_TEMP_MIN_C (-40)
#define UI_TEMP_MAX_C 150

static UI_State_t ui;
static Encoder_t setpoint_encoder;
static Encoder_t timer_encoder;
static char wanted[UI_ROWS][UI_COLS + 1U];
static char painted[UI_ROWS][UI_COLS];
static uint16_t wanted_fg[UI_ROWS];
static uint16_t wanted_bg[UI_ROWS];
static uint16_t painted_fg[UI_ROWS][UI_COLS];
static uint16_t painted_bg[UI_ROWS][UI_COLS];
static unsigned cursor;
static uint32_t last_refresh;
static bool redraw;

static int32_t Clamp(int64_t value, int32_t low, int32_t high)
{
    return value < low ? low : (value > high ? high : (int32_t)value);
}

static void Snapshot(void)
{
    bool was_running = ui.load.running;
    UI_View_t previous_view = ui.view;
    ui.settings = LoadControl_GetSettings();
    ui.session = LoadControl_GetSessionSettings();
    ui.load = LoadControl_GetStatus();
    ui.discharge = Discharge_GetStatus();
    if (ui.load.running)
    {
        ui.view = UI_VIEW_RUNNING;
        ui.last_run = ui.load;
        ui.last_run_available = true;
    }
    else if (was_running && ui.last_run_available)
    {
        /* Keep the last observed running V/I/temperatures, with authoritative
         * stop time/reason. Later idle measurements must not erase the result. */
        ui.last_run.running = false;
        ui.last_run.elapsed_ms = ui.load.elapsed_ms;
        ui.last_run.remaining_ms = ui.load.remaining_ms;
        ui.last_run.stop_reason = ui.load.stop_reason;
        ui.last_run.faults = ui.load.faults;
        ui.view = UI_VIEW_SUMMARY;
    }
    if (previous_view != ui.view) redraw = true;
}

static bool Editable(void)
{
    return !ui.load.running && !ui.discharge.logging;
}

static void AdjustParameters(int32_t set_delta, int32_t time_delta)
{
    if (set_delta == 0 && time_delta == 0) return;
    if (!Editable()) {ui.edit_blocked = true;return;}
    LoadSettings s = ui.settings;
    LoadSessionSettings limits = ui.session;
    int32_t current_ma = (int32_t)(s.current_a * 1000.0f + 0.5f);
    int32_t voltage_mv = (int32_t)(s.voltage_v * 1000.0f + 0.5f);
    int32_t seconds_step = ui.fine_step ? 1 : 60;
    switch (ui.selected_field)
    {
    case UI_FIELD_CURRENT:
        current_ma = Clamp((int64_t)current_ma + (int64_t)set_delta * (ui.fine_step ? 10 : 100),
                           0, (int32_t)(LOAD_MAX_CURRENT_A * 1000.0f));
        break;
    case UI_FIELD_VOLTAGE:
        voltage_mv = Clamp((int64_t)voltage_mv + (int64_t)set_delta * (ui.fine_step ? 100 : 1000),
                           UI_CV_MIN_MV, (int32_t)fminf(UI_CV_MAX_MV, LOAD_MAX_CV_V * 1000.0f));
        break;
    case UI_FIELD_DURATION:
        /* Encoder 2 always adjusts time; combine deltas if this field is selected. */
        limits.max_duration_s = (uint32_t)Clamp((int64_t)limits.max_duration_s +
                                  (int64_t)set_delta * seconds_step, 1, 86400);
        break;
    case UI_FIELD_EXTERNAL_TEMP:
        limits.external_limit_c = (float)Clamp((int64_t)limits.external_limit_c + set_delta,
                                              UI_TEMP_MIN_C, UI_TEMP_MAX_C);
        break;
    case UI_FIELD_INTERNAL_TEMP:
        limits.internal_limit_c = (float)Clamp((int64_t)limits.internal_limit_c + set_delta,
                                              UI_TEMP_MIN_C, UI_TEMP_MAX_C);
        break;
    default: break;
    }
    limits.max_duration_s = (uint32_t)Clamp((int64_t)limits.max_duration_s +
                              (int64_t)time_delta * seconds_step, 1, 86400);
    s.current_a = (float)current_ma / 1000.0f;
    s.voltage_v = (float)voltage_mv / 1000.0f;
    (void)LoadControl_SetSettings(&s);
    (void)LoadControl_SetSessionSettings(&limits);
    ui.start_rejected = false;
    ui.edit_blocked = false;
    redraw = true;
}

static const char *StatusMessage(void)
{
    if (!ui.load.temperature.config_ok) return "NTC CONFIG REQUIRED";
    if (ui.load.temperature.valid_mask != ui.load.temperature.required_mask) return "CHECK TEMP PROBES";
    if (ui.load.temperature.hot_mask != 0U) return "TEMPERATURE TOO HIGH";
    if (ui.discharge.sd_error != FR_OK) return "SD CARD ERROR";
    if (ui.discharge.rate_error) return "SD CARD TOO SLOW";
    if (ui.load.faults & LOAD_FAULT_EXTERNAL) return "EXTERNAL FAULT";
    if (ui.load.faults & LOAD_FAULT_STALE) return "SENSOR DATA STALE";
    if (ui.load.faults & LOAD_FAULT_SENSOR) return "SENSING ERROR";
    if (ui.load.faults & LOAD_FAULT_OVERCURRENT) return "OVER CURRENT";
    if (ui.load.faults & LOAD_FAULT_OVERPOWER) return "OVER POWER";
    if (ui.load.faults & LOAD_FAULT_DAC) return "DAC ERROR";
    if (ui.load.faults & LOAD_FAULT_LOGGING) return "LOGGING ERROR";
    if (ui.edit_blocked && ui.load.running) return "STOP TO EDIT";
    if (ui.start_rejected) return "START NOT READY";
    if (ui.load.running) return "SD LOGGING ACTIVE";
    if (ui.discharge.logging) return "SAVING LOG...";
    if (ui.load.stop_reason == LOAD_STOP_TIMER) return "TIME COMPLETE";
    if (ui.load.faults != 0U) return "CLEAR FAULT / START";
    return "READY - PRESS START";
}

static uint16_t DisplayMilli(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0U;
    return (uint16_t)fminf(value * 1000.0f + 0.5f, 65535.0f);
}

static void FormatTemperature(char *buffer, size_t size, const LoadStatus *sample, unsigned index)
{
    if (index >= TEMPERATURE_COUNT || !(sample->temperature.valid_mask & (1U << index)) ||
        !isfinite(sample->temperature.c[index]))
    {
        (void)snprintf(buffer, size, "--");
        return;
    }
    long tenth = lroundf(fmaxf(-999.0f, fminf(999.0f, sample->temperature.c[index])) * 10.0f);
    unsigned magnitude = (unsigned)(tenth < 0 ? -tenth : tenth);
    (void)snprintf(buffer, size, "%s%u.%u", tenth < 0 ? "-" : "", magnitude / 10U, magnitude % 10U);
}

static void BuildMeasurements(void)
{
    bool running = ui.view == UI_VIEW_RUNNING;
    const LoadStatus *sample = running ? &ui.load : &ui.last_run;
    const char *title = running ? "DISCHARGE RUNNING" :
        (sample->stop_reason == LOAD_STOP_TIMER ? "DISCHARGE COMPLETE" :
         (sample->stop_reason == LOAD_STOP_FAULT ? "DISCHARGE FAULT" : "DISCHARGE STOPPED"));
    (void)snprintf(wanted[0], sizeof wanted[0], "%s", title);
    wanted_fg[0] = running ? GREEN : (sample->faults ? RED : YELLOW);
    (void)snprintf(wanted[1], sizeof wanted[1], "%s %s",
        ui.settings.mode == LOAD_MODE_CC ? "CC" : "CV", running ? "LIVE MEASUREMENTS" : "LAST RUN READINGS");
    uint16_t mv = DisplayMilli(sample->dut_voltage_v);
    uint16_t ma = DisplayMilli(sample->measured_current_a);
    (void)snprintf(wanted[2], sizeof wanted[2], "Voltage: %2u.%02u V",
        (unsigned)mv / 1000U, ((unsigned)mv % 1000U) / 10U);
    (void)snprintf(wanted[3], sizeof wanted[3], "Current: %2u.%02u A",
        (unsigned)ma / 1000U, ((unsigned)ma % 1000U) / 10U);
    float power = sample->dut_voltage_v * sample->measured_current_a;
    uint16_t deciwatts = isfinite(power) && power > 0.0f ? (uint16_t)fminf(power * 10.0f + 0.5f, 65535.0f) : 0U;
    (void)snprintf(wanted[4], sizeof wanted[4], "Power: %4u.%u W", (unsigned)deciwatts / 10U, (unsigned)deciwatts % 10U);
    unsigned long elapsed = (unsigned long)(sample->elapsed_ms / 1000U);
    unsigned long left = ((unsigned long)sample->remaining_ms + 999UL) / 1000UL;
    (void)snprintf(wanted[5], sizeof wanted[5], "Elapsed %02lu:%02lu:%02lu",
        elapsed / 3600UL, (elapsed / 60UL) % 60UL, elapsed % 60UL);
    (void)snprintf(wanted[6], sizeof wanted[6], "Left    %02lu:%02lu:%02lu",
        left / 3600UL, (left / 60UL) % 60UL, left % 60UL);
    unsigned external = 0U;
    unsigned hottest_internal = TEMPERATURE_COUNT;
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        if (temperature_channels[i].role == TEMP_EXTERNAL && external < 4U)
        {
            char temperature[12];FormatTemperature(temperature, sizeof temperature, sample, i);
            (void)snprintf(wanted[7U + external], sizeof wanted[0], "Battery %u: %.7s C", external + 1U, temperature);
            ++external;
        }
        if (temperature_channels[i].role == TEMP_INTERNAL &&
            (sample->temperature.valid_mask & (1U << i)) && isfinite(sample->temperature.c[i]) &&
            (hottest_internal == TEMPERATURE_COUNT || sample->temperature.c[i] > sample->temperature.c[hottest_internal]))
            hottest_internal = i;
    }
    while (external < 4U)
    {
        (void)snprintf(wanted[7U + external], sizeof wanted[0], "Battery %u: -- C", external + 1U);
        ++external;
    }
    char temperature[12];FormatTemperature(temperature, sizeof temperature, sample, hottest_internal);
    (void)snprintf(wanted[11], sizeof wanted[11], "Internal: %.7s C", temperature);
    if (running)
    {
        (void)snprintf(wanted[12], sizeof wanted[12], "SD: %s", ui.discharge.logging ? "LOGGING" : "NOT LOGGING");
        (void)snprintf(wanted[13], sizeof wanted[13], "2:STOP DISCHARGE");
    }
    else
    {
        const char *reason = sample->stop_reason == LOAD_STOP_TIMER ? "TIME COMPLETE" :
            (sample->stop_reason == LOAD_STOP_FAULT ? "FAULT - CHECK SETUP" : "STOPPED BY USER");
        (void)snprintf(wanted[12], sizeof wanted[12], "%s", reason);
        (void)snprintf(wanted[13], sizeof wanted[13], "3:SETUP 1:RESTART");
    }
    wanted_fg[2] = wanted_fg[3] = wanted_fg[4] = CYAN;
}

static void BuildScreen(void)
{
    for (unsigned row = 0; row < UI_ROWS; ++row)
    {
        wanted[row][0] = '\0';
        wanted_fg[row] = WHITE;
        wanted_bg[row] = BLACK;
    }
    if (ui.view == UI_VIEW_SETUP)
    {
    (void)snprintf(wanted[0], sizeof wanted[0], "PROGRAMMABLE LOAD");
    const char *state = ui.load.running ? "RUNNING" :
        (ui.load.faults != 0U ? "FAULT / STOPPED" : "STOPPED");
    (void)snprintf(wanted[1], sizeof wanted[1], "%s  %s",
                   ui.settings.mode == LOAD_MODE_CC ? "CC" : "CV", state);
    wanted_fg[0] = CYAN;
    wanted_fg[1] = ui.load.running ? GREEN : (ui.load.faults ? RED : YELLOW);
    uint16_t ma = DisplayMilli(ui.settings.current_a);
    uint16_t mv = DisplayMilli(ui.settings.voltage_v);
    (void)snprintf(wanted[2], sizeof wanted[2], " %s:%2u.%02u A",
        ui.settings.mode == LOAD_MODE_CC ? "Current" : "I limit", (unsigned)ma / 1000U, ((unsigned)ma % 1000U) / 10U);
    (void)snprintf(wanted[3], sizeof wanted[3], " CV target:%2u.%01u V", (unsigned)mv / 1000U, ((unsigned)mv % 1000U) / 100U);
    unsigned long duration = (unsigned long)ui.session.max_duration_s;
    (void)snprintf(wanted[4], sizeof wanted[4], " Time:%02lu:%02lu:%02lu",
        duration / 3600UL, (duration / 60UL) % 60UL, duration % 60UL);
    (void)snprintf(wanted[5], sizeof wanted[5], " External:%3ld C", (long)ui.session.external_limit_c);
    (void)snprintf(wanted[6], sizeof wanted[6], " Internal:%3ld C", (long)ui.session.internal_limit_c);
    unsigned selected = 2U + (unsigned)ui.selected_field;
    wanted[selected][0] = '>';
    wanted_fg[selected] = Editable() ? CYAN : GRAY;
    wanted_bg[selected] = DARKBLUE;
    uint16_t measured_mv = DisplayMilli(ui.load.dut_voltage_v);
    uint16_t measured_ma = DisplayMilli(ui.load.measured_current_a);
    (void)snprintf(wanted[7], sizeof wanted[7], "DUT %2u.%02uV %2u.%02uA",
        (unsigned)measured_mv / 1000U, ((unsigned)measured_mv % 1000U) / 10U,
        (unsigned)measured_ma / 1000U, ((unsigned)measured_ma % 1000U) / 10U);
    unsigned long left = ((unsigned long)ui.load.remaining_ms + 999UL) / 1000UL;
    (void)snprintf(wanted[8], sizeof wanted[8], "Left %02lu:%02lu:%02lu",
        left / 3600UL, (left / 60UL) % 60UL, left % 60UL);
    char external[6] = "--", internal[6] = "--";
    float hottest_external = -INFINITY, hottest_internal = -INFINITY;
    for (unsigned i = 0; i < TEMPERATURE_COUNT; ++i)
    {
        if (!(ui.load.temperature.valid_mask & (1U << i)) || !isfinite(ui.load.temperature.c[i]))
            continue;
        if (temperature_channels[i].role == TEMP_EXTERNAL)
            hottest_external = fmaxf(hottest_external, ui.load.temperature.c[i]);
        if (temperature_channels[i].role == TEMP_INTERNAL)
            hottest_internal = fmaxf(hottest_internal, ui.load.temperature.c[i]);
    }
    if (isfinite(hottest_external))
        (void)snprintf(external, sizeof external, "%ld", (long)fmaxf(-999.0f, fminf(999.0f, hottest_external)));
    if (isfinite(hottest_internal))
        (void)snprintf(internal, sizeof internal, "%ld", (long)fmaxf(-999.0f, fminf(999.0f, hottest_internal)));
    (void)snprintf(wanted[9], sizeof wanted[9], "Now E:%.4s I:%.4s C", external, internal);
    (void)snprintf(wanted[10], sizeof wanted[10], "%s", StatusMessage());
    wanted_fg[10] = ui.load.faults || ui.start_rejected ? YELLOW : WHITE;
    (void)snprintf(wanted[11], sizeof wanted[11], "1:RUN 2:STOP 6:MODE");
    (void)snprintf(wanted[12], sizeof wanted[12], "3/4 FIELD 5:%s", ui.fine_step ? "FINE" : "COARSE");
    (void)snprintf(wanted[13], sizeof wanted[13], "ENC1 EDIT ENC2 TIME");
    }
    else BuildMeasurements();
    for (unsigned row = 0; row < UI_ROWS; ++row)
    {
        size_t length = strlen(wanted[row]);
        memset(wanted[row] + length, ' ', UI_COLS - length);
        wanted[row][UI_COLS] = '\0';
    }
}

static bool RenderOne(void)
{
    for (unsigned scan = 0; scan < UI_ROWS * UI_COLS; ++scan)
    {
        unsigned cell = cursor;
        cursor = (cursor + 1U) % (UI_ROWS * UI_COLS);
        unsigned row = cell / UI_COLS, col = cell % UI_COLS;
        if (painted[row][col] == wanted[row][col] &&
            painted_fg[row][col] == wanted_fg[row] && painted_bg[row][col] == wanted_bg[row])
            continue;
        char glyph[2] = {wanted[row][col], '\0'};
        ST7789_WriteString(10U + col * 11U, 8U + row * 22U, glyph,
                          Font_11x18, wanted_fg[row], wanted_bg[row]);
        painted[row][col] = wanted[row][col];
        painted_fg[row][col] = wanted_fg[row];
        painted_bg[row][col] = wanted_bg[row];
        return true;
    }
    return false;
}

void UI_Init(void)
{
    memset(&ui, 0, sizeof ui);
    memset(painted, 0, sizeof painted);
    cursor = 0U;
    Buttons_Init();
    Encoder_Init(&setpoint_encoder, ENC1_A_GPIO_Port, ENC1_A_Pin, ENC1_B_GPIO_Port, ENC1_B_Pin);
    Encoder_Init(&timer_encoder, ENC2_A_GPIO_Port, ENC2_A_Pin, ENC2_B_GPIO_Port, ENC2_B_Pin);
    ST7789_Init(); /* Full clear occurs once, while discharge is stopped. */
    Snapshot();
    BuildScreen();
    /* Complete initial paint while stopped; no input can start discharge here. */
    while (RenderOne()) LoadControl_Process();
    last_refresh = HAL_GetTick();
    redraw = true;
}

void UI_Task(void)
{
    Snapshot();
    Buttons_Update();
    bool start = Buttons_WasPressed(BUTTON_1);
    bool stop_press = Buttons_WasPressed(BUTTON_2);
    bool stop = stop_press || Buttons_IsHeld(BUTTON_2);
    bool previous = Buttons_WasPressed(BUTTON_3);
    bool next = Buttons_WasPressed(BUTTON_4);
    bool step = Buttons_WasPressed(BUTTON_5);
    bool mode = Buttons_WasPressed(BUTTON_6);
    int32_t set_delta = Encoder_ReadDelta(&setpoint_encoder);
    int32_t time_delta = Encoder_ReadDelta(&timer_encoder);
    if (stop)
    {
        if (stop_press || ui.load.running) Discharge_Stop();
        start = false;
        ui.start_rejected = false;
        ui.edit_blocked = false;
        redraw = true;
        Snapshot();
    }
    if (ui.view == UI_VIEW_SUMMARY && previous && Editable())
    {
        ui.view = UI_VIEW_SETUP;
        previous = false;
        ui.start_rejected = ui.edit_blocked = false;
        redraw = true;
    }
    bool setup = ui.view == UI_VIEW_SETUP;
    if (setup && (previous || next))
    {
        unsigned field = (unsigned)ui.selected_field;
        if (previous) field = (field + UI_FIELD_COUNT - 1U) % UI_FIELD_COUNT;
        if (next) field = (field + 1U) % UI_FIELD_COUNT;
        ui.selected_field = (UI_Field_t)field;
        redraw = true;
    }
    if (setup && step) {ui.fine_step = !ui.fine_step;redraw = true;}
    if (setup && mode)
    {
        if (Editable())
        {
            LoadSettings s = ui.settings;
            s.mode = s.mode == LOAD_MODE_CC ? LOAD_MODE_CV : LOAD_MODE_CC;
            (void)LoadControl_SetSettings(&s);
            ui.start_rejected = ui.edit_blocked = false;
            Snapshot();
        }
        else ui.edit_blocked = true;
        redraw = true;
    }
    if (setup) AdjustParameters(set_delta, time_delta);
    if (start && !ui.load.running && !ui.discharge.logging)
    {
        ui.start_rejected = !Discharge_Start();
        if (ui.start_rejected) ui.view = UI_VIEW_SETUP;
        ui.edit_blocked = false;
        redraw = true;
    }
    Snapshot();
    uint32_t now = HAL_GetTick();
    if (redraw || (uint32_t)(now - last_refresh) >= UI_REFRESH_MS)
    {
        BuildScreen();
        last_refresh = now;
        redraw = false;
    }
    (void)RenderOne();
}

const UI_State_t *UI_GetState(void) { return &ui; }
