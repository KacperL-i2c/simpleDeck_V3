/* =============================================================================
 *  Mock state - definicje zmiennych globalnych zmockowanych
 * ============================================================================= */
#include "libopencm3_mock.h"
#include "timer.h"                 /* prototypy timer_init / timer_set_hw_brightness */

uint32_t mock_gpio_idr[4] = {0, 0, 0, 0};   /* IDR dla GPIOA/B/C/D */
uint32_t mock_adc_dr = 0;
uint8_t mock_ep_write_returns = 1;          /* default: write sukces */

const uint8_t rcc_hse_configs[1] = {0};

/* Mock scheduler_millis - zwraca konfigurowalny "czas" dla testów.
 * Test może ustawić tę zmienną przed wywołaniem testowanej funkcji. */
uint32_t mock_scheduler_ms = 0;
uint32_t scheduler_millis(void) { return mock_scheduler_ms; }

/* ---- C10: Mock funkcji timer.c dla testów jednostkowych ----
 * timer.c NIE jest kompilowany w testach (nie ma go w regułach Makefile).
 * leds.c wywołuje timer_set_hw_brightness() — mock przechwytuje wartość
 * do tablicy mock_hw_brightness, dzięki czemu testy mogą zweryfikować
 * ustawioną jasność HW PWM bez prawdziwego TIM2. */
uint8_t mock_hw_brightness[LED_HW_PWM_COUNT] = {0, 0};

void timer_init(void) {
    /* no-op w testach */
}

void timer_set_hw_brightness(uint8_t ch, uint8_t level) {
    if (ch < LED_HW_PWM_COUNT) mock_hw_brightness[ch] = level;
}

/* V3: Weak stub button_debounced_state — używany gdy buttons.c nie jest
 * kompilowany w teście (np. test_leds, test_vu_bar). Gdy buttons.c jest
 * linkowany (test_buttons), silna definicja nadpisuje weak.
 * UWAGA: nie includuj buttons.h tutaj — jego deklaracja bez __attribute__((weak))
 * nadpisałaby atrybut weak na tej definicji (GCC 10+, -fno-common). */
__attribute__((weak)) uint8_t button_debounced_state(uint8_t idx) {
    (void)idx;
    return 0;
}
