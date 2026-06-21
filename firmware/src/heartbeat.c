/* =============================================================================
 *  GREJEM OS / heartbeat.c
 *
 *  Periodyczny sygnał "żyję" wysyłany do PC co CFG_HEARTBEAT_PERIOD_MS (1.5s).
 *  Po stronie PC watchdog oczekuje heartbeatu; jeśli nie przyjdzie przez 3×
 *  ten czas (4.5 s), aplikacja przechodzi w tryb auto-reconnect.
 *
 *  Tu inicjalizujemy też LED 0 w trybie BLINK - wizualny sygnał że urządzenie
 *  jest włączone. LED onboard (PC13) jest przełączany razem z heartbeatem
 *  jako backupowy wskaźnik "żyję" (na wypadek gdyby LEDy zewnętrzne nie były
 *  podłączone).
 * ============================================================================= */
#include "heartbeat.h"
#include "config.h"
#include "scheduler.h"
#include "protocol.h"
#include "leds.h"
#include "board.h"

#include <libopencm3/stm32/gpio.h>

void heartbeat_init(void) {
    /* LED 0 miga cały czas = "urządzenie żyje, pętla działa" */
    led_set(0, LED_MODE_BLINK);
}

void heartbeat_tick(void) {
    /* Wyślij pakiet heartbeat do PC (uptime_ms + wersja FW w 1 bajcie) */
    protocol_emit_heartbeat(scheduler_millis());

    /* Backupowy sygnał na LED onboard (PC13 - aktywny low) */
    static uint8_t hb_toggle = 0;
    hb_toggle ^= 1;
    if (hb_toggle) gpio_clear(STATUS_LED_PORT, STATUS_LED_PIN);   /* ON  */
    else           gpio_set  (STATUS_LED_PORT, STATUS_LED_PIN);   /* OFF */
}
