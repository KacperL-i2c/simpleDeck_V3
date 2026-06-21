/* =============================================================================
 *  GREJEM OS / adc.c
 *
 *  ADC1 + DMA1 Channel 1, ciągły skan 5 kanałów (PA0..PA4 = ADC1_IN0..IN4),
 *  bufor kołowy POT_COUNT × CFG_ADC_OVERSAMPLE halfwords.
 *  Przerwania DMA HT (half-transfer) i TC (transfer-complete) wywołują
 *  uśrednienie okna (8 próbek na kanał) i uruchomienie filtra adaptacyjnego.
 *
 *  Filtr: EMA (Exponential Moving Average) z dynamicznym współczynnikiem alfa
 *  i strefą martwą (Deadband). Wszystko w arytmetyce stałopozycyjnej ×256 (Q8),
 *  bo Cortex-M3 nie ma FPU.
 *
 *    err = raw - ema
 *    if |err| < DEADBAND              → szum, return (nie aktualizuj)
 *    alpha = (|err| > FAST_THR) ? ALPHA_FAST : ALPHA_SLOW
 *    ema += alpha × (raw - ema)
 *    if |ema - last_sent| >= SEND_THR → dirty = 1 (scheduler wyśle)
 *
 *  Właściwości:
 *   - Szum kwantyzacji i mikro-drgania potencjometru są odfiltrowane (deadband)
 *   - Powolne ruchy są gładkie (alfa=0.05)
 *   - Gwałtowne ruchy reagują natychmiast (alfa=0.80, zerowe opóźnienie)
 *   - CPU jest asystowane przez DMA - tylko krótki ISR filtruje dane
 * ============================================================================= */
#include "adc.h"
#include "config.h"
#include "board.h"
#include "protocol.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>

#include <string.h>

/* ---- Bufor DMA (linked bezpośrednio do DMA1 Ch1) ----
 * Kołowy, 5 kanałów × 16 próbek = 80 halfwords = 160 B RAM.
 * volatile: modyfikowany przez DMA za plecami CPU.
 *
 * Pod TEST: brak volatile/static żeby testy PC mogły bezpośrednio wypełniać. */
#ifdef TEST
uint16_t adc_buffer[POT_COUNT * CFG_ADC_OVERSAMPLE];
#else
static volatile uint16_t adc_buffer[POT_COUNT * CFG_ADC_OVERSAMPLE];
#endif

/* ---- Stan filtra dla każdego potencjometru ---- */
typedef struct {
    uint16_t raw_last;          /* ostatnia uśredniona wartość surowa       */
    int32_t  ema_x256;          /* EMA ×256 (Q8)                            */
    int32_t  last_sent_x256;    /* ostatnia wysłana wartość ×256            */
    uint8_t  dirty;             /* 1 = czeka wyemitowanie przez scheduler   */
} pot_state_t;

static pot_state_t pots[POT_COUNT];

/* Pierwsze przejście filtra inicjalizuje EMA bez opóźnienia startowego. */
static uint8_t ema_initialized = 0;

/* ---- Runtime tuning filtra (domyślnie = stałe z config.h) ----
 * Modyfikowane przez CFG_CMD z PC (patrz protocol.c). Pozwala dostrajać
 * filtr bez rekompilacji/reflash'a... no, wymaga tej wersji firmware'u,
 * ale potem tuning jest żywy. */
static adc_cfg_t g_cfg = {
    CFG_DEADBAND,
    CFG_FAST_THR,
    CFG_ALPHA_SLOW_X256,
    CFG_ALPHA_FAST_X256,
    CFG_SEND_THR,
};

adc_cfg_t *adc_cfg(void) { return &g_cfg; }

/* ===========================================================================
 *  Konfiguracja ADC1 + DMA1 Channel 1
 * =========================================================================== */
