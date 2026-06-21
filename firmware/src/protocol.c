/* =============================================================================
 *  GREJEM OS / protocol.c
 *
 *  Implementacja lekkiego protokołu binarnego MCU ↔ PC.
 *
 *  TX:   moduły (heartbeat, button, pot) wywołują protocol_emit_*() aby
 *        zakolejkować ramkę. Scheduler w usbhid_pump() woła protocol_pump()
 *        by sformatować kolejną ramkę jako 64-bajtowy raport HID i wysłać.
 *
 *  RX:   callback USB hid_out_callback() woła protocol_handle_out() gdy
 *        przyjdzie raport OUT (komenda od PC). Po walidacji CRC i długości
 *        komenda jest dispatchowana do modułu docelowego (led_set / cfg).
 *
 *  CRC:  CRC16-CCITT (poly 0x1021, init 0xFFFF) bez tablicy LUT.
 *
 *  THREAD-SAFETY (C1 fix):
 *    tx_push() jest wołane z DWÓCH kontekstów:
 *      - main loop:   buttons_poll → protocol_emit_button (superloop)
 *      - USB ISR:     hid_out_callback → protocol_handle_out → protocol_emit_ack
 *    Bez sekcji krytycznej dochodzi do race na tx_head → korupcja kolejki.
 *    Sekcja krytyczna irq_save/restore otacza całą operację push.
 * ============================================================================= */
#include "protocol.h"
#include "config.h"
#include "board.h"
#include "leds.h"
#include "adc.h"

#include <string.h>

/* ===========================================================================
 *  Sekcja krytyczna - inline asm dla Cortex-M3 (niezależny od CMSIS)
 *  Zwraca poprzedni stan PRIMASK; restore przywraca (nie włącza gdy były
 *  wyłączone). Działa na -O2 bez memory barrier issues dzięki "memory" clobber.
 *
 *  Na PC (testy jednostkowe z -DTEST) sekcja krytyczna jest no-op - testy są
 *  single-threaded więc race nie występuje.
 * =========================================================================== */
#ifndef TEST
static inline uint32_t irq_save(void) {
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    return primask;
}
static inline void irq_restore(uint32_t primask) {
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory");
}
#else
/* Tryb testowy na PC - brak IRQ więc sekcja krytyczna = no-op */
static inline uint32_t irq_save(void)   { return 0; }
static inline void irq_restore(uint32_t primask) { (void)primask; }
#endif

/* ===========================================================================
 *  CRC16-CCITT (poly 0x1021, init 0xFFFF), bez tablicy LUT - oszczędza Flash
 * =========================================================================== */
uint16_t protocol_crc16(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)( crc << 1);
        }
    }
    return crc;
}

/* ===========================================================================
 *  TX queue - prosty ring (FIFO) na TX_QUEUE_SIZE ramek
 *
 *  C1 fix: TX_QUEUE_SIZE zwiększone z 16 do 32 - dwukrotnie więcej bufora
 *  (= +576 B RAM, razem ~1.2 KB) co w praktyce eliminuje porzucanie ramek
 *  przy nagłym ruchu (5 potów + button + heartbeat w jednym ticku).
 * =========================================================================== */
typedef struct {
    uint8_t type;
    uint8_t ch;
    uint8_t len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} frame_t;

#define TX_QUEUE_SIZE   32          /* potęga 2 dla wydajności modulo */
#define TX_QUEUE_MASK   (TX_QUEUE_SIZE - 1)

static frame_t tx_queue[TX_QUEUE_SIZE];
static volatile uint8_t tx_head = 0;     /* write index (producent) */
static volatile uint8_t tx_tail = 0;     /* read index (konsument)  */

/* C4 fix: licznik porzuconych ramek (gdy kolejka pełna). Diagnostyka. */
static volatile uint32_t tx_drop_count = 0;

uint32_t protocol_get_drops(void) {
    return tx_drop_count;
}

/* Wypchnij ramkę do kolejki. Zwraca 1 jeśli się udało, 0 jeśli pełna.
 *
 * C1 fix: cała operacja otoczona sekcją krytyczną (irq_save/restore).
 * Bez tego race między main loop (emit_button) a USB ISR (emit_ack z
 * protocol_handle_out) psuje tx_head. */
