/* =============================================================================
 *  MOCK_LIBOPENCM3_H - stub biblioteki libopencm3 do testów jednostkowych na PC.
 *
 *  Filozofia: moduły firmware'u (protocol.c, adc.c, buttons.c, leds.c, ...)
 *  kompilujemy z gcc natywnym z flagą -DTEST. Wtedy:
 *    1) -include ten nagłówek → stuby wszystkiego API libopencm3
 *    2) gpio_* / dma_* / usbd_* / rcc_* / nvic_* = puste makra lub mock fn
 *    3) test_main.c z własnym main() i asercjami
 *
 *  Dzięki temu testujemy logikę firmware'u (CRC, framing, debounce, EMA)
 *  na PC - szybko, bez hardware. To nie zastępuje testów na MCU, ale łapie
 *  80% bugów logicznych przed flashowaniem.
 * ============================================================================= */
#ifndef MOCK_LIBOPENCM3_H
#define MOCK_LIBOPENCM3_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* ---- Makra atrybutów (GCC na PC ma __attribute__) ---- */
#ifndef __attribute__
#define __attribute__(x)
#endif

/* ---- Stuby funkcji ze ścieżek libopencm3 ----
 * W testach wszystkie wywołania sprzętowe = no-op. */

/* ---- libopencm3/stm32/gpio.h ---- */
#define GPIOA  ((uint32_t)0x40010800)
#define GPIOB  ((uint32_t)0x40010C00)
#define GPIOC  ((uint32_t)0x40011000)
#define GPIOD  ((uint32_t)0x40011400)

#define GPIO0  (1u << 0)
#define GPIO1  (1u << 1)
#define GPIO2  (1u << 2)
#define GPIO3  (1u << 3)
#define GPIO4  (1u << 4)
#define GPIO5  (1u << 5)
#define GPIO6  (1u << 6)
#define GPIO7  (1u << 7)
#define GPIO8  (1u << 8)
#define GPIO9  (1u << 9)
#define GPIO10 (1u << 10)
#define GPIO11 (1u << 11)
#define GPIO12 (1u << 12)
#define GPIO13 (1u << 13)
#define GPIO14 (1u << 14)
#define GPIO15 (1u << 15)

#define GPIO_MODE_INPUT                0x00
#define GPIO_MODE_OUTPUT_2_MHZ         0x02
#define GPIO_MODE_OUTPUT_10_MHZ        0x01
#define GPIO_MODE_OUTPUT_50_MHZ        0x03
#define GPIO_CNF_INPUT_ANALOG          0x00
#define GPIO_CNF_INPUT_PULL_UPDOWN     0x02
#define GPIO_CNF_OUTPUT_PUSHPULL       0x00

/* Mock stanu GPIO - test może ustawić przycisk "wciśnięty" */
extern uint32_t mock_gpio_idr[4];   /* index 0=A, 1=B, 2=C, 3=D */
static inline uint32_t gpio_get(uint32_t gpioport, uint16_t gpios) {
    /* Mapowanie GPIOA/B/C/D na indeks 0/1/2/3 */
    uint32_t idx = (gpioport - GPIOA) / 0x400;
    if (idx > 3) return 0;
    return mock_gpio_idr[idx] & gpios;
}
static inline void gpio_set(uint32_t gpioport, uint16_t gpios) {
    uint32_t idx = (gpioport - GPIOA) / 0x400;
    if (idx > 3) return;
    mock_gpio_idr[idx] |= gpios;
}
static inline void gpio_clear(uint32_t gpioport, uint16_t gpios) {
    uint32_t idx = (gpioport - GPIOA) / 0x400;
    if (idx > 3) return;
    mock_gpio_idr[idx] &= ~gpios;
}
static inline void gpio_set_mode(uint32_t gpioport, uint8_t mode, uint8_t cnf, uint16_t gpios) {
    (void)gpioport; (void)mode; (void)cnf; (void)gpios;
}

/* ---- libopencm3/stm32/rcc.h ---- */
#define RCC_GPIOA   0
#define RCC_GPIOB   1
#define RCC_GPIOC   2
#define RCC_GPIOD   3
#define RCC_AFIO    4
#define RCC_USB     5
#define RCC_ADC1    6
#define RCC_DMA1    7
static inline void rcc_periph_clock_enable(uint32_t periph) { (void)periph; }

