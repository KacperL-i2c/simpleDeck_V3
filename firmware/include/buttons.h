/* =============================================================================
 *  GREJEM OS / buttons.h
 *  Programowy debouncing (integrator) dla 4 przycisków.
 * ============================================================================= */
#ifndef GREJEM_BUTTONS_H
#define GREJEM_BUTTONS_H

#include <stdint.h>

/* Konfiguruje piny przycisków jako wejścia z pull-up. */
void buttons_init(void);

/* Polling 1 kHz - wywoływany przez scheduler co 1 ms.
 * Po każdej wykrytej zmianie stanu emituje zdarzenie PROTO_BUTTON_EVT. */
void buttons_poll(void);

/* V3: Zwraca zdebouncowany stan przycisku idx (0=niewciśnięty, 1=wciśnięty).
 * Używane przez tryb LED_MODE_BUTTONS. */
uint8_t button_debounced_state(uint8_t idx);

#endif /* GREJEM_BUTTONS_H */
