#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>

/* Six interface buttons. Encoder push switches can be added later. */
typedef enum
{
    BUTTON_1 = 0,
    BUTTON_2,
    BUTTON_3,
    BUTTON_4,
    BUTTON_5,
    BUTTON_6,
    BUTTON_COUNT
} ButtonId;

/* Call once after MX_GPIO_Init(). */
void Buttons_Init(void);

/* Call frequently from the main loop to debounce the inputs. */
void Buttons_Update(void);

/* Returns true once per detected press, then clears that event. */
bool Buttons_WasPressed(ButtonId button);

/* Returns true while the debounced button state is pressed. */
bool Buttons_IsHeld(ButtonId button);

#endif /* BUTTONS_H */
