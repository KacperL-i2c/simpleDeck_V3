/* =============================================================================
 *  GREJEM OS / leds.c
 *
 *  V3: Wielomodowa linijka LED (3 aktywne diody: PB10, PB11, PB12).
 *
 *  Architektura PWM:
 *    PB10, PB11 → TIM2_CH3/CH4 hardware PWM (256 poziomów, ~1 kHz).
 *    PB12..PB15, PA9, PA10 → software PWM przez TIM3 ISR (64 poziomy, ~8 kHz).
 *
 *  V3 Tryby linijki (led_mode_global):
 *    LEGACY (8)     — per-LED przez led_set / led_set_ext (testy sterownika).
 *    VU_BAR (9)     — linijka proporcjonalna do poziomu (0..255), timeout 3 s.
 *    SOLID (10)     — wszystkie LED ciągle z jasnością.
 *    BREATHING (11) — wszystkie LED oddychają (sinusoida LUT).
 *    CHASE (12)     — jedna LED biegnie w przód (wrap po ACTIVE_LED_COUNT).
 *    KNIGHT_RIDER(13)— scanner: pozycja biegnie tam i z powrotem.
 *    STROBE_BAR(14) — wszystkie LED migają (duty cycle z arg).
 *    BUTTONS (15)   — każda LED = stan przycisku (0..2).
 *    MANUAL (16)    — indywidualna jasność per-LED (manual_levels[]).
 *
 *  Funkcje legacy (led_set / led_set_ext) i animacje (0..7) zachowane dla
 *  testów sterownika — ustawiają led_mode_global = LED_MODE_LEGACY.
 * ============================================================================= */
#include "leds.h"
#include "board.h"
#include "config.h"
#include "timer.h"
#include "scheduler.h"
#include "buttons.h"

#include <libopencm3/stm32/gpio.h>

#include <string.h>

/* ---- V2: Globalny stan linijki VU bar ---- */
static uint8_t  vu_level;             /* 0..255 docelowy poziom          */
static uint32_t vu_last_rx_ms;        /* timestamp ostatniego leds_set_vu */
static uint8_t  vu_active;            /* 1 = linijka renderowana (aktywna lub fading) */
static uint8_t  vu_fading;            /* 1 = trwa fade-out               */
static uint32_t vu_fade_start_ms;     /* timestamp początku fade          */
static uint8_t  vu_fade_start_level;  /* poziom na starcie fade           */
static uint8_t  vu_active_ch;         /* aktywny kanał 0..4 (przyszłość)  */

/* ---- V3: Globalny stan trybu linijki ---- */
static uint8_t  led_mode_global = LED_MODE_LEGACY;  /* aktualny tryb */
static uint8_t  led_brightness  = 255;               /* globalna jasność 0..255 */
static uint16_t led_speed_ms    = 1000;              /* okres animacji (ms) */
static uint8_t  led_arg         = 0;                 /* argument wzorca */
static uint32_t anim_start_ms   = 0;                 /* timestamp początku animacji */
static uint8_t  manual_levels[ACTIVE_LED_COUNT];     /* per-LED jasność dla MANUAL */

/* ---- Stan pojedynczej diody (legacy / PWM helper) ---- */
typedef struct {
    uint8_t  mode;            /* aktualny tryb (LED_MODE_*)                  */
    uint8_t  brightness;      /* docelowa jasność 0..255                     */
    uint16_t period_ms;       /* okres animacji                              */
    uint8_t  arg;             /* argument wzorca (duty cycle % itp.)         */
    uint8_t  sw_level;        /* SW PWM target 0..64 (dla pinów PB12..PA10)  */
    uint32_t anim_start;      /* timestamp początku animacji                 */
} led_state_t;

static led_state_t led_state[LED_COUNT];

/* ---- 64-entry sinusoida dla trybu BREATHE / BREATHING (0..255) ----
 * Wartość = (sin(2π·i/64) · 0.5 + 0.5) · 255, zaokrąglone.
 * Okres pełnego cyklu = 64 kroki. Pełna amplituda 0..255. */
static const uint8_t breathe_lut[64] = {
    128, 140, 153, 165, 177, 188, 199, 209,
    218, 226, 233, 240, 245, 250, 253, 254,
    255, 254, 253, 250, 245, 240, 233, 226,
    218, 209, 199, 188, 177, 165, 153, 140,
    128, 115, 103,  91,  79,  68,  57,  47,
     37,  28,  20,  13,   7,   3,   1,   0,
      0,   0,   1,   3,   7,  13,  20,  28,
     37,  47,  57,  68,  79,  91, 103, 115,
};

/* ---- Pomocnicze: fizyczny zapis pinu (active-high) ---- */
static inline void led_write(uint8_t idx, uint8_t on) {
    if (on) gpio_set  (board_leds[idx].port, board_leds[idx].pin);
    else    gpio_clear(board_leds[idx].port, board_leds[idx].pin);
}

