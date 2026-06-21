/* =============================================================================
 *  GREJEM OS / adc.h
 *  ADC1 + DMA1 Channel 1, ciągły skan 5 kanałów z adaptacyjnym filtrem EMA.
 * ============================================================================= */
#ifndef GREJEM_ADC_H
#define GREJEM_ADC_H

#include <stdint.h>

/* Konfiguruje piny analogowe, DMA1 Ch1 kołowo, ADC1 w trybie scan+continuous+DMA.
 * Po wywołaniu tej funkcji ADC zaczyna samodzielnie produkować dane. */
void adc_start(void);

#ifdef TEST
/* W trybie testowym bufor DMA jest współdzielony z testem (do wypełniania danymi). */
extern uint16_t adc_buffer[];
#endif

/* Pompowanie "brudnych" potencjometrów do protokołu HID.
 * Wywoływane przez scheduler co kilka ms. */
void adc_flush_dirty(void);

/* Wymuś oznaczenie wszystkich potencjometrów jako dirty (do wysłania).
 * Używane po połączeniu USB aby wysłać aktualne pozycje do PC. */
void adc_force_all_dirty(void);

/* ---- Diagnostic API ---- */
uint16_t adc_pot_raw(uint8_t idx);        /* ostatnia uśredniona surowa wartość */
uint16_t adc_pot_filtered(uint8_t idx);   /* wartość po filtrze EMA (0..4095)    */
uint8_t  adc_pot_dirty(uint8_t idx);      /* 1 = czeka wysłanie                  */

/* ---- Runtime tuning filtra (modyfikowane przez CFG_CMD z PC) ---- */
/* Wartości domyślne po starcie = z config.h (CFG_DEADBAND itd.). */
typedef struct {
    uint8_t deadband;     /* strefa martwa (LSB)                          */
    uint8_t fast_thr;     /* próg przełączenia na alfa szybki (LSB)        */
    uint8_t alpha_slow;   /* alfa ×256 dla wolnego ruchu                   */
    uint8_t alpha_fast;   /* alfa ×256 dla gwałtownego ruchu               */
    uint8_t send_thr;     /* próg wysyłki raportu do PC (LSB)              */
} adc_cfg_t;

/* Wskaźnik do globalnej struktury tuningu (żyje cały czas działania). */
adc_cfg_t *adc_cfg(void);

#endif /* GREJEM_ADC_H */