void adc_start(void) {
    memset(pots, 0, sizeof(pots));
    ema_initialized = 0;   /* C7 fix: reset dla powtórnego adc_start() */

    /* Piny PA0..PA4 jako analogowe (bez pull-up/down, cyfrowy bufor OFF) */
    gpio_set_mode(POT_PORT,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_ANALOG,
                  POT_PIN_MASK);

    /* --- DMA1 Channel 1 (ADC1 jest hard-wired do DMA1 Ch1) --- */
    dma_channel_reset(DMA1, DMA_CHANNEL1);

    dma_set_peripheral_address(DMA1, DMA_CHANNEL1, (uint32_t)&ADC_DR(ADC1));
    dma_set_memory_address     (DMA1, DMA_CHANNEL1, (uint32_t)adc_buffer);
    dma_set_number_of_data     (DMA1, DMA_CHANNEL1,
                                POT_COUNT * CFG_ADC_OVERSAMPLE);

    dma_set_read_from_peripheral(DMA1, DMA_CHANNEL1);   /* P2M */
    dma_enable_memory_increment_mode   (DMA1, DMA_CHANNEL1);
    dma_disable_peripheral_increment_mode(DMA1, DMA_CHANNEL1);
    dma_set_peripheral_size(DMA1, DMA_CHANNEL1, DMA_CCR_PSIZE_16BIT);
    dma_set_memory_size    (DMA1, DMA_CHANNEL1, DMA_CCR_MSIZE_16BIT);
    dma_enable_circular_mode(DMA1, DMA_CHANNEL1);       /* kołowo */

    /* Przerwania: na połowie bufora (HT) i na końcu (TC) */
    dma_enable_half_transfer_interrupt  (DMA1, DMA_CHANNEL1);
    dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL1);

    nvic_set_priority(NVIC_DMA1_CHANNEL1_IRQ, 1);  /* wyższy priorytet niż USB */
    nvic_enable_irq(NVIC_DMA1_CHANNEL1_IRQ);

    dma_enable_channel(DMA1, DMA_CHANNEL1);

    /* --- ADC1 --- */
    adc_power_off(ADC1);

    /* Kolejność skanowania: 5 kanałów ADC1_IN0..IN4 */
    static const uint8_t channel_list[POT_COUNT] = { POT_ADC_CHANNEL_LIST };
    adc_set_regular_sequence(ADC1, POT_COUNT, (uint8_t *)channel_list);

    /* Czas próbkowania = 239.5 cyklu (najbardziej stabilny, ignoruje Rs source) */
    adc_set_sample_time_on_all_channels(ADC1, CFG_ADC_SAMPLE_TIME);

    /* Tryby: SCAN (przejedź całą sekwencję) + CONT (ciągły) + DMA */
    adc_enable_scan_mode(ADC1);
    adc_set_continuous_conversion_mode(ADC1);
    adc_enable_dma(ADC1);

    /* Włącz i skalibruj (jednorazowe opóźnienie - tylko podczas startu).
     * C7 fix: usunięto podwójne adc_calibrate() - drugi przebieg niczego nie
     * weryfikował, tylko nadpisywał kalibrację (kolejne ~14 µs boot). */
    adc_power_on(ADC1);
    for (volatile uint32_t i = 0; i < 1000; i++) __asm__("nop");  /* ~14 µs */
    adc_reset_calibration(ADC1);
    adc_calibrate(ADC1);

    /* Pierwszy trigger - DMA + CONT utrzymuje ciągłość */
    adc_enable_external_trigger_regular(ADC1, ADC_CR2_EXTSEL_SWSTART);
    adc_start_conversion_regular(ADC1);
}

/* ===========================================================================
 *  Adaptacyjny filtr EMA + deadband (stałopozycynowy Q8)
 * =========================================================================== */
static inline void pot_filter(pot_state_t *p, uint16_t raw_now) {
    /* Pierwsza aktualizacja po starcie - inicjalizacja bez opóźnienia */
    if (!ema_initialized) {
        p->ema_x256       = (int32_t)raw_now << 8;
        p->last_sent_x256 = p->ema_x256;
        p->raw_last       = raw_now;
        p->dirty          = 1;
        return;
    }

    int32_t raw32 = (int32_t)raw_now;
    int32_t ema   = p->ema_x256 >> 8;
    int32_t err   = raw32 - ema;
    int32_t abs_err = (err < 0) ? -err : err;

    /* --- Strefa martwa: szum ignorujemy --- */
    if (abs_err < g_cfg.deadband) return;

    /* --- Adaptacyjny współczynnik alfa ---
     * Gwałtowny ruch → duży alfa (prawie bez wygładzania, zerowe opóźnienie)
     * Powolny ruch   → mały alfa  (silne wygładzanie) */
    int32_t alpha_x256 = (abs_err > (int32_t)g_cfg.fast_thr)
                       ? (int32_t)g_cfg.alpha_fast
                       : (int32_t)g_cfg.alpha_slow;

    /* EMA: ema += alpha × (raw - ema)
     * W Q8: ema_x256 += (alpha × (raw×256 - ema_x256)) / 256
     *     = (raw×256 - ema_x256) × alpha_x256 >> 8 */
    int32_t diff = (raw32 << 8) - p->ema_x256;
    p->ema_x256 += (diff * alpha_x256) >> 8;
    p->raw_last  = raw_now;

    /* --- Threshold wysyłki ---
     * Emituj zdarzenie tylko jeśli wartość przefiltrowana zmieniła się
     * istotnie - ogranicza ruch USB */
    int32_t send_diff = p->ema_x256 - p->last_sent_x256;
    if (send_diff < 0) send_diff = -send_diff;
    if (send_diff >= ((int32_t)g_cfg.send_thr << 8)) {
        p->last_sent_x256 = p->ema_x256;
        p->dirty          = 1;
    }
}

