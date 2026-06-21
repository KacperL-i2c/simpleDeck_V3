/* =============================================================================
 *  Test LED — tryby off/on/blink, dim/pulse/breathe/strobe/heartbeat,
 *  PWM levels, bounds check, blink non-blocking.
 * ============================================================================= */
#include "../include/leds.h"
#include "../include/config.h"
#include "../include/board.h"
#include "../include/timer.h"
#include <assert.h>
#include <stdio.h>

extern uint32_t mock_gpio_idr[4];   /* 0=A,1=B,2=C,3=D */
extern uint32_t mock_scheduler_ms;
extern uint8_t  mock_hw_brightness[LED_HW_PWM_COUNT];
extern const struct led_pin board_leds[];

static int failures = 0;
#define CHECK(expr, msg) do { \
    if (!(expr)) { printf("  ✗ %s (linia %d)\n", msg, __LINE__); failures++; } \
    else { printf("  ✓ %s\n", msg); } \
} while (0)

/* GPIOB (idx 1) PB10..PB14 = LEDy. Sprawdź czy LED `idx` świeci.
 * Dla HW PWM LEDów (0,1) sprawdza mock_hw_brightness; dla SW PWM (2,3,4)
 * sprawdza GPIO IDR (napędzane przez leds_sw_pwm_tick / led_apply_level). */
static int led_is_on(uint8_t idx) {
    if (idx < LED_HW_PWM_COUNT)
        return mock_hw_brightness[idx] > 0;
    uint16_t pin = board_leds[idx].pin;
    return (mock_gpio_idr[1] & pin) != 0;   /* active-high */
}

/* Zwróć aktualny SW PWM level diody (po konwersji na 0..64) */
static uint8_t sw_pwm_cycle_on_ratio(uint8_t idx) {
    /* Symuluj pełny cykl 64 kroków SW PWM i policz ile ticków LED jest ON */
    uint8_t on_count = 0;
    for (uint8_t step = 0; step < 64; step++) {
        leds_sw_pwm_tick();
        if (led_is_on(idx)) on_count++;
    }
    return on_count;
}