/* ---- libopencm3/stm32/adc.h ---- */
#define ADC1        ((uint32_t)0x40012400)
#define ADC_SMPR_SMP_239DOT5CYC 7
static inline void adc_power_off(uint32_t adc) { (void)adc; }
static inline void adc_power_on(uint32_t adc) { (void)adc; }
static inline void adc_set_regular_sequence(uint32_t adc, uint8_t len, uint8_t *ch) { (void)adc; (void)len; (void)ch; }
static inline void adc_set_sample_time_on_all_channels(uint32_t adc, uint8_t time) { (void)adc; (void)time; }
static inline void adc_enable_scan_mode(uint32_t adc) { (void)adc; }
static inline void adc_set_continuous_conversion_mode(uint32_t adc) { (void)adc; }
static inline void adc_enable_dma(uint32_t adc) { (void)adc; }
static inline void adc_reset_calibration(uint32_t adc) { (void)adc; }
static inline void adc_calibrate(uint32_t adc) { (void)adc; }
static inline void adc_start_conversion_regular(uint32_t adc) { (void)adc; }

/* V3: adc_enable_external_trigger_regular (mock — no-op dla testów) */
#define ADC_CR2_EXTSEL_SWSTART   0x7
static inline void adc_enable_external_trigger_regular(uint32_t adc, uint32_t trigger) { (void)adc; (void)trigger; }