/* ===========================================================================
 *  Uśrednianie półki bufora DMA (CFG_ADC_OVERSAMPLE/2 próbek na kanał)
 * =========================================================================== */
void adc_consume_half(int half) {
    /* Każda "półka" bufora zawiera POT_COUNT × (OVERSAMPLE/2) próbek
     * ułożonych: [ch0,ch1,ch2,ch3,ch4, ch0,ch1,...] */
    const uint16_t spc = CFG_ADC_OVERSAMPLE / 2;            /* próbek/kanał/półka */
    const uint16_t base = half ? (POT_COUNT * spc) : 0;

    for (uint8_t ch = 0; ch < POT_COUNT; ch++) {
        uint32_t acc = 0;
        for (uint16_t s = 0; s < spc; s++) {
            uint16_t idx = base + s * POT_COUNT + ch;
            acc += adc_buffer[idx];
        }
        uint16_t avg = (uint16_t)(acc / spc);
        pot_filter(&pots[ch], avg);
    }
    ema_initialized = 1;
}

/* ---- ISR: DMA1 Channel 1 (Half-Transfer i Transfer-Complete) ---- */
void dma1_channel1_isr(void) {
    if (dma_get_interrupt_flag(DMA1, DMA_CHANNEL1, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA1, DMA_CHANNEL1, DMA_HTIF);
        adc_consume_half(0);             /* pierwsza połowa bufora */
    }
    if (dma_get_interrupt_flag(DMA1, DMA_CHANNEL1, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA1, DMA_CHANNEL1, DMA_TCIF);
        adc_consume_half(1);             /* druga połowa bufora */
    }
}

/* ===========================================================================
 *  API publiczne
 * ===========================================================================
 *
 *  C5 fix: dirty=0 czyszczone PRZED emit_pot. Sekwencja race:
 *    1) flush czyta dirty=1, czyta ema_x256=v1
 *    2) przerywa DMA ISR, który liczy v2, ustawia ema_x256=v2, dirty=1
 *    3) flush wznawia, emituje v1, czyści dirty=0 → v2 NIGDY nie emitowane
 *  Naprawa: czyść dirty PRZED emit. Jeśli ISR znów podniesie w trakcie emit,
 *  flush następnym razem wyślie aktualną wartość. To nie eliminuje wszystkich
 *  race'ów (seqlock byłby idealny) ale minimalizuje okno utraty zadań.
 * =========================================================================== */
void adc_flush_dirty(void) {
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        if (!pots[i].dirty) continue;
        uint16_t v = (uint16_t)(pots[i].ema_x256 >> 8);
        pots[i].dirty = 0;          /* C5: czyść PRZED emit_pot */
        protocol_emit_pot(i, v);
    }
}

uint16_t adc_pot_raw(uint8_t idx) {
    return (idx < POT_COUNT) ? pots[idx].raw_last : 0;
}

uint16_t adc_pot_filtered(uint8_t idx) {
    return (idx < POT_COUNT) ? (uint16_t)(pots[idx].ema_x256 >> 8) : 0;
}

uint8_t adc_pot_dirty(uint8_t idx) {
    return (idx < POT_COUNT) ? pots[idx].dirty : 0;
}

void adc_force_all_dirty(void) {
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        pots[i].dirty = 1;
    }
}
