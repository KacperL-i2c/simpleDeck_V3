/* =============================================================================
 *  GREJEM OS / scheduler.h
 *  Non-blocking task scheduler oparty o SysTick (1 kHz).
 * ============================================================================= */
#ifndef GREJEM_SCHEDULER_H
#define GREJEM_SCHEDULER_H

#include <stdint.h>

/* Inicjalizuje SysTick @ 1 kHz, zeruje licznik. Rejestruje tablicę zadań. */
void scheduler_init(void);

/* Wywoływane w superloopie głównym. Sprawdza czas ostatniego uruchomienia
 * każdej funkcji i wywołuje ją jeśli upłynął jej okres. */
void scheduler_dispatch(void);

/* Monotoniczna liczba milisekund od startu (32-bit, roluje co ~49 dni). */
uint32_t scheduler_millis(void);

#endif /* GREJEM_SCHEDULER_H */
