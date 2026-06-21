/* =============================================================================
 *  GREJEM OS / timer.c
 *
 *  Konfiguracja TIM2 (hardware PWM) i TIM3 (software PWM ISR).
 *
 *  TIM2:
 *    - Zegar timera: 72 MHz (APB1 ×2 prescaler).
 *    - Prescaler = 280 → 72 MHz / 281 ≈ 256 kHz.
 *    - ARR = 255 → PWM ≈ 1 kHz z 256-stopniową rozdzielczością jasności.
 *    - CH3 = PB10, CH4 = PB11 (wymaga TIM2 partial remap 2 — AFIO_MAPR bits 9:8 = 0b10).
 *
 *  TIM3:
 *    - Zegar timera: 72 MHz (prescaler = 0).
 *    - ARR = 8999 → update event co 125 µs = 8 kHz.
 *    - Przerwanie wywołuje leds_sw_pwm_tick() która realizuje 64-stopniowy
 *      software PWM na pinach PB12..PB15, PA9, PA10 (6 pinów, V2).
 *
 *  C10: Nowy moduł — rozszerzenie LED o PWM.
 * ============================================================================= */
#include "timer.h"
#include "leds.h"
#include "board.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

void timer_init(void) {
    /* ---- Taktowanie peryferiów ---- */
    rcc_periph_clock_enable(RCC_TIM2);
    rcc_periph_clock_enable(RCC_TIM3);
    /* RCC_AFIO już włączone w main.c::clock_setup() */

    /* ---- TIM2 partial remap 2: CH3→PB10, CH4→PB11 ----
     * Bez remapu CH3=PA2, CH4=PA3 (konflikt z potencjometrami ADC).
     * AFIO_MAPR bits [9:8]: 00=no, 01=partial1, 10=partial2, 11=full.
     * Wartość 10 przenosi tylko CH3/CH4 na PB10/PB11; CH1/CH2 zostają na PA0/PA1. */
    AFIO_MAPR = (AFIO_MAPR & ~((uint32_t)0x3 << 8)) | ((uint32_t)0x2 << 8);

    /* ---- PB10, PB11 jako alternate-function push-pull (50 MHz) ---- */
    gpio_set_mode(GPIOB,
                  GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
                  GPIO10 | GPIO11);

    /* ---- TIM2: PWM base ~1 kHz, 256 levels ---- */
    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM2, 280);        /* 72 MHz / 281 ≈ 256 kHz */
    timer_continuous_mode(TIM2);
    timer_set_period(TIM2, 255);           /* 256 kHz / 256 = 1 kHz */
    timer_enable_preload(TIM2);

    /* CH3 (PB10) i CH4 (PB11) w trybie PWM mode 1 (high while counter < CCR) */
    timer_set_oc_mode(TIM2, TIM_OC3, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM2, TIM_OC4, TIM_OCM_PWM1);
    timer_enable_oc_output(TIM2, TIM_OC3);
    timer_enable_oc_output(TIM2, TIM_OC4);

    /* Początkowo zgaszone (CCR = 0) */
    timer_set_oc_value(TIM2, TIM_OC3, 0);
    timer_set_oc_value(TIM2, TIM_OC4, 0);

    timer_enable_counter(TIM2);

    /* ---- TIM3: przerwanie 8 kHz dla software PWM ---- */
    timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM3, 0);          /* pełne 72 MHz */
    timer_continuous_mode(TIM3);
    timer_set_period(TIM3, 8999);          /* 72 MHz / 9000 = 8 kHz */
    timer_enable_preload(TIM3);

    timer_enable_irq(TIM3, TIM_DIER_UIE);
    nvic_enable_irq(NVIC_TIM3_IRQ);

    timer_enable_counter(TIM3);
}

void timer_set_hw_brightness(uint8_t ch, uint8_t level) {
    if (ch == 0)      timer_set_oc_value(TIM2, TIM_OC3, level);
    else if (ch == 1) timer_set_oc_value(TIM2, TIM_OC4, level);
}

/* ---- TIM3 update interrupt ISR ----
 * Software PWM dla PB12..PB15, PA9, PA10 (V2: 6 pinów). Wywołuje leds_sw_pwm_tick() która
 * ustawia gpio_set/clear na podstawie 64-stopniowego licznika PWM. */
void tim3_isr(void) {
    timer_clear_flag(TIM3, TIM_SR_UIF);
    leds_sw_pwm_tick();
}
