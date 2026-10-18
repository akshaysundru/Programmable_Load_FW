#ifndef UI_H
#define UI_H

#include "load_control.h"
#include "discharge_session.h"
#include <stdbool.h>

typedef enum {
    UI_FIELD_CURRENT = 0,
    UI_FIELD_VOLTAGE,
    UI_FIELD_DURATION,
    UI_FIELD_EXTERNAL_TEMP,
    UI_FIELD_INTERNAL_TEMP,
    UI_FIELD_COUNT
} UI_Field_t;

typedef enum { UI_VIEW_SETUP = 0, UI_VIEW_RUNNING, UI_VIEW_SUMMARY } UI_View_t;

typedef struct {
    UI_View_t view;
    bool last_run_available;
    LoadStatus last_run; /* Last UI-observed running measurements; frozen after Stop. */
    UI_Field_t selected_field;
    bool fine_step;
    bool start_rejected;
    bool edit_blocked;
    LoadSettings settings;
    LoadSessionSettings session;
    LoadStatus load;
    DischargeStatus discharge;
} UI_State_t;

/* Call once after GPIO/DMA/ADC/DAC/SPI and LoadControl_Init.
 * Owns Buttons_Init, both encoder objects and ST7789 initialization.
 * Discharge_Init follows UI_Init so its SD hook uses initialized buttons. */
void UI_Init(void);
/* Main-context only. Polls controls, applies stopped-only edits, dispatches
 * Start/Stop and renders at most one changed 11x18 glyph per call.
 * Continue calling LoadControl_Process and Discharge_Process from main. */
void UI_Task(void);
/* Read-only snapshot; output/timer state always comes from LoadControl. */
const UI_State_t *UI_GetState(void);

#endif
