/* =============================================================================
 *  Test TX queue - FIFO, wrap-around, pełna kolejka, drop counter.
 *
 *  Weryfikuje fix C1 (sekcja krytyczna) i C4 (drop counter).
 * ============================================================================= */
#include "../include/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(expr, msg) do { \
    if (!(expr)) { printf("  ✗ %s (linia %d)\n", msg, __LINE__); failures++; } \
    else { printf("  ✓ %s\n", msg); } \
} while (0)

int main(void) {
    printf("=== test_protocol_queue ===\n");

    /* Pusta kolejka na start */
    CHECK(protocol_tx_pending() == 0, "początkowo pusta");

    /* Push TX_QUEUE_SIZE-1 = 31 ramek (zapas 1 dla head!=tail) */
    for (int i = 0; i < 31; i++) {
        protocol_emit_button((uint8_t)(i % 4), (uint8_t)(i & 1));
    }
    CHECK(protocol_tx_pending() == 1, "31 ramek w kolejce");

    /* 32. push powinien ZAWIEŚĆ (kolejka pełna) → drop_count rośnie */
    uint32_t drops_before = protocol_get_drops();
    /* Push kolejnej ramki - kolejka pełna (32-1=31 max, head==tail zakazane) */
    for (int i = 0; i < 5; i++) {
        protocol_emit_button(0, 1);   /* drop */
    }
    uint32_t drops_after = protocol_get_drops();
    CHECK(drops_after > drops_before, "drop_count rośnie gdy kolejka pełna");
    printf("  ℹ drops: %lu → %lu\n", (unsigned long)drops_before, (unsigned long)drops_after);

    /* FIFO - wyjmij wszystkie ramek i sprawdź kolejność */
    uint8_t out[PROTO_REPORT_SIZE];
    int first_btn_ch = -1, last_btn_ch = -1;
    int count = 0;
    while (protocol_tx_pending()) {
        uint8_t n = protocol_pump(out);
        if (n == 0) break;
        if (count == 0) first_btn_ch = out[2];   /* CH pierwszej ramki */
        last_btn_ch = out[2];
        count++;
    }
    CHECK(count == 31, "wyjęto dokładnie 31 ramek (FIFO)");
    printf("  ℹ first CH = %d, last CH = %d\n", first_btn_ch, last_btn_ch);
    /* Pierwsza ramka: i=0 → CH=0. Ostatnia i=30 → CH=30%4=2 */
    CHECK(first_btn_ch == 0, "FIFO: pierwsza ramka to ch=0");
    CHECK(last_btn_ch == 2, "FIFO: ostatnia ramka to ch=2 (30%4)");

    /* Pusta po wyjęciu wszystkich */
    CHECK(protocol_tx_pending() == 0, "pusta po drain");

    /* Wrap-around: push 5, pop 5, push 5 → indeksy wracają */
    for (int i = 0; i < 5; i++) protocol_emit_pot(0, (uint16_t)i);
    for (int i = 0; i < 5; i++) (void)protocol_pump(out);
    CHECK(protocol_tx_pending() == 0, "wrap-around: kolejka pusta po cyklu");
    /* Drugi cykl - dalej działa */
    protocol_emit_button(1, 1);
    CHECK(protocol_tx_pending() == 1, "drugi cykl po wrap-around");

    /* Drop counter resetuje w nowej sesji (zmienne statyczne) - tu wciąż narasta */
    /* Po prostu weryfikujemy że > 0 */
    CHECK(protocol_get_drops() > 0, "drop counter > 0 po testach");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
