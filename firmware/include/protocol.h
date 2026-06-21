/* =============================================================================
 *  GREJEM OS / protocol.h
 *
 *  Lekki binarny protokół komunikacji MCU ↔ PC.
 *
 *  Ramka (przekazywana w jednym 64-bajtowym raporcie HID):
 *
 *    +------+------+------+-----+---------------------+--------+--------+
 *    | SOF  | TYPE | CH   | LEN | PAYLOAD[0..LEN-1]   | CRC_LO | CRC_HI |
 *    | 0xA5 | 1 B  | 1 B  | 1 B | do PROTO_MAX_PAYLOAD| 1 B    | 1 B    |
 *    +------+------+------+-----+---------------------+--------+--------+
 *
 *  SOF     : 0xA5  - sync byte (można użyć do synchronizacji gdyby CDC)
 *  TYPE    : kod komendy/zdarzenia (patrz enum poniżej)
 *  CH      : numer kanału (numer przycisku / potencjometru / LEDa, lub 0)
 *  LEN     : długość payloadu w bajtach (0..PROTO_MAX_PAYLOAD)
 *  PAYLOAD : dane zależne od TYPE
 *  CRC     : CRC16-CCITT (poly 0x1021, init 0xFFFF) liczone od TYPE..PAYLOAD
 *
 *  UWAGA dot. HID: konwencja "Report ID 0x00" - host zawsze dokłada 0x00
 *  jako pierwszy bajt bufora w hid_write() (po stronie PC). Ten bajt jest
 *  konsumowany przez stack USB hosta i NIE trafia do MCU. Na MCUEndpoint EP
 *  jedzie tylko 64-bajtowy payload (czyli dokładnie ramka powyżej).
 *
 *  Reszta 64-bajtowego raportu HID poza ramką jest zerowana (padding).
 * ============================================================================= */
#ifndef GREJEM_PROTOCOL_H
#define GREJEM_PROTOCOL_H

#include <stdint.h>

#define PROTO_SOF               0xA5u
#define PROTO_MAX_PAYLOAD       32u
#define PROTO_REPORT_SIZE       64u     /* pełny rozmiar raportu HID */

/* Typy ramek (TYPE) */
enum {
    /* --- MCU → PC (asynchroniczne zdarzenia) --- */
    PROTO_HEARTBEAT      = 0x01,   /* payload[5]: uptime(LE) + fw_version(1)
                                    * CH=0                                    */
    PROTO_BUTTON_EVT     = 0x02,   /* payload[1]: state(0=up, 1=down)
                                    * CH = numer przycisku 0..3               */
    PROTO_POT_EVT        = 0x03,   /* payload[2]: filtered_value(LE, 0..4095)
                                    * CH = numer potencjometru 0..4           */
    PROTO_VERSION        = 0x13,   /* payload[3]: major, minor, patch         */

    /* --- PC → MCU (komendy) --- */
    PROTO_LED_CMD        = 0x04,   /* V3: globalne tryby linijki LED.
                                      * VU_BAR (9): plen=2 [mode, level], CH=kanał.
                                      * SOLID..BUTTONS (10..15): plen≥2 [mode, bright,
                                      *   speed_lo, speed_hi, arg].
                                      * MANUAL (16): plen≥2 [mode, b0..bN].
                                      * Legacy (0..7) → NAK(ERR_BAD_TYPE). */
    PROTO_CFG_CMD        = 0x05,   /* payload[4]: deadband, slow, fast, thr
                                    * (runtime update filtra)                 */
    PROTO_GET_VERSION    = 0x12,   /* request: brak payloadu
                                    * MCU odpowiada PROTO_VERSION             */

    /* --- Control flow --- */
    PROTO_ACK            = 0x10,   /* payload[1]: typ obsłużonej komendy      */
    PROTO_NAK            = 0x11,   /* payload[1]: kod błędu                   */
    PROTO_ERROR          = 0xFF,   /* payload[1]: kod błędu (legacy)          */
};

/* Kody błędów dla PROTO_NAK */
enum {
    ERR_OK            = 0x00,
    ERR_BAD_CRC       = 0x09,
    ERR_BAD_FRAME     = 0x0A,
    ERR_BAD_TYPE      = 0x0C,
    ERR_BAD_CHANNEL   = 0x0D,
    ERR_OVERFLOW      = 0x0E,
};

/* Wersja firmware'u - zgłaszana komendą PROTO_GET_VERSION.
 * V3 (1.2.0): 3-LED multi-mode (Solid/Breathing/Chase/KnightRider/Strobe/
 *             Buttons/Manual + VU bar). ACTIVE_LED_COUNT=3. */
#define FW_VERSION_MAJOR   1
#define FW_VERSION_MINOR   2
#define FW_VERSION_PATCH   0

/* ---- CRC16-CCITT (poly 0x1021, init 0xFFFF), bez LUT (~20 B Flash) ---- */
uint16_t protocol_crc16(const uint8_t *data, uint8_t len);

/* ---- RX: obsługa raportu OUT od hosta (wywoływane przez USB callback) ----
 * Parsuje pełną ramkę, waliduje CRC i dispatchuje komendę. */
void protocol_handle_out(const uint8_t *buf, uint8_t len);

/* ---- TX: enqueue ramek do wysłania przez USB IN EP ----
 * Funkcje są bezpieczne do wywołania z dowolnego kontekstu (krótki sekcje
 * krytyczne). Kolejka to prosty ring TX_QUEUE_SIZE ramki. */
void protocol_emit_heartbeat(uint32_t uptime_ms);
uint8_t protocol_emit_button(uint8_t idx, uint8_t state);   /* C4: zwraca 1/0 */
void protocol_emit_pot(uint8_t idx, uint16_t value);
void protocol_emit_version(void);
void protocol_emit_ack(uint8_t acked_type);
void protocol_emit_nak(uint8_t err_code);

/* Zwraca 1 jeśli cokolwiek czeka w kolejce TX. */
uint8_t protocol_tx_pending(void);

/* Pobiera następną ramkę bez zdejmowania (peek). Formatuje w out_buf
 * (zawsze 64 B z padding 0). Zwraca długość użytej części (lub 0 jeśli pusta).
 *
 * C6 fix: rozdzielenie peek/pop pozwala usbhid_pump() na próbę write_packet
 * i zdjęcie ramki dopiero po potwierdzeniu sukcesu. */
uint8_t protocol_pump_peek(uint8_t *out_buf);

/* Zdejmuje ramkę z kolejki (po udanym write_packet). */
void protocol_pump_pop(void);

/* Kompatybilność wsteczna: peek + pop w jednym. */
uint8_t protocol_pump(uint8_t *out_buf);

/* C4 fix: licznik porzuconych ramek (diagnostyka). */
uint32_t protocol_get_drops(void);

#endif /* GREJEM_PROTOCOL_H */
