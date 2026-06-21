/* =============================================================================
 *  GREJEM OS / board.c
 *  Stałe opisujące fizyczne mapowanie pinów (zdefiniowane w board.h).
 * ============================================================================= */
#include "board.h"

/* PB6..PB9 - przyciski (active-low, podciągnięte pull-upem wewnętrznym).
 * Switch do GND: wciśnięty = stan niski na pinie. */
const struct button_pin board_buttons[BUTTON_COUNT] = {
    { GPIOB, GPIO6 },
    { GPIOB, GPIO7 },
    { GPIOB, GPIO8 },
    { GPIOB, GPIO9 },
};

/* V2: 8-LED linijka VU bar (PB10..PB15 + PA9 + PA10).
 * LED 0,1 (PB10/PB11) — hardware PWM przez TIM2_CH3/CH4.
 * LED 2..7 (PB12..PB15, PA9, PA10) — software PWM przez TIM3 ISR.
 * Anoda LED przez rezystor ~330Ω do pinu, katoda do GND. */
const struct led_pin board_leds[LED_COUNT] = {
    { GPIOB, GPIO10 },
    { GPIOB, GPIO11 },
    { GPIOB, GPIO12 },
    { GPIOB, GPIO13 },
    { GPIOB, GPIO14 },
    { GPIOB, GPIO15 },
    { GPIOA, GPIO9  },
    { GPIOA, GPIO10 },
};
