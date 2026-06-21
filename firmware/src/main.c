/* =============================================================================
 *  GREJEM OS - main.c
 *
 *  Architektura oprogramowania dla STM32F103C6T6 (Cortex-M3 @ 72 MHz):
 *
 *   ├── clock_setup()       - HSE 8 MHz → PLL ×9 → SYSCLK 72 MHz (USB=48, ADC=36)
 *   ├── gpio_setup()        - LED onboard PC13 (statusowy)
 *   ├── scheduler_init()    - SysTick 1 kHz, rejestruje tablicę zadań
 *   ├── leds_init()         - 8 LED VU bar (PB10..PB15, PA9, PA10) push-pull
 *   ├── buttons_init()      - 4 przyciski PB6..PB9 z pull-up
 *   ├── adc_start()         - DMA1 Ch1 kołowo + ADC1 scan+continuous+DMA
 *   ├── usbhid_init()       - USB Custom HID z EP1 IN/OUT
 *   ├── heartbeat_init()    - LED 0 w trybie blink (status urządzenia)
 *
 *   └── while(1) { scheduler_dispatch(); __WFI(); }   ← superloop + sleep
 *
 *  ZASADA ZERO-BLOCK: żadna funkcja w ścieżce operacyjnej nie zawiesza CPU.
 *  Jedynymi wyjątkami są jednorazowe kalibracje ADC podczas adc_start().
 * ============================================================================= */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>

#include "board.h"
#include "config.h"
#include "scheduler.h"
#include "adc.h"
#include "buttons.h"
#include "leds.h"
#include "timer.h"
#include "heartbeat.h"
#include "usbhid.h"
#include "protocol.h"

/* ---- Inicjalizacja zegara ----
 * HSE 8 MHz → PLL ×9 → SYSCLK 72 MHz.
 * Side-effects: USB PLL daje 48 MHz, ADC = 12 MHz (PCLK2/6, spec wymaga ≤14),
 * APB1 = 36 MHz, APB2 = 72 MHz.
 * libopencm3 rcc_clock_setup_pll konfiguruje też preskalery i wait-state Flasha.
 */
static void clock_setup(void) {
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    /* Taktowanie peryferiów */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOD);
    rcc_periph_clock_enable(RCC_AFIO);    /* dla remapu / USB PD */
    rcc_periph_clock_enable(RCC_USB);
    rcc_periph_clock_enable(RCC_ADC1);
    rcc_periph_clock_enable(RCC_DMA1);
    rcc_periph_clock_enable(RCC_TIM2);    /* C10: HW PWM dla PB10/PB11 */
    rcc_periph_clock_enable(RCC_TIM3);    /* V2: SW PWM ISR dla PB12..PB15, PA9, PA10 */
}

/* ---- Konfiguracja LED onboard (PC13) - aktywny low ----
 * Używany tylko jako "device alive" - mruga razem z heartbeatem.
 * Reszta konfiguracji GPIO (LEDy, przyciski, piny ADC) jest w dedykowanych
 * modułach (leds_init / buttons_init / adc_start). */
static void status_gpio_setup(void) {
    gpio_set_mode(STATUS_LED_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  STATUS_LED_PIN);
    gpio_set(STATUS_LED_PORT, STATUS_LED_PIN);   /* OFF (aktywny low) */
}

int main(void) {
    /* 1. Zegar i bazowa konfiguracja */
    clock_setup();
    status_gpio_setup();

    /* 2. Peryferia + scheduler */
    scheduler_init();
    leds_init();
    timer_init();        /* C10: HW/SW PWM dla LEDów (po leds_init, po clock_setup) */
    buttons_init();
    adc_start();
    usbhid_init();
    heartbeat_init();   /* LED 0 → blink (status urządzenia) */

    /* 3. Superloop ----------------------------------------------------- */
    /* Wszystkie zadania są wywoływane przez scheduler_dispatch().
     * Gdy nie ma nic do roboty, WFI usypia CPU do następnego przerwania
     * (SysTick 1 kHz, DMA HT/TC, USB) - minimalny pobór prądu. */
    uint8_t pot_initial_sent = 0;
    while (1) {
        scheduler_dispatch();

        /* Po pierwszym połączeniu USB wyślij aktualne pozycje potencjometrów
         * aby desktop od razu pokazał ich stany (bez ruchu pota). */
        if (!pot_initial_sent && usbhid_ready()) {
            adc_force_all_dirty();
            pot_initial_sent = 1;
        }

        __asm__("wfi");
    }

    /* Niedościgalne - dla pewności */
    return 0;
}