/* ---- libopencm3/stm32/dma.h ---- */
#define DMA1            ((uint32_t)0x40020000)
#define DMA_CHANNEL1    1
#define DMA_CCR_PSIZE_16BIT  0x500
#define DMA_CCR_MSIZE_16BIT  0x500
#define DMA_HTIF        (1 << 2)
#define DMA_TCIF        (1 << 1)
static inline void dma_channel_reset(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_set_peripheral_address(uint32_t dma, uint8_t channel, uint32_t addr) { (void)dma; (void)channel; (void)addr; }
static inline void dma_set_memory_address(uint32_t dma, uint8_t channel, uint32_t addr) { (void)dma; (void)channel; (void)addr; }
static inline void dma_set_number_of_data(uint32_t dma, uint8_t channel, uint16_t n) { (void)dma; (void)channel; (void)n; }
static inline void dma_set_read_from_peripheral(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_enable_memory_increment_mode(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_disable_peripheral_increment_mode(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_set_peripheral_size(uint32_t dma, uint8_t channel, uint32_t size) { (void)dma; (void)channel; (void)size; }
static inline void dma_set_memory_size(uint32_t dma, uint8_t channel, uint32_t size) { (void)dma; (void)channel; (void)size; }
static inline void dma_enable_circular_mode(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_enable_half_transfer_interrupt(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_enable_transfer_complete_interrupt(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_enable_channel(uint32_t dma, uint8_t channel) { (void)dma; (void)channel; }
static inline void dma_clear_interrupt_flags(uint32_t dma, uint8_t channel, uint32_t interrupts) { (void)dma; (void)channel; (void)interrupts; }
static inline uint32_t dma_get_interrupt_flag(uint32_t dma, uint8_t channel, uint32_t flag) { (void)dma; (void)channel; (void)flag; return 0; }

/* ADC_DR w libopencm3 - makro. Zmockowane na statyczny bufor. */
extern uint32_t mock_adc_dr;
#define ADC_DR(adc)  (*(volatile uint32_t *)&mock_adc_dr)

/* ---- libopencm3/cm3/nvic.h ---- */
#define NVIC_DMA1_CHANNEL1_IRQ        11
#define NVIC_USB_LP_CAN_RX0_IRQ       20
#define NVIC_USB_HP_CAN_TX_IRQ        19
static inline void nvic_set_priority(uint8_t irqn, uint8_t priority) { (void)irqn; (void)priority; }
static inline void nvic_enable_irq(uint8_t irqn) { (void)irqn; }

/* ---- libopencm3/cm3/systick.h ---- */
static inline void systick_set_clocksource(uint32_t clk) { (void)clk; }
static inline void systick_set_reload(uint32_t reload) { (void)reload; }
static inline void systick_interrupt_enable(void) {}
static inline void systick_clear(void) {}
static inline void systick_counter_enable(void) {}

/* ---- libopencm3/usb/usbd.h ---- */
typedef struct usbd_device usbd_device;
typedef struct usb_setup_data usb_setup_data;
typedef void (*usbd_control_complete)(usbd_device *usbd_dev, struct usb_setup_data *req);
enum usbd_request_return_codes {
    USBD_REQ_HANDLED = 1,
    USBD_REQ_NEXT_CALLBACK = 2,
};
#define USB_REQ_GET_DESCRIPTOR         0x06
#define USB_DT_REPORT                  0x22
#define USB_REQ_TYPE_STANDARD          0x00
#define USB_REQ_TYPE_INTERFACE         0x01
#define USB_REQ_TYPE_TYPE              0x60
#define USB_REQ_TYPE_RECIPIENT         0x1F
typedef struct {} usb_device_descriptor;
typedef struct {} usb_endpoint_descriptor;
typedef struct {} usb_interface_descriptor;
typedef struct {} usb_interface;
typedef struct {} usb_config_descriptor;
struct usb_setup_data { uint8_t bmRequestType; uint8_t bRequest; uint16_t wValue; uint16_t wIndex; uint16_t wLength; };
struct usb_hid_descriptor { uint8_t bLength; uint8_t bDescriptorType; uint16_t bcdHID; uint8_t bCountryCode; uint8_t bNumDescriptors; };
#define USB_DT_DEVICE_SIZE        18
#define USB_DT_DEVICE             0x01
#define USB_CLASS_HID             0x03
#define USB_DT_ENDPOINT_SIZE      7
#define USB_DT_ENDPOINT           0x05
#define USB_ENDPOINT_ATTR_INTERRUPT 0x03
#define USB_DT_INTERFACE_SIZE     9
#define USB_DT_INTERFACE          0x04
#define USB_DT_CONFIGURATION_SIZE 9
#define USB_DT_CONFIGURATION      0x02
#define USB_DT_HID                0x21

/* Mock funkcji usbd_* - zwracają deterministyczne wartości */
static inline usbd_device *usbd_init(const void *driver, const void *dev, const void *cfg,
                                      const char **strs, int nstr,
                                      uint8_t *buf, uint16_t bufsize) {
    (void)driver; (void)dev; (void)cfg; (void)strs; (void)nstr; (void)buf; (void)bufsize;
    return (usbd_device *)1;   /* non-NULL = success */
}
static inline void usbd_register_set_config_callback(usbd_device *dev, void *cb) { (void)dev; (void)cb; }
static inline void usbd_register_reset_callback(usbd_device *dev, void *cb) { (void)dev; (void)cb; }
static inline void usbd_register_control_callback(usbd_device *dev, uint8_t a, uint8_t b, void *cb) { (void)dev; (void)a; (void)b; (void)cb; }
static inline void usbd_ep_setup(usbd_device *dev, uint8_t ep, uint8_t type, uint16_t size, void *cb) { (void)dev; (void)ep; (void)type; (void)size; (void)cb; }
static inline void usbd_poll(usbd_device *dev) { (void)dev; }

/* Konfigurowalny mock dla usbd_ep_write_packet - test może kontrolować czy EP zajęty */
extern uint8_t mock_ep_write_returns;   /* 0=EP zajęty, 1=sukces */
static inline uint16_t usbd_ep_write_packet(usbd_device *dev, uint8_t ep, const void *buf, uint16_t len) {
    (void)dev; (void)ep; (void)buf; (void)len;
    return mock_ep_write_returns;
}
static inline uint16_t usbd_ep_read_packet(usbd_device *dev, uint8_t ep, void *buf, uint16_t len) {
    (void)dev; (void)ep; (void)buf; (void)len;
    return 0;
}

/* ---- Inne, których firmware używa, ale nieistotne dla testów ---- */
#define STK_CSR_CLKSOURCE_AHB_DIV8 0
#define RCC_CLOCK_HSE8_72MHZ        0
extern const uint8_t rcc_hse_configs[1];
static inline void rcc_clock_setup_pll(const void *cfg) { (void)cfg; }

#endif /* MOCK_LIBOPENCM3_H */
