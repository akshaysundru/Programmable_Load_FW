#include "encoder.h"

static uint8_t encoder_state(Encoder_t *enc)
{
    uint8_t a = (HAL_GPIO_ReadPin(enc->port_a, enc->pin_a) == GPIO_PIN_SET);
    uint8_t b = (HAL_GPIO_ReadPin(enc->port_b, enc->pin_b) == GPIO_PIN_SET);
    return (uint8_t)((a << 1) | b);
}

void Encoder_Init(Encoder_t *enc,
                  GPIO_TypeDef *port_a, uint16_t pin_a,
                  GPIO_TypeDef *port_b, uint16_t pin_b)
{
    enc->port_a = port_a;
    enc->pin_a = pin_a;
    enc->port_b = port_b;
    enc->pin_b = pin_b;
    enc->previous_state = encoder_state(enc);
    enc->transition_count = 0;
}

int32_t Encoder_ReadDelta(Encoder_t *enc)
{
    /* Quadrature transition lookup table. Invalid two-bit jumps are ignored. */
    static const int8_t table[16] = {
         0, -1, +1,  0,
        +1,  0,  0, -1,
        -1,  0,  0, +1,
         0, +1, -1,  0
    };

    uint8_t current = encoder_state(enc);
    uint8_t index = (uint8_t)((enc->previous_state << 2) | current);
    enc->transition_count += table[index];
    enc->previous_state = current;

    /*
     * Many mechanical encoders generate four transitions per detent.
     * If your encoder differs, tune this threshold after testing.
     */
    if (enc->transition_count >= 4) {
        enc->transition_count -= 4;
        return +1;
    }
    if (enc->transition_count <= -4) {
        enc->transition_count += 4;
        return -1;
    }
    return 0;
}
