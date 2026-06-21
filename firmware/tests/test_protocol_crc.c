/* =============================================================================
 *  Test CRC16-CCITT - wektory znane, weryfikacja zgodności z Python (CCITT-FALSE).
 *
 *  Spójność z desktop/src/grejem_os/transport/protocol.py::crc16_ccitt jest
 *  kluczowa - ramki kodowane Pythonem muszą dekodować się w C i odwrotnie.
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
    printf("=== test_protocol_crc ===\n");

    /* CCITT-FALSE wektory testowe:
     *   crc("") = 0xFFFF  (init value, no bytes)
     *   crc("123456789") = 0x29B1
     *   crc("A") = 0xB915  (ręcznie wyliczone) */
    CHECK(protocol_crc16((const uint8_t *)"", 0) == 0xFFFF,
          "crc('') == 0xFFFF (init value)");
    CHECK(protocol_crc16((const uint8_t *)"123456789", 9) == 0x29B1,
          "crc('123456789') == 0x29B1 (CCITT-FALSE standard)");
    CHECK(protocol_crc16((const uint8_t *)"A", 1) == 0xB915,
          "crc('A') == 0xB915 (ręcznie wyliczone)");

    /* Dłuższa sekwencja bajtów - weryfikacja brak ucięcia typu uint8_t w pętli */
    uint8_t data32[32];
    for (int i = 0; i < 32; i++) data32[i] = (uint8_t)(i * 7 + 3);
    uint16_t crc32 = protocol_crc16(data32, 32);
    printf("  ℹ crc(32 bytes pattern) = 0x%04X\n", crc32);
    /* Sanity: ten sam bufor z różnymi długościami daje różne CRC */
    uint16_t crc16_short = protocol_crc16(data32, 16);
    CHECK(crc32 != crc16_short, "różne długości → różne CRC");

    /* Brak zmiany input (CRC jest pure function) */
    uint8_t preserved[5] = {1, 2, 3, 4, 5};
    uint8_t copy[5];
    memcpy(copy, preserved, 5);
    (void)protocol_crc16(preserved, 5);
    CHECK(memcmp(preserved, copy, 5) == 0, "CRC nie modyfikuje input");

    /* Symetryczność: crc(a||b) zależy od całego ciągu, nie od samych bajtów */
    uint8_t ab[] = {0xAA, 0xBB};
    uint8_t ba[] = {0xBB, 0xAA};
    CHECK(protocol_crc16(ab, 2) != protocol_crc16(ba, 2),
          "kolejność bajtów ma znaczenie");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
