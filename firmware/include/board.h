/* =============================================================================
 *  GREJEM OS / board.h
 *  Mapowanie pinów dla STM32F103C6T6 (LQFP48).
 *
 *  Pinout:
 *    PA0..PA4   <- 5x potencjometr      (ADC1_IN0..IN4)
 *    PA9/PA10   <- LED 6/7 VU bar       (byłe USART1 TX/RX - nieużywane)
 *    PA11/PA12  <- USB D-/D+            (hardware USB FS)
 *    PA13/PA14  <- SWDIO/SWCLK          (debug)
 *    PD0/PD1    <- HSE 8 MHz quartz
 *    PB6..PB9   <- 4x przycisk          (active-low, pull-up)
 *    PB10..PB15 <- LED 0..5 VU bar      (active-high, push-pull)
 *    PC13       <- LED onboard (Blue Pill, statusowy, active-low)
 *
 *  V2: 8-LED linijka VU (PB10..PB15 + PA9 + PA10) zastępuje 5 pojedynczych LED.
 *      HW PWM: PB10/PB11 (TIM2). SW PWM: PB12..PB15, PA9, PA10 (TIM3 ISR).
 * ============================================================================= */
#ifndef GREJEM_BOARD_H
#define GREJEM_BOARD_H

#include <libopencm3/stm32/gpio.h>
#include <stdint.h>

/* ---- Zegar ---- */
#define BOARD_HSE_HZ        8000000UL
#define BOARD_SYSCLK_HZ     72000000UL

/* ---- Potencjometry (ADC1_IN0..IN4 = PA0..PA4) ---- */
#define POT_COUNT           5
#define POT_PORT            GPIOA
#define POT_PIN_MASK        (GPIO0 | GPIO1 | GPIO2 | GPIO3 | GPIO4)
/* Kanały ADC1 w kolejności skanowania (numer kanału, NIE numer pinu) */
#define POT_ADC_CHANNEL_LIST  0, 1, 2, 3, 4

/* ---- Przyciski ---- */
#define BUTTON_COUNT        4
struct button_pin {
    uint32_t port;
    uint16_t pin;
};
extern const struct button_pin board_buttons[BUTTON_COUNT];

/* ---- Diody LED (V2: 8-LED VU bar, V3: 3 aktywne) ---- */
#define LED_COUNT           8       /* wszystkie piny LED zainicjalizowane */
#define ACTIVE_LED_COUNT    3       /* V3: fizycznie podłączone (indeksy 0,1,2 = PB10,PB11,PB12) */
struct led_pin {
    uint32_t port;
    uint16_t pin;
};
extern const struct led_pin board_leds[LED_COUNT];

/* ---- LED onboard (PC13) - statusowy ---- */
#define STATUS_LED_PORT     GPIOC
#define STATUS_LED_PIN      GPIO13

/* ---- USB ---- */
#define BOARD_USB_VID       0x1209          /* pid.codes (public) */
#define BOARD_USB_PID       0xDE10          /* GREJEM Stream Deck */
#define BOARD_USB_VENDOR    "GREJEM INDUSTRIES"
#define BOARD_USB_PRODUCT   "GREJEM Stream Deck"
#define BOARD_USB_SERIAL    "GREJ-DECK-0001"

#endif /* GREJEM_BOARD_H */