/* ---- Aplikuj poziom jasności do HW lub SW PWM ---- */
static void led_apply_level(uint8_t idx, uint8_t level) {
    if (idx < LED_HW_PWM_COUNT) {
        /* PB10/PB11 — hardware PWM (TIM2) */
        timer_set_hw_brightness(idx, level);
    } else {
        /* PB12..PB15, PA9, PA10 — software PWM (TIM3 ISR), 64 poziomy */
        led_state[idx].sw_level = (uint8_t)((level * 64U + 127U) / 255U);
        /* Natychmiastowy zapis GPIO — ISR utrzyma PWM potem.
         * Bez tego pin nie reaguje aż do następnego ticka ISR (max 125 µs). */
        led_write(idx, led_state[idx].sw_level > 0 ? 1 : 0);
    }
}

/* ---- V3: Czyść nieużywane LEDy (indeksy ACTIVE_LED_COUNT..LED_COUNT-1) ---- */
static void leds_clear_unused(void) {
    for (uint8_t i = ACTIVE_LED_COUNT; i < LED_COUNT; i++)
        led_apply_level(i, 0);
}

/* ---- Domyślny okres dla danego trybu (gdy period_ms = 0) ---- */
static uint16_t default_period(uint8_t mode) {
    switch (mode) {
    case LED_MODE_STROBE:    return 100;    /* szybkie miganie */
    case LED_MODE_PULSE:     return 1000;   /* 1 s cykl */
    case LED_MODE_BREATHE:   return 3000;   /* 3 s cykl (wolne oddychanie) */
    case LED_MODE_HEARTBEAT: return 1000;   /* 1 s cykl podwójnego błysku */
    default:                 return CFG_LED_BLINK_PERIOD_MS;
    }
}

void leds_init(void) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        gpio_set_mode(board_leds[i].port,
                      GPIO_MODE_OUTPUT_2_MHZ,
                      GPIO_CNF_OUTPUT_PUSHPULL,
                      board_leds[i].pin);
        gpio_clear(board_leds[i].port, board_leds[i].pin);
    }
    memset(led_state, 0, sizeof(led_state));
    memset(manual_levels, 0, sizeof(manual_levels));
    /* Domyślna jasność = pełna dla wszystkich diod */
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        led_state[i].brightness = 255;
    }
    led_mode_global = LED_MODE_LEGACY;
}

/* ---- Oblicz aktualny poziom jasności dla animacji legacy ---- */
static uint8_t compute_level(uint8_t i, uint32_t now) {
    led_state_t *s = &led_state[i];
    uint8_t  mode   = s->mode;
    uint8_t  bright = s->brightness;
    uint16_t period = s->period_ms;
    if (period < 50) period = 50;

    uint32_t elapsed = now - s->anim_start;
    uint32_t phase   = elapsed % period;

    switch (mode) {
    case LED_MODE_OFF:
        return 0;

    case LED_MODE_ON:
    case LED_MODE_DIM:
        return bright;

    case LED_MODE_BLINK: {
        uint32_t cycle_pos = elapsed % ((uint32_t)period * 2);
        return (cycle_pos < period) ? bright : 0;
    }

    case LED_MODE_STROBE: {
        uint8_t duty = s->arg ? s->arg : 30;
        uint32_t on_time = ((uint32_t)period * duty) / 100;
        return (phase < on_time) ? bright : 0;
    }

    case LED_MODE_HEARTBEAT: {
        uint32_t q = period / 8;
        if (phase < q || (phase >= q * 2 && phase < q * 3))
            return bright;
        return 0;
    }

    case LED_MODE_PULSE: {
        uint32_t half = period / 2;
        if (phase < half)
            return (uint8_t)((bright * phase) / half);
        return (uint8_t)((bright * (period - phase)) / half);
    }

    case LED_MODE_BREATHE: {
        uint8_t idx = (uint8_t)((phase * 64U) / period);
        return (uint8_t)(((uint16_t)breathe_lut[idx] * bright) / 255U);
    }

    default:
        return 0;
    }
}

/* ===========================================================================
 *  V3: VU bar — segmentowana linijka + płynny top LED (ACTIVE_LED_COUNT LED)
 * ===========================================================================
 *  level 0..255 → ACTIVE_LED_COUNT wartości jasności.
 *  Każdy segment "posiada" 256/ACTIVE_LED_COUNT jednostki.
 *  LED poniżej segmentu = pełny (255). LED na granicy = płynny (PWM). Powyżej = 0.
 *
 *  Dla 3 LED (seg=85): level=0 → wszystkie off, level=85 → LED0 pełny,
 *  level=127 → LED0 pełny + LED1 ~50%, level=255 → wszystkie pełne. */
