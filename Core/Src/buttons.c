#include "buttons.h"
#include "main.h"

#define BUTTON_DEBOUNCE_MS 20U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} ButtonPin;

typedef struct
{
    bool raw_pressed;
    bool stable_pressed;
    bool press_pending;
    uint32_t changed_at;
} ButtonState;

/* Order must match BUTTON_1 through BUTTON_6 in buttons.h. */
static const ButtonPin button_pins[BUTTON_COUNT] =
{
    {BUTTON1_GPIO_Port, BUTTON1_Pin},
    {BUTTON2_GPIO_Port, BUTTON2_Pin},
    {BUTTON3_GPIO_Port, BUTTON3_Pin},
    {BUTTON4_GPIO_Port, BUTTON4_Pin},
    {BUTTON5_GPIO_Port, BUTTON5_Pin},
    {BUTTON6_GPIO_Port, BUTTON6_Pin}
};

static ButtonState button_states[BUTTON_COUNT];

static bool ReadButton(ButtonId button)
{
    return HAL_GPIO_ReadPin(button_pins[button].port,
                            button_pins[button].pin) == GPIO_PIN_RESET;
}

void Buttons_Init(void)
{
    uint32_t now = HAL_GetTick();

    for (unsigned int i = 0; i < BUTTON_COUNT; i++)
    {
        bool pressed = ReadButton((ButtonId)i);

        button_states[i].raw_pressed = pressed;
        button_states[i].stable_pressed = pressed;
        button_states[i].press_pending = false;
        button_states[i].changed_at = now;
    }
}

void Buttons_Update(void)
{
    uint32_t now = HAL_GetTick();

    for (unsigned int i = 0; i < BUTTON_COUNT; i++)
    {
        ButtonState *state = &button_states[i];
        bool pressed = ReadButton((ButtonId)i);

        /* Restart the debounce timer whenever the input changes. */
        if (pressed != state->raw_pressed)
        {
            state->raw_pressed = pressed;
            state->changed_at = now;
        }

        /* Accept the new state after it has remained stable. */
        if ((state->raw_pressed != state->stable_pressed) &&
            ((uint32_t)(now - state->changed_at) >= BUTTON_DEBOUNCE_MS))
        {
            state->stable_pressed = state->raw_pressed;

            if (state->stable_pressed)
            {
                state->press_pending = true;
            }
        }
    }
}

bool Buttons_WasPressed(ButtonId button)
{
    if ((unsigned int)button >= BUTTON_COUNT)
    {
        return false;
    }

    bool pending = button_states[button].press_pending;
    button_states[button].press_pending = false;

    return pending;
}

bool Buttons_IsHeld(ButtonId button)
{
    if ((unsigned int)button >= BUTTON_COUNT)
    {
        return false;
    }

    return button_states[button].stable_pressed;
}
