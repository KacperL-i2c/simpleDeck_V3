/* =============================================================================
 *  Test adaptacyjnego filtra EMA (Q8 bit-exact).
 *
 *  Weryfikuje logikę filtra: deadband, alfa adaptacyjny, send threshold.
 *
 *  Bufor DMA ma 5 kanałów × 16 próbek = 80 halfwords ułożonych kołowo:
 *    [ch0,ch1,ch2,ch3,ch4, ch0,ch1,...] (próbka 0, potem próbka 1, ...)
 *  Półka 0 (adc_consume_half(0)) = indeksy 0..39  (8 próbek per kanał)
 *  Półka 1 (adc_consume_half(1)) = indeksy 40..79 (8 próbek per kanał)
 *
 *  Indeks w buforze: dla półki `h`, próbki `s`, kanału `ch`:
 *    idx = h * 40 + s * 5 + ch
 * ============================================================================= */
#include "../include/adc.h"
#include "../include/config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void adc_consume_half(int half);

static int failures = 0;
#define CHECK(expr, msg) do { \
    if (!(expr)) { printf("  ✗ %s (linia %d)\n", msg, __LINE__); failures++; } \
    else { printf("  ✓ %s\n", msg); } \
} while (0)

/* Wypełnij półkę `half` bufora wartością `value` dla wszystkich 5 kanałów. */
static void fill_half(int half, uint16_t value) {
    for (int ch = 0; ch < 5; ch++) {
        for (int s = 0; s < 8; s++) {
            int idx = half * 40 + s * 5 + ch;
            adc_buffer[idx] = value;
        }
    }
}

int main(void) {
    printf("=== test_adc_filter ===\n");

    /* === Inicjalizacja EMA: pierwsza aktualizacja === */
    adc_start();
    memset(adc_buffer, 0, 80 * sizeof(uint16_t));
    fill_half(0, 1000);
    adc_consume_half(0);
    CHECK(adc_pot_filtered(0) == 1000, "init: ema = 1000");
    CHECK(adc_pot_dirty(0) == 1, "init: dirty=1");
    adc_flush_dirty();
    CHECK(adc_pot_dirty(0) == 0, "dirty=0 po flush");

    /* === Deadband: mała zmiana = ignorowana === */
    fill_half(1, 1003);   /* delta = 3 < DEADBAND=8 */
    adc_consume_half(1);
    CHECK(adc_pot_filtered(0) == 1000, "deadband: ema bez zmian (delta=3 < 8)");
    CHECK(adc_pot_dirty(0) == 0, "deadband: dirty=0 (zmiana zignorowana)");

    /* === Adaptacyjny alfa - powolny ruch (delta 20 < FAST_THR=128) === */
    fill_half(0, 1020);   /* delta = 20 */
    adc_consume_half(0);
    /* alfa = SLOW = 13/256 ≈ 0.05.
     * Q8: diff = (1020-1000)*256 = 5120, ema_x256 += 5120 * 13 / 256 = 260
     * ema_x256 = 256000 + 260 = 256260 → ema = 1001 */
    uint16_t ema_slow = adc_pot_filtered(0);
    printf("  ℹ slow (delta=20): ema = %d (oczekiwane 1001)\n", ema_slow);
    CHECK(ema_slow >= 1000 && ema_slow <= 1002, "slow alfa: ema ~1001");

    /* === Adaptacyjny alfa - szybki ruch (delta > FAST_THR=128) === */
    adc_start();
    memset(adc_buffer, 0, 80 * sizeof(uint16_t));
    fill_half(0, 1000);
    adc_consume_half(0);
    adc_flush_dirty();
    fill_half(1, 1500);   /* delta = 500 > FAST_THR=128 → alfa=FAST=205/256 */
    adc_consume_half(1);
    /* diff = (1500-1000)*256 = 128000, ema_x256 += 128000 * 205 / 256 = 102500
     * ema_x256 = 256000 + 102500 = 358500 → ema = 1400 */
    uint16_t ema_fast = adc_pot_filtered(0);
    printf("  ℹ fast (delta=500): ema = %d (oczekiwane ~1400)\n", ema_fast);
    CHECK(ema_fast >= 1395 && ema_fast <= 1405, "fast alfa: ema ~1400 (no-lag)");

    /* === Send threshold: zmiana ema < 16 = nie dirty === */
    adc_start();
    memset(adc_buffer, 0, 80 * sizeof(uint16_t));
    fill_half(0, 1000);
    adc_consume_half(0);
    adc_flush_dirty();   /* clear initial dirty */
    /* Drugi update: delta=20 (>deadband, <fast_thr). ema ruszy się o ~1 LSB.
     * |ema - last_sent| = 1 < SEND_THR=16 → dirty = 0. */
    fill_half(1, 1020);
    adc_consume_half(1);
    CHECK(adc_pot_dirty(0) == 0, "send_thr: mała zmiana ema → dirty=0");

    /* === Pełny zakres: 0 → 4095 === */
    adc_start();
    memset(adc_buffer, 0, 80 * sizeof(uint16_t));
    for (uint16_t v = 0; v <= 4095; v += 50) {
        fill_half(0, v);
        fill_half(1, v);
        adc_consume_half(0);
        adc_consume_half(1);
    }
    uint16_t final_ema = adc_pot_filtered(0);
    printf("  ℹ pełny zakres 0..4095: ema = %d\n", final_ema);
    CHECK(final_ema >= 4000, "pełny zakres: ema dochodzi do 4000+");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
