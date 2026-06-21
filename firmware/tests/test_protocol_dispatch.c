/* =============================================================================
 *  Test dispatch komend RX - weryfikacja fix C3 (plen per-komenda).
 *
 *  C3 (regresja): PROTO_LED_CMD z plen=0 NIE powinien czytać CRC jako mode.
 *  Naprawa: protocol_handle_out sprawdza plen per-komenda.
 * ============================================================================= */
#include "../include/protocol.h"
#include "../include/leds.h"
#include "../include/timer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint32_t mock_gpio_idr[4];   /* z mock_state.c - do weryfikacji LED stanu */
extern uint8_t  mock_hw_brightness[LED_HW_PWM_COUNT];

static int failures = 0;
#define CHECK(expr, msg) do { \
    if (!(expr)) { printf("  ✗ %s (linia %d)\n", msg, __LINE__); failures++; } \
    else { printf("  ✓ %s\n", msg); } \
} while (0)

/* Helper: zbuduj ramkę OUT i wyślij przez protocol_handle_out.
 * Ramka: SOF + TYPE + CH + LEN + PAYLOAD[LEN] + CRC16(LE) + padding.
 * Reszta bufora = 0. */
static void send_cmd(uint8_t *buf, uint8_t type, uint8_t ch,
                     uint8_t plen, const uint8_t *payload) {
    memset(buf, 0, PROTO_REPORT_SIZE);
    buf[0] = PROTO_SOF;
    buf[1] = type;
    buf[2] = ch;
    buf[3] = plen;
    if (plen && payload) memcpy(&buf[4], payload, plen);
    /* CRC od TYPE do końca PAYLOAD = 3 bajty nagłówka + payload */
    uint16_t crc = protocol_crc16(&buf[1], 3 + plen);
    buf[4 + plen] = (uint8_t)(crc & 0xFF);
    buf[5 + plen] = (uint8_t)(crc >> 8);
}

