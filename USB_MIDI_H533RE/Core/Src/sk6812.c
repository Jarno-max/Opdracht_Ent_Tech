#include "sk6812.h"

extern TIM_HandleTypeDef htim3;

// Met een kloksnelheid van 250 MHz en ARR=312 krijgen we ~800 kHz.
// 0.32 us hoge puls = (0.32 / 1.25) * 312 ≈ 80
// 0.64 us hoge puls = (0.64 / 1.25) * 312 ≈ 160
#define PWM_HI 160
#define PWM_LO 80

uint16_t dma_buffer[DMA_BUFF_SIZE];
static uint8_t led_data[NUM_LEDS][3]; // Houdt huidige kleuren bij (GRB) voor referenties

void sk6812_init(void) {
    // Initialiseer buffertijd voor reset op 0
    for (int i = 0; i < DMA_BUFF_SIZE; i++) {
        dma_buffer[i] = 0;
    }
    
    // Zet alle leds standaard uit
    for (int i = 0; i < NUM_LEDS; i++) {
        set_led_color(i, 0, 0, 0);
    }
}

// Functie voor set_led_color
// SK6812 expects data in GRB order. We sturen MSB first per byte.
void set_led_color(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b) {
    if (led_index >= NUM_LEDS) return;

    // Sla waarden op (optioneel, handig voor effecten)
    led_data[led_index][0] = g;
    led_data[led_index][1] = r;
    led_data[led_index][2] = b;

    // Zet kleur in 24-bits variabele, volgorde: G - R - B
    // Voor de SK6812 Mini-E, Groen is eerst, dan Rood, dan Blauw
    uint32_t color = ((uint32_t)g << 16) | ((uint32_t)r << 8) | ((uint32_t)b);
    uint32_t start_indx = led_index * 24;

    for (int i = 23; i >= 0; i--) {
        if ((color >> i) & 0x01) {
            dma_buffer[start_indx + (23 - i)] = PWM_HI;
        } else {
            dma_buffer[start_indx + (23 - i)] = PWM_LO;
        }
    }
}

void sk6812_update(void) {
    // Start het kopiëren van het RAM buffer naar het TIM3 CCR1 register.
    HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_1, (uint32_t *)dma_buffer, DMA_BUFF_SIZE);
}

// HAL Callback afhandelingen voor GPDMA Transfer Complete
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
    // Stop de DMA en de PWM nadat de buffer verzonden is.
    // Hierdoor wordt de pin op LAAG gehouden -> Reset signaal.
    if (htim->Instance == TIM3) {
        HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_1);
    }
}

// Callback kan ook via de gewone DMA completion getriggerd worden bij sommige HAL library versies,
// als failsafe stoppen we de timer in elke transfer complete macro die CubeMX heeft gegenereerd.
// In de STM32H5 HAL werkt PulseFinished betrouwbaar.

void sk6812_startup_animation(void) {
    sk6812_init();

    // Alle LEDs individueel even aan en uit zetten (groen)
    for(int i = 0; i < NUM_LEDS; i++) {
        set_led_color(i, 0, 50, 0); // r=0, g=50, b=0 (gedimd groen)
        sk6812_update();
        HAL_Delay(50); // Wacht even per LED voor de animatie
        set_led_color(i, 0, 0, 0); 
    }
    
    // Zet ze op de "Geen activiteit" stand-by kleur (bijv. gedimd blauw)
    for(int i = 0; i < NUM_LEDS; i++) {
        set_led_color(i, 0, 0, 5); // gedimd blauw
    }
    sk6812_update();
}