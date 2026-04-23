#ifndef SK6812_H
#define SK6812_H

#include "main.h"

// Configuratie
#define NUM_LEDS 16
#define WS2812_RESET_PULSES 64  // 64 pulsen van 1.25 us = ~80 us reset

// Buffergrootte per transfer. Elk bitje in SRAM wordt 1 PWM-puls.
#define DMA_BUFF_SIZE ((NUM_LEDS * 24) + WS2812_RESET_PULSES)

extern uint16_t dma_buffer[DMA_BUFF_SIZE];

// Functies
void sk6812_init(void);
void set_led_color(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b);
void sk6812_update(void);
void sk6812_startup_animation(void);

#endif // SK6812_H