int main(void) {
    printf("=== test_leds ===\n");

    leds_init();
    /* Po init wszystkie LEDy OFF */
    int all_off = 1;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (led_is_on(i)) { all_off = 0; break; }
    }
    CHECK(all_off, "init: wszystkie LEDy OFF");

    /* === LED_MODE_ON === */
    led_set(0, LED_MODE_ON);
    CHECK(led_is_on(0), "LED 0 ON");
    CHECK(!led_is_on(1), "LED 1 nadal OFF");

    /* === LED_MODE_OFF === */
    led_set(0, LED_MODE_OFF);
    CHECK(!led_is_on(0), "LED 0 OFF po led_set(OFF)");

    /* === Bounds check: idx >= LED_COUNT = no-op (nie crash) === */
    led_set(99, LED_MODE_ON);   /* ignorowane */
    CHECK(1, "led_set(99,...) nie crashuje");

    /* === LED_MODE_BLINK === */
    mock_scheduler_ms = 1000;
    led_set(2, LED_MODE_BLINK);
    CHECK(led_is_on(2), "LED 2 BLINK start: świeci");

    /* leds_update przed okresem = bez zmian */
    mock_scheduler_ms = 1100;   /* 100ms < BLINK_PERIOD_MS=250 */
    leds_update();
    CHECK(led_is_on(2), "LED 2 BLINK: świeci przed okresem");

    /* Po okresie (250ms) = toggla */
    mock_scheduler_ms = 1300;   /* 300ms od 1000 > 250 */
    leds_update();
    CHECK(!led_is_on(2), "LED 2 BLINK: zgasł po okresie");

    /* Kolejny okres - znów świeci */
    mock_scheduler_ms = 1600;
    leds_update();
    CHECK(led_is_on(2), "LED 2 BLINK: świeci po kolejnym toggla");

    /* === Nieznany mode = OFF (bezpieczny fallback) === */
    led_set(3, 99);
    CHECK(!led_is_on(3), "nieznany mode=99 → OFF");

    /* ===========================================================
     * C10: Nowe tryby PWM (DIM, STROBE, HEARTBEAT, PULSE, BREATHE)
     * =========================================================== */

    /* === LED_MODE_DIM — HW PWM (LED 0 = PB10) === */
    mock_scheduler_ms = 2000;
    led_set_ext(0, LED_MODE_DIM, 128, 0, 0);
    CHECK(mock_hw_brightness[0] == 128, "LED 0 DIM 50%: HW brightness=128");

    led_set_ext(0, LED_MODE_DIM, 0, 0, 0);
    CHECK(mock_hw_brightness[0] == 0, "LED 0 DIM 0%: HW brightness=0");

    led_set_ext(0, LED_MODE_DIM, 255, 0, 0);
    CHECK(mock_hw_brightness[0] == 255, "LED 0 DIM 100%: HW brightness=255");

    /* === LED_MODE_DIM — SW PWM (LED 2 = PB12) === */
    mock_scheduler_ms = 2100;
    led_set_ext(2, LED_MODE_DIM, 255, 0, 0);
    {
        uint8_t on = sw_pwm_cycle_on_ratio(2);
        CHECK(on >= 60, "LED 2 DIM 100%: SW PWM ≥ 60/64 ticków ON");
    }

    led_set_ext(2, LED_MODE_DIM, 0, 0, 0);
    {
        uint8_t on = sw_pwm_cycle_on_ratio(2);
        CHECK(on == 0, "LED 2 DIM 0%: SW PWM 0 ticków ON");
    }

    led_set_ext(2, LED_MODE_DIM, 128, 0, 0);
    {
        uint8_t on = sw_pwm_cycle_on_ratio(2);
        CHECK(on >= 28 && on <= 36, "LED 2 DIM 50%: SW PWM ~32/64 ticków ON");
    }

    /* === LED_MODE_STROBE — szybkie miganie === */
    mock_scheduler_ms = 3000;
    led_set_ext(0, LED_MODE_STROBE, 255, 100, 30);  /* period=100ms, duty=30% */
    /* Na początku cyklu (phase=0) → ON */
    CHECK(mock_hw_brightness[0] == 255, "LED 0 STROBE: świeci na początku cyklu");
    /* Po 50ms (połowa okresu, > 30ms on-time) → OFF */
    mock_scheduler_ms = 3050;
    leds_update();
    CHECK(mock_hw_brightness[0] == 0, "LED 0 STROBE: zgasł po on-time (duty 30%)");

    /* === LED_MODE_HEARTBEAT — podwójny błysk === */
    mock_scheduler_ms = 4000;
    led_set_ext(0, LED_MODE_HEARTBEAT, 255, 1000, 0);  /* period=1000ms */
    /* phase=0 → pierwszy błysk → ON */
    CHECK(mock_hw_brightness[0] == 255, "LED 0 HEARTBEAT: pierwszy błysk ON");
    /* phase=125ms (1/8) → pauza między błyskami → OFF */
    mock_scheduler_ms = 4125;
    leds_update();
    CHECK(mock_hw_brightness[0] == 0, "LED 0 HEARTBEAT: pauza między błyskami");
    /* phase=250ms (2/8) → drugi błysk → ON */
    mock_scheduler_ms = 4250;
    leds_update();
    CHECK(mock_hw_brightness[0] == 255, "LED 0 HEARTBEAT: drugi błysk ON");
    /* phase=500ms (4/8) → długa pauza → OFF */
    mock_scheduler_ms = 4500;
    leds_update();
    CHECK(mock_hw_brightness[0] == 0, "LED 0 HEARTBEAT: długa pauza OFF");

    /* === LED_MODE_PULSE — trójkątny ramp === */
    mock_scheduler_ms = 5000;
    led_set_ext(0, LED_MODE_PULSE, 200, 1000, 0);  /* period=1000ms */
    /* phase=0 → level=0 (start ramp) */
    CHECK(mock_hw_brightness[0] == 0, "LED 0 PULSE: dno ramp (level=0)");
    /* phase=250ms (1/4 okresu) → level = 200*250/500 = 100 */
    mock_scheduler_ms = 5250;
    leds_update();
    CHECK(mock_hw_brightness[0] == 100, "LED 0 PULSE: 1/4 narastania (level~100)");
    /* phase=500ms (mid) → peak = brightness */
    mock_scheduler_ms = 5500;
    leds_update();
    CHECK(mock_hw_brightness[0] == 200, "LED 0 PULSE: szczyt (level=200)");
    /* phase=750ms (3/4) → opadanie = 200*250/500 = 100 */
    mock_scheduler_ms = 5750;
    leds_update();
    CHECK(mock_hw_brightness[0] == 100, "LED 0 PULSE: 3/4 opadania (level~100)");

    /* === LED_MODE_BREATHE — sinusoidalny fade === */
    mock_scheduler_ms = 6000;
    led_set_ext(0, LED_MODE_BREATHE, 255, 3000, 0);  /* period=3000ms */
    /* phase=0 → breathe_lut[0]=128 → 128*255/255=128 */
    CHECK(mock_hw_brightness[0] == 128, "LED 0 BREATHE: start (level~128)");
    /* phase = 3000/4 = 750ms → LUT index = 750*64/3000 = 16 → breathe_lut[16]=255 */
    mock_scheduler_ms = 6750;
    leds_update();
    CHECK(mock_hw_brightness[0] == 255, "LED 0 BREATHE: szczyt sinusoidy (level=255)");
    /* phase = 3000/2 = 1500ms → LUT index = 32 → breathe_lut[32]=128 */
    mock_scheduler_ms = 7500;
    leds_update();
    CHECK(mock_hw_brightness[0] == 128, "LED 0 BREATHE: środek (level~128)");
    /* phase = 3*3000/4 = 2250ms → LUT index = 48 → breathe_lut[48]=0 */
    mock_scheduler_ms = 8250;
    leds_update();
    CHECK(mock_hw_brightness[0] == 0, "LED 0 BREATHE: dno sinusoidy (level=0)");

    /* === led_set (legacy) == led_set_ext z domyślnymi === */
    mock_scheduler_ms = 9000;
    led_set(1, LED_MODE_ON);
    CHECK(mock_hw_brightness[1] == 255, "LED 1 legacy ON → brightness=255");

    led_set(1, LED_MODE_OFF);
    CHECK(mock_hw_brightness[1] == 0, "LED 1 legacy OFF → brightness=0");

    /* === Periodyczność BLINK (pełen cykl, period = half-cycle) === */
    mock_scheduler_ms = 10000;
    led_set(4, LED_MODE_BLINK);  /* default period 250ms = toggle interval */
    CHECK(led_is_on(4), "LED 4 BLINK start ON");
    mock_scheduler_ms = 10200;   /* 200ms < 250 → ON */
    leds_update();
    CHECK(led_is_on(4), "LED 4 BLINK 200ms: nadal ON");
    mock_scheduler_ms = 10300;   /* 300ms > 250 → OFF */
    leds_update();
    CHECK(!led_is_on(4), "LED 4 BLINK 300ms: OFF (druga połowa cyklu)");
    mock_scheduler_ms = 10500;   /* 500ms → cycle_pos=0 → ON (drugi cykl) */
    leds_update();
    CHECK(led_is_on(4), "LED 4 BLINK 500ms: ON (drugi cykl)");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
