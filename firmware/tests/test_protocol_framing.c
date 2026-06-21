/* =============================================================================
 *  Test framing - encode/decode round-trip + edge cases (zgodność z Python).
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
    printf("=== test_protocol_framing ===\n");

    /* Round-trip: utwórz ramkę → pump_peek → sprawdź strukturalnie */
    uint8_t out[PROTO_REPORT_SIZE];

    /* Ramka: BUTTON_EVT ch=2 payload=1 (pressed) */
    protocol_emit_button(2, 1);
    CHECK(protocol_tx_pending() == 1, "tx_pending po emit_button");

    uint8_t used = protocol_pump(out);
    CHECK(used > 0, "pump zwrócił dane");

    /* SOF na pozycji 0 */
    CHECK(out[0] == PROTO_SOF, "SOF na pozycji 0");

    /* TYPE = BUTTON_EVT = 0x02 */
    CHECK(out[1] == PROTO_BUTTON_EVT, "TYPE = BUTTON_EVT (0x02)");

    /* CH = 2 */
    CHECK(out[2] == 2, "CH = 2");

    /* LEN = 1 (jeden bajt payload) */
    CHECK(out[3] == 1, "LEN = 1");

    /* PAYLOAD[0] = 1 (state=pressed) */
    CHECK(out[4] == 1, "PAYLOAD[0] = 1 (pressed)");

    /* CRC na pozycjach 5-6 (little endian) */
    uint8_t expected_body[] = {PROTO_BUTTON_EVT, 2, 1, 1};   /* TYPE+CH+LEN+payload */
    uint16_t expected_crc = protocol_crc16(expected_body, 4);
    CHECK(out[5] == (expected_crc & 0xFF), "CRC_LO zgodne");
    CHECK(out[6] == (expected_crc >> 8), "CRC_HI zgodne");

    /* Padding zerami po ramce (reszta = 0) */
    int padding_ok = 1;
    for (int i = 7; i < PROTO_REPORT_SIZE; i++) {
        if (out[i] != 0) { padding_ok = 0; break; }
    }
    CHECK(padding_ok, "padding zerami do 64 B");

    /* Po pump kolejka pusta */
    CHECK(protocol_tx_pending() == 0, "tx_pending = 0 po pump");

    /* Ramka HEARTBEAT z konkretnym uptime - weryfikacja byte order */
    protocol_emit_heartbeat(0x12345678);
    used = protocol_pump(out);
    CHECK(out[1] == PROTO_HEARTBEAT, "HEARTBEAT type");
    CHECK(out[3] == 5, "HEARTBEAT LEN = 5");
    CHECK(out[4] == 0x78, "uptime[0] = 0x78 (LE)");
    CHECK(out[5] == 0x56, "uptime[1] = 0x56 (LE)");
    CHECK(out[6] == 0x34, "uptime[2] = 0x34 (LE)");
    CHECK(out[7] == 0x12, "uptime[3] = 0x12 (LE)");

    /* POT_EVT z wartością 0x0FFF (max 12-bit) */
    protocol_emit_pot(3, 0x0FFF);
    used = protocol_pump(out);
    CHECK(out[1] == PROTO_POT_EVT, "POT_EVT type");
    CHECK(out[2] == 3, "CH = 3");
    CHECK(out[3] == 2, "LEN = 2");
    CHECK(out[4] == 0xFF && out[5] == 0x0F, "value = 0x0FFF (LE)");

    /* Pump z pustej kolejki = 0 */
    CHECK(protocol_pump(out) == 0, "pump z pustej kolejki = 0");

    /* Peek bez pop - ramka zostaje */
    protocol_emit_button(0, 0);
    used = protocol_pump_peek(out);
    CHECK(used > 0, "peek zwrócił dane");
    CHECK(protocol_tx_pending() == 1, "ramka NIE zdjęta po peek");
    protocol_pump_pop();
    CHECK(protocol_tx_pending() == 0, "ramka zdjęta po pop");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
