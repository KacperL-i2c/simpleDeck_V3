/* =============================================================================
 *  GREJEM OS / usbhid.h
 *  USB Custom HID (EP1 IN + EP1 OUT), 64-bajtowe raporty.
 *  VID=0x1209, PID=0xDE10 (pid.codes / GREJEM INDUSTRIES).
 * ============================================================================= */
#ifndef GREJEM_USBHID_H
#define GREJEM_USBHID_H

#include <stdint.h>

/* Inicjalizuje stos USB, rejestruje deskryptory, włącza przerwania USB. */
void usbhid_init(void);

/* Zwraca 1 jeśli host zaadresował i skonfigurował urządzenie (SET_CONFIG). */
uint8_t usbhid_ready(void);

/* Wywoływane z superloopa - jeśli cokolwiek czeka w kolejce TX protokołu,
 * buduje raport HID i wysyła przez EP1 IN. */
void usbhid_pump(void);

#endif /* GREJEM_USBHID_H */
