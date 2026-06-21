/* =============================================================================
 *  GREJEM OS / timer.h
 *
 *  Konfiguracja timerów sprzętowych dla obsługi LED:
 *    TIM2 — hardware PWM na PB10 (CH3) i PB11 (CH4), ~1 kHz, 256 poziomów.
 *    TIM3 — przerwanie ~8 kHz dla software PWM na PB12..PB15, PA9, PA10 (64 poziomy).
 *
 *  V2: 8-LED VU bar. HW PWM = LED 0/1 (PB10/PB11). SW PWM = LED 2..7.
 * ============================================================================= */
#ifndef GREJEM_TIMER_H
#define GREJEM_TIMER_H

#include <stdint.h>

/* Pierwsze 2 LEDy (PB10, PB11) mają hardware PWM przez TIM2_CH3/CH4.
 * Pozostałe 6 (PB12..PB15, PA9, PA10) używają software PWM napędzanego
 * przerwaniem TIM3. V2: 8-LED VU bar. */
#define LED_HW_PWM_COUNT   2

/* Inicjalizuje TIM2 (HW PWM) i TIM3 (SW PWM ISR). Wywołać po leds_init(). */
void timer_init(void);

/* Ustawia jasność kanału hardware PWM.
 *   ch=0 → PB10 (TIM2_CH3), ch=1 → PB11 (TIM2_CH4).
 *   level: 0..255 (0=OFF, 255= pełna jasność). */
void timer_set_hw_brightness(uint8_t ch, uint8_t level);

#endif /* GREJEM_TIMER_H */