static uint8_t tx_push(uint8_t type, uint8_t ch,
                       uint8_t len, const uint8_t *payload) {
    uint32_t primask = irq_save();

    uint8_t next = (tx_head + 1) & TX_QUEUE_MASK;
    if (next == tx_tail) {
        irq_restore(primask);
        return 0;                       /* kolejka pełna */
    }

    if (len > PROTO_MAX_PAYLOAD) len = PROTO_MAX_PAYLOAD;
    frame_t *f = &tx_queue[tx_head];
    f->type = type;
    f->ch   = ch;
    f->len  = len;
    if (len && payload) memcpy(f->payload, payload, len);

    tx_head = next;
    irq_restore(primask);
    return 1;
}

uint8_t protocol_tx_pending(void) {
    return tx_head != tx_tail;
}

/* Pobierz następną ramkę bez zdejmowania (peek). Zwraca 0 jeśli pusta.
 *
 * C6 fix: usbhid_pump() woła peek przed write_packet(). Jeśli write
 * zwróci 0 (EP zajęty), nie woła pop() → ramka nie jest tracona. */
uint8_t protocol_pump_peek(uint8_t *out_buf) {
    if (tx_head == tx_tail) return 0;

    frame_t *f = &tx_queue[tx_tail];
    memset(out_buf, 0, PROTO_REPORT_SIZE);

    uint8_t pos = 0;
    out_buf[pos++] = PROTO_SOF;
    out_buf[pos++] = f->type;
    out_buf[pos++] = f->ch;
    out_buf[pos++] = f->len;
    if (f->len) {
        memcpy(&out_buf[pos], f->payload, f->len);
        pos += f->len;
    }
    /* CRC liczone od TYPE do końca PAYLOAD (3 bajty nagłówka + payload) */
    uint16_t crc = protocol_crc16(&out_buf[1], 3 + f->len);
    out_buf[pos++] = (uint8_t)(crc & 0xFF);    /* LO */
    out_buf[pos++] = (uint8_t)(crc >> 8);      /* HI */

    return pos;
}

/* Zdejmij ramkę z kolejki (po udanym write_packet). */
void protocol_pump_pop(void) {
    if (tx_head == tx_tail) return;
    /* Tail jest modyfikowany tylko tutaj (single consumer = main thread),
     * więc nie trzeba sekcji krytycznej - ale dla formalnej poprawności: */
    uint32_t primask = irq_save();
    tx_tail = (tx_tail + 1) & TX_QUEUE_MASK;
    irq_restore(primask);
}

/* Kompatybilność wsteczna: dotychczasowy pump = peek + pop (gdy klient nie
 * potrzebuje rozdzielenia). Zachowane dla testów. */
uint8_t protocol_pump(uint8_t *out_buf) {
    uint8_t used = protocol_pump_peek(out_buf);
    if (used > 0) protocol_pump_pop();
    return used;
}

/* ===========================================================================
 *  Emitery TX (wołane z innych modułów)
 *
 *  C4 fix: każda zwraca 1 jeśli ramka zakolejkowana, 0 jeśli porzucona.
 * =========================================================================== */
void protocol_emit_heartbeat(uint32_t uptime_ms) {
    uint8_t p[5];
    p[0] = (uint8_t)(uptime_ms);
    p[1] = (uint8_t)(uptime_ms >> 8);
    p[2] = (uint8_t)(uptime_ms >> 16);
    p[3] = (uint8_t)(uptime_ms >> 24);
    /* Wersja spakowana w 1 bajcie: high nibble = major, low = minor */
    p[4] = (uint8_t)(((FW_VERSION_MAJOR & 0x0F) << 4) | (FW_VERSION_MINOR & 0x0F));
    if (!tx_push(PROTO_HEARTBEAT, 0, 5, p)) tx_drop_count++;
}

uint8_t protocol_emit_button(uint8_t idx, uint8_t state) {
    uint8_t ok = tx_push(PROTO_BUTTON_EVT, idx, 1, &state);
    if (!ok) tx_drop_count++;
    return ok;
}