static void compute_bar(uint8_t level, uint8_t out[]) {
    uint16_t seg = 256U / ACTIVE_LED_COUNT;
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++) {
        uint16_t lower = (uint16_t)i * seg;
        uint16_t upper = lower + seg;
        if (level >= upper) {
            out[i] = 255;                                    /* pełny segment */
        } else if (level <= lower) {
            out[i] = 0;                                      /* zgaszony */
        } else {
            out[i] = (uint8_t)((uint16_t)(level - lower) * 255U / (seg - 1));
        }
    }
}

void leds_set_vu(uint8_t ch, uint8_t level) {
    /* V3: Przełącz na tryb VU_BAR jeśli nie jest aktywny */
    if (led_mode_global != LED_MODE_VU_BAR) {
        led_mode_global = LED_MODE_VU_BAR;
        leds_clear_unused();
    }
    vu_level       = level;
    vu_last_rx_ms  = scheduler_millis();
    vu_active      = 1;
    vu_fading      = 0;
    vu_active_ch   = ch;
}

/* ---- V3: Ustaw globalny tryb linijki ---- */
void leds_set_mode(uint8_t mode, uint8_t brightness, uint16_t speed_ms, uint8_t arg) {
    led_mode_global = mode;
    led_brightness  = brightness;
    led_speed_ms    = speed_ms;
    led_arg         = arg;
    anim_start_ms   = scheduler_millis();
    vu_active       = 0;
    vu_fading       = 0;
    leds_clear_unused();
}

/* ---- V3: Ustaw ręczną jasność per-LED ---- */
void leds_set_manual(const uint8_t *levels, uint8_t count) {
    led_mode_global = LED_MODE_MANUAL;
    anim_start_ms   = scheduler_millis();
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT && i < count; i++)
        manual_levels[i] = levels[i];
    leds_clear_unused();
}

/* ---- V3: Zwraca aktualny tryb ---- */
uint8_t leds_get_mode(void) {
    return led_mode_global;
}

void led_set_ext(uint8_t idx, uint8_t mode, uint8_t brightness,
                 uint16_t period_ms, uint8_t arg) {
    if (idx >= LED_COUNT) return;

    /* V3: Przełącz na tryb legacy */
    led_mode_global = LED_MODE_LEGACY;

    led_state_t *s = &led_state[idx];
    s->mode       = mode;
    s->brightness = brightness;
    s->period_ms  = (period_ms == 0) ? default_period(mode) : period_ms;
    if (s->period_ms < 50) s->period_ms = 50;
    s->arg        = arg;
    s->anim_start = scheduler_millis();

    /* Natychmiastowo zastosuj poziom */
    uint8_t level = compute_level(idx, scheduler_millis());
    led_apply_level(idx, level);
}

void led_set(uint8_t idx, uint8_t mode) {
    /* Legacy API: domyślna jasność 255, domyślny okres, brak arg */
    led_set_ext(idx, mode, 255, 0, 0);
}

/* ===========================================================================
 *  V3: Renderuj animacje globalnego trybu linijki
 * ===========================================================================
 *  Static helpers dla trybów animacji.
 * ============================================================================= */

/* ---- Renderuj VU bar z timeout/fade ---- */
static void render_vu_bar(uint32_t now) {
    uint8_t current_level = vu_level;

    if (vu_fading) {
        uint32_t elapsed = now - vu_fade_start_ms;
        if (elapsed >= CFG_VU_FADE_MS) {
            vu_active = 0;
            vu_fading = 0;
            vu_level  = 0;
            for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
                led_apply_level(i, 0);
            return;
        }
        current_level = (uint8_t)((uint16_t)vu_fade_start_level
                        * (CFG_VU_FADE_MS - elapsed) / CFG_VU_FADE_MS);
    } else if ((now - vu_last_rx_ms) >= CFG_VU_TIMEOUT_MS) {
        vu_fading = 1;
        vu_fade_start_ms = now;
        vu_fade_start_level = vu_level;
        current_level = vu_level;
    }

    uint8_t bar[ACTIVE_LED_COUNT];
    compute_bar(current_level, bar);
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        led_apply_level(i, bar[i]);
}

/* ---- Renderuj tryb BREATHING (sinusoida na wszystkich LED) ---- */
static void render_breathing(uint32_t now) {
    uint32_t elapsed = now - anim_start_ms;
    uint16_t period = led_speed_ms ? led_speed_ms : 3000;
    if (period < 50) period = 50;
    uint32_t phase = elapsed % period;
    uint8_t idx = (uint8_t)((phase * 64U) / period);
    uint8_t level = (uint8_t)(((uint16_t)breathe_lut[idx] * led_brightness) / 255U);
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        led_apply_level(i, level);
}

