/* =============================================================================
 *  GREJEM OS / leds.h
 *  V2: 8-LED linijka VU bar (wskaźnik głośności) z auto-focus i fade-out.
 *  Sterowanie 8 diodami LED (PB10..PB15, PA9, PA10) z PWM.
 *  Funkcje legacy (led_set / led_set_ext) zachowane dla testów i kompatybilności.
 * ============================================================================= */
#ifndef GREJEM_LEDS_H
#define GREJEM_LEDS_H

#include <stdint.h>

/* Tryby diody — muszą być identyczne z desktop/src/.../profile.py::LedMode.
 *
 *  V2: Głównym trybem jest VU_BAR (9) — linijka LED pokazuje poziom
 *  głośności aktywnego kanału, wygasa po 3 s bezczynności.
 *  Tryby legacy (0..7) zachowane w sterowniku dla testów; protokół V2
 *  wysyła tylko VU_BAR, a legacy tryby NAK-uje (ERR_BAD_TYPE).
 *
 *  V3: Nowe globalne tryby linijki (10..16):
 *    SOLID       — wszystkie LED świecą ciągle z zadana jasnością.
 *    BREATHING   — wszystkie LED "oddychają" (sinusoida).
 *    CHASE       — zapalona jedna LED biegnie w przód (wrap).
 *    KNIGHT_RIDER — scanner: pozycja biegnie tam i z powrotem (KITT).
 *    STROBE_BAR  — wszystkie LED migają z regulowanym duty cycle.
 *    BUTTONS     — każda LED odzwierciedla stan przycisku (0..2).
 *    MANUAL      — indywidualna jasność per-LED (leds_set_manual).
 *
 *  VU_BAR — linijka proporcjonalna do brightness(0..255): segmentowane
 *            + płynny top LED (PWM). CH = aktywny kanał (0..4).
 *
 *  LEGACY (8) — tryb per-LED dla led_set / led_set_ext (testy sterownika).
 *               leds_update() renderuje każdą LED niezależnie. */
enum {
    LED_MODE_OFF         = 0,
    LED_MODE_ON          = 1,
    LED_MODE_BLINK       = 2,
    LED_MODE_DIM         = 3,
    LED_MODE_PULSE       = 4,
    LED_MODE_BREATHE     = 5,
    LED_MODE_STROBE      = 6,
    LED_MODE_HEARTBEAT   = 7,
    LED_MODE_LEGACY      = 8,    /* V3: tryb legacy per-LED */
    LED_MODE_VU_BAR      = 9,    /* V2: wskaźnik głośności */
    LED_MODE_SOLID       = 10,   /* V3: wszystkie LED ciągle */
    LED_MODE_BREATHING   = 11,   /* V3: wszystkie LED oddychają */
    LED_MODE_CHASE       = 12,   /* V3: pościg */
    LED_MODE_KNIGHT_RIDER= 13,   /* V3: scanner KITT */
    LED_MODE_STROBE_BAR  = 14,   /* V3: stroboskop linijki */
    LED_MODE_BUTTONS     = 15,   /* V3: wskaźnik przycisków */
    LED_MODE_MANUAL      = 16,   /* V3: ręczna jasność per-LED */
};

/* Konfiguruje piny LED jako wyjścia i inicjalizuje stan (wszystkie OFF). */
void leds_init(void);

/* V2: Ustawia poziom linijki VU bar. Resetuje timer wygaszania (3 s).
 *   ch    — aktywny kanał 0..4 (zapamiętany, obecnie niewykorzystany do renderu)
 *   level — poziom głośności 0..255 (0=wszystkie zgaszone, 255=wszystkie świecą) */
void leds_set_vu(uint8_t ch, uint8_t level);

/* V3: Ustawia globalny tryb linijki LED.
 *   mode       — LED_MODE_SOLID..LED_MODE_BUTTONS (10..15)
 *   brightness — globalna jasność 0..255
 *   speed_ms   — okres animacji w ms (0 = domyślny)
 *   arg        — argument wzorca (np. duty cycle % dla STROBE_BAR) */
void leds_set_mode(uint8_t mode, uint8_t brightness, uint16_t speed_ms, uint8_t arg);

/* V3: Ustawia ręczną jasność per-LED (tryb LED_MODE_MANUAL).
 *   levels — tablica jasności 0..255 (min count = ACTIVE_LED_COUNT)
 *   count  — liczba elementów w levels */
void leds_set_manual(const uint8_t *levels, uint8_t count);

/* V3: Zwraca aktualny globalny tryb linijki LED. */
uint8_t leds_get_mode(void);

/* Ustawia tryb diody `idx` (legacy API — używa domyślnej jasności i okresu).
 * Zachowany dla kompatybilności wstecznej / testów sterownika. */
void led_set(uint8_t idx, uint8_t mode);

/* Rozszerzone API — ustawia tryb, jasność, okres animacji i argument wzorca.
 * Zachowane dla testów sterownika (legacy). V2 protokół używa leds_set_vu. */
void led_set_ext(uint8_t idx, uint8_t mode, uint8_t brightness,
                 uint16_t period_ms, uint8_t arg);

/* Aktualizuje animacje LED (wywoływane przez scheduler co 20 ms = 50 Hz).
 * V3: Renderuje aktualny globalny tryb linijki (led_mode_global).
 *     VU_BAR: segmenty + timeout/fade. SOLID/BREATHING/CHASE/...: animacje.
 *     LEGACY: per-LED (testy sterownika). */
void leds_update(void);

/* Software PWM tick — wywoływane przez TIM3 ISR (~8 kHz).
 * Realizuje 64-stopniowy PWM na pinach PB12..PB15, PA9, PA10. */
void leds_sw_pwm_tick(void);

#endif /* GREJEM_LEDS_H */