int main(void) {
    printf("=== test_protocol_dispatch ===\n");

    leds_init();
    uint8_t buf[PROTO_REPORT_SIZE];

    /* === TEST C3 (regresja): LED_CMD z plen=0 === */
    /* V2: plen < 2 → NAK(ERR_BAD_FRAME). */
    send_cmd(buf, PROTO_LED_CMD, 0, 0, NULL);
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    uint8_t out[PROTO_REPORT_SIZE];
    uint8_t n = protocol_pump(out);
    CHECK(n > 0, "LED_CMD plen=0: wyemitowano odpowiedź");
    CHECK(out[1] == PROTO_NAK, "LED_CMD plen=0 → NAK (regresja C3)");
    if (out[1] == PROTO_NAK) {
        printf("  ℹ err_code = 0x%02X (oczekiwane 0x0A = ERR_BAD_FRAME)\n", out[4]);
        CHECK(out[4] == ERR_BAD_FRAME, "NAK err_code = ERR_BAD_FRAME");
    }

    /* === V2 TEST: LED_CMD poprawny (VU_BAR, plen=2) === */
    {
        uint8_t vu[2] = { LED_MODE_VU_BAR, 128 };   /* 50% poziom */
        send_cmd(buf, PROTO_LED_CMD, 2, 2, vu);
        protocol_handle_out(buf, PROTO_REPORT_SIZE);
        n = protocol_pump(out);
        CHECK(n > 0, "LED_CMD VU_BAR plen=2: wyemitowano odpowiedź");
        CHECK(out[1] == PROTO_ACK, "LED_CMD VU_BAR → ACK");
    }

    /* === V2 TEST: LED_CMD plen=1 (za krótki dla V2) → NAK === */
    {
        uint8_t vu_short[1] = { LED_MODE_VU_BAR };
        send_cmd(buf, PROTO_LED_CMD, 0, 1, vu_short);
        protocol_handle_out(buf, PROTO_REPORT_SIZE);
        (void)protocol_pump(out);
        CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_FRAME,
              "LED_CMD plen=1 → NAK(ERR_BAD_FRAME) — V2 wymaga plen>=2");
    }

    /* === V2 TEST: LED_CMD z trybem legacy (DIM) → NAK(ERR_BAD_TYPE) === */
    {
        uint8_t legacy[2] = { LED_MODE_DIM, 128 };
        send_cmd(buf, PROTO_LED_CMD, 0, 2, legacy);
        protocol_handle_out(buf, PROTO_REPORT_SIZE);
        (void)protocol_pump(out);
        CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_TYPE,
              "LED_CMD mode=DIM (legacy) → NAK(ERR_BAD_TYPE)");
    }

    /* === V2 TEST: LED_CMD ch >= POT_COUNT → NAK(ERR_BAD_CHANNEL) === */
    {
        uint8_t vu[2] = { LED_MODE_VU_BAR, 255 };
        send_cmd(buf, PROTO_LED_CMD, 99, 2, vu);
        protocol_handle_out(buf, PROTO_REPORT_SIZE);
        (void)protocol_pump(out);
        CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_CHANNEL,
              "LED_CMD ch=99 → NAK(ERR_BAD_CHANNEL)");
    }

    /* === TEST: GET_VERSION === */
    send_cmd(buf, PROTO_GET_VERSION, 0, 0, NULL);
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    n = protocol_pump(out);
    CHECK(n > 0, "GET_VERSION: wyemitowano odpowiedź");
    CHECK(out[1] == PROTO_VERSION, "GET_VERSION → VERSION");
    CHECK(out[3] == 3, "VERSION LEN = 3");
    CHECK(out[4] == FW_VERSION_MAJOR, "VERSION major");
    CHECK(out[5] == FW_VERSION_MINOR, "VERSION minor");
    CHECK(out[6] == FW_VERSION_PATCH, "VERSION patch");

    /* === TEST: Nieznany TYPE === */
    send_cmd(buf, 0x42, 0, 0, NULL);
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    (void)protocol_pump(out);
    CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_TYPE,
          "Nieznany TYPE → NAK(ERR_BAD_TYPE)");

    /* === TEST: Zły SOF === */
    memset(buf, 0, PROTO_REPORT_SIZE);
    buf[0] = 0x00;   /* zły SOF */
    buf[1] = PROTO_LED_CMD;
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    (void)protocol_pump(out);
    CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_FRAME,
          "Zły SOF → NAK(ERR_BAD_FRAME)");

    /* === TEST: Uszkodzone CRC === */
    {
        uint8_t vu[2] = { LED_MODE_VU_BAR, 200 };
        send_cmd(buf, PROTO_LED_CMD, 0, 2, vu);
        buf[4 + 2] ^= 0xFF;   /* psujemy CRC_LO (offset = 4 + plen) */
        protocol_handle_out(buf, PROTO_REPORT_SIZE);
        (void)protocol_pump(out);
        CHECK(out[1] == PROTO_NAK && out[4] == ERR_BAD_CRC,
              "Uszkodzone CRC → NAK(ERR_BAD_CRC)");
    }

    /* === TEST: CFG_CMD poprawny (plen=4) === */
    uint8_t cfg[4] = {8, 13, 205, 16};
    send_cmd(buf, PROTO_CFG_CMD, 0, 4, cfg);
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    (void)protocol_pump(out);
    CHECK(out[1] == PROTO_ACK, "CFG_CMD plen=4 → ACK");

    /* === TEST: CFG_CMD z plen<4 (regresja C3) === */
    send_cmd(buf, PROTO_CFG_CMD, 0, 2, cfg);
    protocol_handle_out(buf, PROTO_REPORT_SIZE);
    (void)protocol_pump(out);
    CHECK(out[1] == PROTO_NAK, "CFG_CMD plen=2 → NAK (regresja C3)");

    if (failures == 0) {
        printf("\n=== ALL PASS ===\n");
        return 0;
    }
    printf("\n=== %d FAILURE(S) ===\n", failures);
    return 1;
}
