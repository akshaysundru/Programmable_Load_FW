#ifndef ENCODER_H
#define ENCODER_H

#include "main.h"
#include <stdint.h>

typedef struct {
    GPIO_TypeDef *port_a;
    uint16_t pin_a;
    GPIO_TypeDef *port_b;
    uint16_t pin_b;
    uint8_t previous_state;
    int16_t transition_count;
} Encoder_t;

void Encoder_Init(Encoder_t *enc,
                  GPIO_TypeDef *port_a, uint16_t pin_a,
                  GPIO_TypeDef *port_b, uint16_t pin_b);

/* Returns -1, 0, or +1 per detent. Call frequently from the main loop. */
int32_t Encoder_ReadDelta(Encoder_t *enc);

#endif