/* ---- Renderuj tryb CHASE (jedna LED biegnie w przód, wrap) ---- */
static void render_chase(uint32_t now) {
    uint32_t elapsed = now - anim_start_ms;
    uint16_t period = led_speed_ms ? led_speed_ms : 450;
    if (period < 50) period = 50;
    /* Czas na każdą pozycję = period / ACTIVE_LED_COUNT */
    uint32_t step_time = period / ACTIVE_LED_COUNT;
    if (step_time == 0) step_time = 1;
    uint8_t active = (uint8_t)((elapsed / step_time) % ACTIVE_LED_COUNT);
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        led_apply_level(i, (i == active) ? led_brightness : 0);
}

/* ---- Renderuj tryb KNIGHT_RIDER (scanner tam i z powrotem) ---- */
static void render_knight_rider(uint32_t now) {
    uint32_t elapsed = now - anim_start_ms;
    uint16_t period = led_speed_ms ? led_speed_ms : 600;
    if (period < 50) period = 50;
    uint32_t phase = elapsed % period;
    uint32_t half = period / 2;
    if (half == 0) half = 1;
    /* Fala trójkątna: 0 → N-1 → 0 → N-1 → ... */
    uint8_t pos;
    if (ACTIVE_LED_COUNT <= 1) {
        pos = 0;
    } else if (phase < half) {
        pos = (uint8_t)((phase * (ACTIVE_LED_COUNT - 1)) / half);
    } else {
        pos = (uint8_t)(((period - phase) * (ACTIVE_LED_COUNT - 1)) / half);
    }
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        led_apply_level(i, (i == pos) ? led_brightness : 0);
}

/* ---- Renderuj tryb STROBE_BAR (wszystkie LED migają) ---- */
static void render_strobe_bar(uint32_t now) {
    uint32_t elapsed = now - anim_start_ms;
    uint16_t period = led_speed_ms ? led_speed_ms : 120;
    if (period < 20) period = 20;
    uint32_t phase = elapsed % period;
    uint8_t duty = led_arg ? led_arg : 50;
    uint32_t on_time = ((uint32_t)period * duty) / 100;
    uint8_t level = (phase < on_time) ? led_brightness : 0;
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        led_apply_level(i, level);
}

/* ---- Renderuj tryb BUTTONS (LED = stan przycisku) ---- */
static void render_buttons(void) {
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++) {
        uint8_t pressed = button_debounced_state(i);
        led_apply_level(i, pressed ? led_brightness : 0);
    }
}

/* ---- Aktualizacja — scheduler 50 Hz (co 20 ms) ---- */
void leds_update(void) {
    uint32_t now = scheduler_millis();

    switch (led_mode_global) {

    case LED_MODE_LEGACY:
        /* Per-LED (testy sterownika / bezpośrednie led_set_ext).
         * Renderuj tylko LEDy z aktywnym trybem animacji. */
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            uint8_t mode = led_state[i].mode;
            if (mode == LED_MODE_OFF || mode == LED_MODE_ON || mode == LED_MODE_DIM)
                continue;
            uint8_t level = compute_level(i, now);
            led_apply_level(i, level);
        }
        break;

    case LED_MODE_OFF:
        for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
            led_apply_level(i, 0);
        break;

    case LED_MODE_VU_BAR:
        if (vu_active)
            render_vu_bar(now);
        else {
            for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
                led_apply_level(i, 0);
        }
        break;

    case LED_MODE_SOLID:
        for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
            led_apply_level(i, led_brightness);
        break;

    case LED_MODE_BREATHING:
        render_breathing(now);
        break;

    case LED_MODE_CHASE:
        render_chase(now);
        break;

    case LED_MODE_KNIGHT_RIDER:
        render_knight_rider(now);
        break;

    case LED_MODE_STROBE_BAR:
        render_strobe_bar(now);
        break;

    case LED_MODE_BUTTONS:
        render_buttons();
        break;

    case LED_MODE_MANUAL:
        for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
            led_apply_level(i, manual_levels[i]);
        break;

    default:
        for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
            led_apply_level(i, 0);
        break;
    }
}

/* ---- Software PWM tick — TIM3 ISR (~8 kHz) ----
 * Licznik PWM 0..63 (64 kroki). Dla każdej diody na pinach PB12..PB15, PA9, PA10
 * sprawdza czy licznik < sw_level → pin HIGH, inaczej → LOW.
 * Odczyt sw_level jest atomowy (1 bajt na Cortex-M3). */
void leds_sw_pwm_tick(void) {
    static uint8_t pwm_counter = 0;
    pwm_counter = (pwm_counter + 1) & 0x3F;     /* 0..63, wrap */

    for (uint8_t i = LED_HW_PWM_COUNT; i < LED_COUNT; i++) {
        uint8_t on = (pwm_counter < led_state[i].sw_level) ? 1 : 0;
        led_write(i, on);
    }
}
