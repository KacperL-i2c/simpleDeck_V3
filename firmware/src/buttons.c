/* =============================================================================
 *  GREJEM OS / buttons.c
 *
 *  Debouncing 4 przycisków mechanicznych (PB6..PB9) metodą "integratora":
 *  polling co 1 ms, każdy przycisk ma licznik 0..DEBOUNCE_TICKS.
 *  Przy każdym zgodnym odczytaniu licznik rośnie, niezgodnym - maleje.
 *  Stan debounced przełącza się dopiero gdy licznik osiągnie 0 lub MAX.
 *
 *  Hardware: switch do GND, wewnętrzny pull-up włączony.
 *           Wciśnięty = stan niski na pinie (active-low).
 *
 *  Po wykryciu zmiany stanu emitowany jest PROTO_BUTTON_EVT do PC.
 * ============================================================================= */
#include "buttons.h"
#include "board.h"
#include "config.h"
#include "protocol.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include <string.h>

typedef struct {
    uint8_t integrator;     /* 0..CFG_DEBOUNCE_TICKS  */
    uint8_t state;          /* debounced: 0=nie, 1=tak */
} button_t;

static button_t buttons[BUTTON_COUNT];

void buttons_init(void) {
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        /* Wejście z pull-up: tryb INPUT, cnf PULL_UPDOWN, po czym ustaw
         * bit pinu (1) w ODR - wtedy działa pull-up (0 = pull-down). */
        gpio_set_mode(board_buttons[i].port,
                      GPIO_MODE_INPUT,
                      GPIO_CNF_INPUT_PULL_UPDOWN,
                      board_buttons[i].pin);
        gpio_set(board_buttons[i].port, board_buttons[i].pin);   /* pull-up */
    }
    memset(buttons, 0, sizeof(buttons));
}

/* Polling co 1 ms - wywoływany z schedulera */
void buttons_poll(void) {
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        /* Active-low: gpio_get zwraca nonzero (prawdę) jeśli pin=1 = NIE wciśnięty */
        uint8_t raw_pressed = gpio_get(board_buttons[i].port,
                                       board_buttons[i].pin) ? 0 : 1;

        button_t *b = &buttons[i];

        /* Integrator z histerezą */
        if (raw_pressed) {
            if (b->integrator < CFG_DEBOUNCE_TICKS) b->integrator++;
        } else {
            if (b->integrator > 0) b->integrator--;
        }

        /* Mapa integratora → stanu (histereza: tylko końce zmieniają stan) */
        uint8_t new_state = b->state;
        if      (b->integrator >= CFG_DEBOUNCE_TICKS) new_state = 1;
        else if (b->integrator == 0)                  new_state = 0;

        /* Emituj event TYLKO przy zmianie zbocza */
        if (new_state != b->state) {
            b->state = new_state;
            protocol_emit_button(i, new_state);
        }
    }
}

/* V3: Akcesor zdebouncowanego stanu — używane przez LED_MODE_BUTTONS */
uint8_t button_debounced_state(uint8_t idx) {
    if (idx >= BUTTON_COUNT) return 0;
    return buttons[idx].state;
}
