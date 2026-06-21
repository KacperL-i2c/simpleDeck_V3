/* =============================================================================
 *  Test VU bar — matematyka linijki (compute_bar), timeout 3 s, fade 300 ms.
 *
 *  V3: 3-LED linijka VU (ACTIVE_LED_COUNT=3, seg=85). Testuje:
 *    - compute_bar na granicach segmentów (0, 85, 170, 255)
 *    - płynny top LED (wartości pomiędzy granicami)
 *    - timeout: 3 s bezczynności → fade
 *    - fade: 300 ms liniowy ramp → SLEEP (zgaszone)
 *    - reset timer przy nowym leds_set_vu
 * ============================================================================= */
#include "../include/leds.h"
#include "../include/config.h"
#include "../include/board.h"
#include "../include/timer.h"
#include <assert.h>
#include <stdio.h>

extern uint32_t mock_gpio_idr[4];   /* 0=A, 1=B, 2=C, 3=D */
extern uint32_t mock_scheduler_ms;
extern uint8_t  mock_hw_brightness[LED_HW_PWM_COUNT];
extern const struct led_pin board_leds[];

static int failures = 0;
#define CHECK(expr, msg) do { \
    if (!(expr)) { printf("  ✗ %s (linia %d)\n", msg, __LINE__); failures++; } \
    else { printf("  ✓ %s\n", msg); } \
} while (0)

/* Sprawdź czy LED `idx` świeci (HW PWM lub SW PWM przez GPIO). */
static int led_is_on(uint8_t idx) {
    if (idx < LED_HW_PWM_COUNT)
        return mock_hw_brightness[idx] > 0;
    uint16_t pin = board_leds[idx].pin;
    uint32_t port_idx = (board_leds[idx].port - GPIOA) / 0x400;
    return (mock_gpio_idr[port_idx] & pin) != 0;
}

/* Policz ile z aktywne LEDów świeci (jakikolwiek poziom > 0). */
static int count_lit(void) {
    int n = 0;
    for (uint8_t i = 0; i < ACTIVE_LED_COUNT; i++)
        if (led_is_on(i)) n++;
    return n;
}

/* Sprawdź HW PWM brightness konkretnego LEDa. */
static uint8_t hw_level(uint8_t idx) {
    if (idx < LED_HW_PWM_COUNT)
        return mock_hw_brightness[idx];
    return 0;
}

int main(void) {
    printf("=== test_vu_bar (V3: %d LED) ===\n", ACTIVE_LED_COUNT);

    leds_init();

    /* === Po init: VU nieaktywny, wszystkie LEDy OFF === */
    CHECK(count_lit() == 0, "init: wszystkie LEDy OFF");

    /* === leds_set_vu(0, 0) → żadna dioda === */
    mock_scheduler_ms = 100;
    leds_set_vu(0, 0);
    leds_update();
    CHECK(count_lit() == 0, "VU level=0: 0 LEDów");

    /* === leds_set_vu(0, 255) → wszystkie 3 === */
    mock_scheduler_ms = 200;
    leds_set_vu(0, 255);
    leds_update();
    CHECK(count_lit() == ACTIVE_LED_COUNT, "VU level=255: wszystkie LEDy");

    /* === leds_set_vu(0, 128) → ~2 (LED0 pełny, LED1 ~53%) === */
    mock_scheduler_ms = 300;
    leds_set_vu(0, 128);
    leds_update();
    int lit_128 = count_lit();
    CHECK(lit_128 >= 1 && lit_128 <= 2, "VU level=128: 1-2 LEDy");

    /* === Granica segmentu: level=85 → LED0 pełny (seg=85) === */
    mock_scheduler_ms = 400;
    leds_set_vu(0, 85);
    leds_update();
    CHECK(hw_level(0) == 255, "VU level=85: LED0 pełny (granica segmentu)");
    CHECK(!led_is_on(1), "VU level=85: LED1 OFF");

    /* === Granica segmentu: level=170 → LED0+1 pełne === */
    mock_scheduler_ms = 500;
    leds_set_vu(0, 170);
    leds_update();
    CHECK(hw_level(0) == 255, "VU level=170: LED0 pełny");
    CHECK(hw_level(1) == 255, "VU level=170: LED1 pełny (HW PWM)");
    CHECK(!led_is_on(2), "VU level=170: LED2 OFF (granica)");

    /* === Płynny top LED: level=42 → LED0 ~50% (42*255/84≈127) === */
    mock_scheduler_ms = 600;
    leds_set_vu(0, 42);
    leds_update();
    CHECK(hw_level(0) >= 120 && hw_level(0) <= 135,
          "VU level=42: LED0 ~50% (płynny)");

    /* === Timeout: 3 s bezczynności → fade === */
    mock_scheduler_ms = 1000;
    leds_set_vu(0, 200);
    leds_update();
    CHECK(count_lit() > 0, "VU active przed timeout: LEDy świecą");

    /* T = 1000 + 2999 → jeszcze przed timeout, świeci */
    mock_scheduler_ms = 3999;
    leds_update();
    CHECK(count_lit() > 0, "VU @2999ms: nadal świeci (przed timeout)");

    /* T = 1000 + 3000 → timeout → fade start */
    mock_scheduler_ms = 4000;
    leds_update();
    CHECK(count_lit() > 0, "VU @3000ms: fade rozpoczęty, LEDy jeszcze świecą");

    /* T = 1000 + 3000 + 300 → fade zakończony → SLEEP */
    mock_scheduler_ms = 4300;
    leds_update();
    CHECK(count_lit() == 0, "VU @3300ms: fade zakończony, wszystkie OFF (SLEEP)");

    /* === Reset timer: nowy leds_set_vu po timeout animuje ponownie === */
    mock_scheduler_ms = 5000;
    leds_set_vu(1, 100);
    leds_update();
    CHECK(count_lit() > 0, "VU reset po SLEEP: LEDy świecą ponownie");

    /* === Fade anulowany przez nowy leds_set_vu === */
    mock_scheduler_ms = 6000;
    leds_set_vu(2, 200);
    leds_update();
    mock_scheduler_ms = 9000;       /* timeout → fade start */
    leds_update();
    mock_scheduler_ms = 9100;       /* w trakcie fade */
    leds_update();
    leds_set_vu(3, 255);            /* nowa aktywność → anuluj fade */
    leds_update();
    CHECK(count_lit() == ACTIVE_LED_COUNT, "VU: nowy set anuluje fade → pełna linijka");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