void protocol_emit_pot(uint8_t idx, uint16_t value) {
    uint8_t p[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    if (!tx_push(PROTO_POT_EVT, idx, 2, p)) tx_drop_count++;
}

void protocol_emit_version(void) {
    uint8_t p[3] = { FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH };
    if (!tx_push(PROTO_VERSION, 0, 3, p)) tx_drop_count++;
}

void protocol_emit_ack(uint8_t acked_type) {
    if (!tx_push(PROTO_ACK, 0, 1, &acked_type)) tx_drop_count++;
}

void protocol_emit_nak(uint8_t err_code) {
    if (!tx_push(PROTO_NAK, 0, 1, &err_code)) tx_drop_count++;
}

/* ===========================================================================
 *  RX - obsługa raportu OUT od hosta
 * =========================================================================== */
void protocol_handle_out(const uint8_t *buf, uint8_t len) {
    /* Walidacja minimalnej długości ramki (SOF + TYPE + CH + LEN + 0 payload + 2 CRC) */
    if (len < 6 || buf == 0) {
        protocol_emit_nak(ERR_BAD_FRAME);
        return;
    }
    if (buf[0] != PROTO_SOF) {
        protocol_emit_nak(ERR_BAD_FRAME);
        return;
    }

    uint8_t type    = buf[1];
    uint8_t ch      = buf[2];
    uint8_t plen    = buf[3];
    if (plen > PROTO_MAX_PAYLOAD) {
        protocol_emit_nak(ERR_BAD_FRAME);
        return;
    }
    if ((uint8_t)(4 + plen + 2) > len) {
        protocol_emit_nak(ERR_BAD_FRAME);
        return;
    }

    /* Weryfikacja CRC: liczone od TYPE do końca PAYLOAD */
    uint16_t crc_calc = protocol_crc16(&buf[1], 3 + plen);
    uint16_t crc_recv = (uint16_t)buf[4 + plen] | ((uint16_t)buf[5 + plen] << 8);
    if (crc_calc != crc_recv) {
        protocol_emit_nak(ERR_BAD_CRC);
        return;
    }

    const uint8_t *p = &buf[4];

    /* Dispatch
     *
     * C3 fix: walidacja długości payloadu per-komenda. Wcześniej LED_CMD
     * z plen=0 czytało p[0] (=CRC_LO) jako tryb LED. Teraz wymagamy plen>=1
     * dla każdej komendy która potrzebuje payloadu. */
    switch (type) {
    case PROTO_LED_CMD:
        /* V3: Globalne tryby linijki LED.
         *   VU_BAR (9):   plen>=2, [mode, level], CH = kanał 0..4
         *   SOLID..BUTTONS (10..15): plen>=2, [mode, brightness, speed_lo, speed_hi, arg]
         *   MANUAL (16):  plen>=2, [mode, b0, b1, ..., bN]
         *   Legacy (0..7): NAK(ERR_BAD_TYPE) */
        if (plen < 2)        { protocol_emit_nak(ERR_BAD_FRAME);  return; }
        {
            uint8_t led_mode = p[0];
            uint8_t bright   = p[1];

            if (led_mode == LED_MODE_VU_BAR) {
                /* VU bar: CH = kanał, brightness = poziom 0..255 */
                if (ch >= POT_COUNT) { protocol_emit_nak(ERR_BAD_CHANNEL); return; }
                leds_set_vu(ch, bright);
            } else if (led_mode == LED_MODE_MANUAL) {
                /* Per-LED: [mode, b0, b1, ..., bN] */
                leds_set_manual(&p[1], plen - 1);
            } else if (led_mode >= LED_MODE_SOLID &&
                       led_mode <= LED_MODE_BUTTONS) {
                /* Tryby statyczne: [mode, brightness, speed_lo, speed_hi, arg] */
                uint16_t speed = 0;
                uint8_t  arg   = 0;
                if (plen >= 5) {
                    speed = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
                    arg   = p[4];
                }
                leds_set_mode(led_mode, bright, speed, arg);
            } else {
                /* Legacy (0..7) i nieznane tryby → NAK */
                protocol_emit_nak(ERR_BAD_TYPE);
                return;
            }
        }
        protocol_emit_ack(type);
        break;

    case PROTO_CFG_CMD:
        /* Payload 4 bajty: [deadband, alpha_slow, alpha_fast, send_thr].
         * fast_thr nie jest strojone z PC (pozostaje z config.h).
         * Wartości są aplikowane żywo do struktury tuningu adc.c (q.v. adc_cfg).
         * Wszystkie pola w skali 0..255. */
        if (plen < 4) { protocol_emit_nak(ERR_BAD_FRAME); return; }
        {
            adc_cfg_t *c = adc_cfg();
            c->deadband   = p[0];
            c->alpha_slow = p[1];
            c->alpha_fast = p[2];
            c->send_thr   = p[3];
        }
        protocol_emit_ack(type);
        break;

    case PROTO_GET_VERSION:
        /* Brak payloadu - poprawne. */
        protocol_emit_version();
        break;

    default:
        protocol_emit_nak(ERR_BAD_TYPE);
        break;
    }
}
