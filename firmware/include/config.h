/* =============================================================================
 *  GREJEM OS / config.h
 *  Centralne miejsce dla wszystkich stałych strojalnych firmware'u.
 *  Edytuj wartości tutaj, nie w kodzie.
 * ============================================================================= */
#ifndef GREJEM_CONFIG_H
#define GREJEM_CONFIG_H

/* ===========================================================================
 *  ADC + DMA
 * =========================================================================== */
/* Liczba próbek zapamiętywanych w buforze DMA na każdy kanał.
 * Bufor kołowy ma rozmiar: POT_COUNT * CFG_ADC_OVERSAMPLE halfwords.
 * Przy pot_COUNT=5 i OVERSAMPLE=16  →  80 halfwords = 160 bajtów RAM.
 * Przerwania DMA HT (half-transfer) i TC (transfer-complete) uśredniają
 * po 8 próbek na kanał naraz. */
#define CFG_ADC_OVERSAMPLE          16

/* Czas próbkowania ADC (239.5 cyklu = najwyższa stabilność, ~14 µs/próbka) */
#define CFG_ADC_SAMPLE_TIME         7   /* ADC_SMPR_SMP_239DOT5 */

/* ===========================================================================
 *  Adaptacyjny filtr EMA + strefa martwa (Deadband)
 *
 *  Stałopozycinkowa arytmetyka ×256 (Q8) - brak float na Cortex-M3 (soft-fp).
 *
 *  Logika:
 *    1) err = raw - ema
 *    2) jeśli |err| < DEADBAND  → szum, ignoruj (return)
 *    3) alpha = (|err| > FAST_THR) ? ALPHA_FAST : ALPHA_SLOW   ← adaptacja
 *    4) ema += alpha × (raw - ema)
 *    5) jeśli |ema - last_sent| ≥ SEND_THR → ustaw dirty=1 (do wysłania)
 * =========================================================================== */

/* Wsuwanie/kręcenie powoli: ignoruj drgania potencjometru i zakłócenia ADC.
 * Skala 0..4095 (12-bit). 8 LSB = ~0.2% pełnej skali. */
#define CFG_DEADBAND                8

/* Powyżej tej zmiany = gwałtowny ruch → przełączenie na alfa szybki (no-lag). */
#define CFG_FAST_THR                128

/* Współczynniki EMA ×256 (0..256). Mniejsze = mocniejsze wygładzanie.
 *  ALPHA_SLOW = 13/256  ≈ 0.05  (bardzo gładko)
 *  ALPHA_FAST = 205/256 ≈ 0.80  (prawie bez opóźnienia) */
#define CFG_ALPHA_SLOW_X256         13
#define CFG_ALPHA_FAST_X256         205

/* Wysyłaj raport do PC dopiero gdy wartość przefiltrowana zmieni się
 * istotnie. Redukuje ruch USB. */
#define CFG_SEND_THR                16

/* ===========================================================================
 *  Debouncing przycisków (integrator)
 * =========================================================================== */
/* Polling co 1 ms. Integrator liczy kolejne zgodne odczyty do limitu
 * CFG_DEBOUNCE_TICKS. Po osiągnięciu limitu stan = wciśnięty.
 * 5 ms jest optymalne dla większości mechanicznych switchy. */
#define CFG_DEBOUNCE_TICKS          5

/* ===========================================================================
 *  Heartbeat
 * =========================================================================== */
/* Pakiet heartbeat wysyłany do PC co CFG_HEARTBEAT_PERIOD_MS.
 * Jeśli PC nie otrzyma heartbeatu przez ~3× ten czas, uznaje połączenie
 * za utracone i przechodzi w tryb auto-reconnect. */
#define CFG_HEARTBEAT_PERIOD_MS     1500

/* ===========================================================================
 *  V2: VU bar — wskaźnik głośności (8 LED)
 * ===========================================================================
 *  Po odebraniu LED_CMD(mode=VU_BAR) linijka świeci proporcjonalnie do poziomu.
 *  Brak nowej ramki przez VU_TIMEOUT_MS → płynne wygaszenie (fade) przez
 *  VU_FADE_MS, po czym LEDy idą w stan SLEEP (zgaszone). */
#define CFG_VU_TIMEOUT_MS           3000    /* brak ruchu potencjometru */
#define CFG_VU_FADE_MS              300     /* czas płynnego wygaszania */

/* ===========================================================================
 *  LED blink
 * =========================================================================== */
/* Okres migania diody statusowej i każdej diody w trybie blink. */
#define CFG_LED_BLINK_PERIOD_MS     250

/* ===========================================================================
 *  USB HID (Custom HID, EP1 IN + EP1 OUT)
 * =========================================================================== */
#define CFG_HID_EP_SIZE             64      /* max EP interrupt packet */
#define CFG_HID_EP_IN               0x81    /* EP1 IN  (MCU → PC) */
#define CFG_HID_EP_OUT              0x01    /* EP1 OUT (PC  → MCU) */
#define CFG_HID_POLL_INTERVAL_MS    1       /* bInterval w deskryptorze */

#endif /* GREJEM_CONFIG_H */
