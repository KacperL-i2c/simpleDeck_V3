/* =============================================================================
 *  GREJEM OS / usbhid.c
 *
 *  USB Custom HID dla STM32F103C6T6. Stos libopencm3 + st_usbfs_v1 driver.
 *
 *  Konfiguracja endpointów:
 *    EP1 IN  (0x81) - MCU → PC, 64 B interrupt, bInterval=1 ms
 *    EP1 OUT (0x01) - PC  → MCU, 64 B interrupt, bInterval=1 ms
 *
 *  VID=0x1209 (pid.codes), PID=0xDE10 (GREJEM Stream Deck).
 *
 *  Report ID 0x00:
 *    Deskryptor HID nie deklaruje jawnego Report ID, więc host stack
 *    (hidapi / Windows HID) traktuje wszystkie raporty jako "Report 0".
 *    Konwencja: hid_write() po stronie PC musi mieć buf[0] = 0x00 (ten bajt
 *    jest konsumowany przez host stack i nie trafia na EP). Po stronie MCU
 *    bufor EP zawiera wyłącznie 64 B payloadu (czyli pełną ramkę protokołu).
 *
 *  Architektura runtime:
 *    - ISR usb_lp_can_rx0_isr / usb_hp_can_tx_isr wołają usbd_poll() na
 *      zdarzenia USB. Po SET_CONFIGURATION aktywujemy endpointy i flagę
 *      usb_configured.
 *    - usbhid_pump() wołane z schedulera co 1 ms: jeśli cokolwiek czeka
 *      w kolejce TX protokołu, formatuje raport i wysyła przez EP1 IN.
 * ============================================================================= */
#include "usbhid.h"
#include "config.h"
#include "board.h"
#include "protocol.h"

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/usbstd.h>
#include <libopencm3/stm32/st_usbfs.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>

#include <string.h>

/* ===========================================================================
 *  HID Report Descriptor
 *  Vendor Page 0xFF00, jedna kolekcja Application.
 *  Input  report: 64 bajty raw (MCU → PC)
 *  Output report: 64 bajty raw (PC → MCU)
 * =========================================================================== */
static const uint8_t hid_report_descriptor[] = {
    0x06, 0x00, 0xFF,    /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,          /* Usage (Vendor 0x01)                */
    0xA1, 0x01,          /* Collection (Application)           */

    /* --- Input report (MCU → PC): 64 bajty --- */
    0x09, 0x02,                       /*   Usage (Vendor 0x02)            */
    0x15, 0x00,                       /*   Logical Minimum (0)            */
    0x26, 0xFF, 0x00,                 /*   Logical Maximum (255)          */
    0x75, 0x08,                       /*   Report Size (8 bits)           */
    0x95, CFG_HID_EP_SIZE,            /*   Report Count (64)              */
    0x81, 0x02,                       /*   Input (Data,Var,Abs)           */

    /* --- Output report (PC → MCU): 64 bajty --- */
    0x09, 0x03,                       /*   Usage (Vendor 0x03)            */
    0x15, 0x00,                       /*   Logical Minimum (0)            */
    0x26, 0xFF, 0x00,                 /*   Logical Maximum (255)          */
    0x75, 0x08,                       /*   Report Size (8 bits)           */
    0x95, CFG_HID_EP_SIZE,            /*   Report Count (64)              */
    0x91, 0x02,                       /*   Output (Data,Var,Abs)          */

    0xC0,                             /* End Collection                   */
};

/* ===========================================================================
 *  Device / Configuration / Interface descriptors
 * =========================================================================== */
static const struct usb_device_descriptor device_descriptor = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,                  /* USB 2.0              */
    .bDeviceClass       = 0,                       /* class zdefiniowany na poziomie interfejsu (standard HID) */
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 8,                       /* EP0 — bezpieczne dla Windows timing */
    .idVendor           = BOARD_USB_VID,
    .idProduct          = BOARD_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor hid_endpoints[] = {{
    .bLength          = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CFG_HID_EP_IN,             /* 0x81 EP1 IN          */
    .bmAttributes     = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize   = CFG_HID_EP_SIZE,
    .bInterval        = CFG_HID_POLL_INTERVAL_MS,
}, {
    .bLength          = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CFG_HID_EP_OUT,            /* 0x01 EP1 OUT         */
    .bmAttributes     = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize   = CFG_HID_EP_SIZE,
    .bInterval        = CFG_HID_POLL_INTERVAL_MS,
}};

/* HID descriptor (class-specific) + meta-info o Report Descriptorze.
 * Libopencm3 oczekuje takiej struktury w polu `extra` deskryptora interfejsu. */
static const struct {
    struct usb_hid_descriptor hid;
    struct {
        uint8_t  bDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) report_desc_meta;
} __attribute__((packed)) hid_function = {
    .hid = {
        .bLength            = sizeof(struct usb_hid_descriptor) + 3,
        .bDescriptorType    = USB_DT_HID,
        .bcdHID             = 0x0111,              /* HID 1.11             */
        .bCountryCode       = 0,
        .bNumDescriptors    = 1,
    },
    .report_desc_meta = {
        .bDescriptorType   = USB_DT_REPORT,
        .wDescriptorLength = sizeof(hid_report_descriptor),
    },
};

static const struct usb_interface_descriptor hid_iface[] = {{
    .bLength            = USB_DT_INTERFACE_SIZE,
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 2,                       /* IN + OUT             */
    .bInterfaceClass    = USB_CLASS_HID,
    .bInterfaceSubClass = 0,                       /* brak Boot            */
    .bInterfaceProtocol = 0,                       /* brak Mouse/Kbd       */
    .iInterface         = 0,
    .endpoint           = hid_endpoints,
    .extra              = &hid_function,
    .extralen           = sizeof(hid_function),
}};

static const struct usb_interface ifaces[] = {{
    .num_altsetting = 1,
    .altsetting     = hid_iface,
}};

static const struct usb_config_descriptor config_descriptor = {
    .bLength             = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType     = USB_DT_CONFIGURATION,
    .wTotalLength        = 0,                      /* wyliczane automatycznie */
    .bNumInterfaces      = 1,
    .bConfigurationValue = 1,
    .iConfiguration      = 0,
    .bmAttributes        = 0x80,                   /* bus-powered, no RW  */
    .bMaxPower           = 0x32,                   /* 100 mA              */
    .interface           = ifaces,
};

/* String descriptors (UTF-16 - libopencm3 konwertuje automatycznie) */
static const char *usb_strings[] = {
    "",                          /* idx 0 - zarezerwowane (język) */
    BOARD_USB_VENDOR,            /* idx 1 - iManufacturer         */
    BOARD_USB_PRODUCT,           /* idx 2 - iProduct              */
    BOARD_USB_SERIAL,            /* idx 3 - iSerialNumber         */
};

/* ===========================================================================
 *  Stan runtime
 * =========================================================================== */
static usbd_device *usbd_dev;
static uint8_t usbd_control_buffer[128];           /* bufor dla EP0 control  */
static volatile uint8_t usb_configured = 0;        /* 1 po SET_CONFIGURATION */

/* Bufory statyczne dla EP - potrzebne aby nie alokować na stosie (alignment). */
static uint8_t hid_out_buf[CFG_HID_EP_SIZE];
static uint8_t hid_in_buf [CFG_HID_EP_SIZE];

/* ===========================================================================
 *  Callbacki USB
 * =========================================================================== */

/* EP1 OUT - przychodzący raport od PC (komenda do MCU) */
static void hid_out_callback(usbd_device *dev, uint8_t ep) {
    (void)ep;
    uint16_t n = usbd_ep_read_packet(dev, CFG_HID_EP_OUT,
                                     hid_out_buf, sizeof(hid_out_buf));
    if (n > 0) {
        protocol_handle_out(hid_out_buf, (uint8_t)n);
    }
}

/* SET_CONFIGURATION od hosta - urządzenie zostało zaadresowane i skonfigurowane.
 * Tu aktywujemy endpointy interrupt IN/OUT. */
static void hid_set_config(usbd_device *dev, uint16_t wValue) {
    (void)wValue;
    usbd_ep_setup(dev, CFG_HID_EP_IN,  USB_ENDPOINT_ATTR_INTERRUPT,
                  CFG_HID_EP_SIZE, NULL);
    usbd_ep_setup(dev, CFG_HID_EP_OUT, USB_ENDPOINT_ATTR_INTERRUPT,
                  CFG_HID_EP_SIZE, hid_out_callback);
    usb_configured = 1;
}

/* C2 fix: USB bus reset (host reboot, unplug+replug, suspend/resume).
 * libopencm3 wraca do stanu default - czyści endpointy. Bez tego callbacku
 * usb_configured zostaje 1 i usbhid_pump próbuje pisać po nienaskonfigurowanym
 * EP1 IN → nieokreślone zachowanie. */
static void hid_reset_callback(void) {
    usb_configured = 0;
}

/* Standardowe zapytanie kontrolne GET_DESCRIPTOR(HID_REPORT).
 * Filtr w usbd_register_control_callback gwarantuje, że dostaniemy tu tylko
 * żądania STANDARD|INTERFACE - sprawdzamy jeszcze bRequest i typ deskryptora. */
static enum usbd_request_return_codes hid_control_request(usbd_device *dev,
                                struct usb_setup_data *req,
                                uint8_t **buf, uint16_t *len,
                                usbd_control_complete_callback *complete) {
    (void)dev;
    (void)complete;

    if (req->bRequest != USB_REQ_GET_DESCRIPTOR) {
        return USBD_REQ_NEXT_CALLBACK;
    }
    uint8_t desc_type = (uint8_t)(req->wValue >> 8);

    if (desc_type == USB_DT_REPORT) {
        *buf = (uint8_t *)hid_report_descriptor;
        *len = sizeof(hid_report_descriptor);
        return USBD_REQ_HANDLED;
    }
    /* Windows HID class driver (hidusb.sys) wysyła osobne GET_DESCRIPTOR(HID)
     * niezależnie od configuration descriptor. libopencm3 usb_standard.c NIE
     * obsługuje USB_DT_HID (0x21) — pole `extra` interfejsu jest używane tylko
     * do budowy configuration descriptor, nie do odpowiedzi na to żądanie.
     * Bez tego handlera → USBD_REQ_NOTSUPP → EP0 STALL → Windows Code 10. */
    if (desc_type == USB_DT_HID) {
        *buf = (uint8_t *)&hid_function;
        *len = sizeof(hid_function);
        return USBD_REQ_HANDLED;
    }
    return USBD_REQ_NEXT_CALLBACK;
}

/* ===========================================================================
 *  API publiczne
 * =========================================================================== */
void usbhid_init(void) {
    /* USB 48 MHz pochodzi z PLL (konfiguracja rcc_clock_setup_pll w main.c).
     * Włącz taktowanie bloku USB bezpośrednio przed konfiguracją stacku,
     * by D+ pull-up aktywował się dopiero gdy USB peripheral jest gotowy
     * (eliminuje race z pierwszym USB reset od hosta). */
    rcc_periph_clock_enable(RCC_USB);

    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver,
                         &device_descriptor,
                         &config_descriptor,
                         usb_strings,
                         sizeof(usb_strings) / sizeof(usb_strings[0]),
                         usbd_control_buffer,
                         sizeof(usbd_control_buffer));

    usbd_register_set_config_callback(usbd_dev, hid_set_config);
    usbd_register_reset_callback(usbd_dev, hid_reset_callback);  /* C2 fix */
    usbd_register_control_callback(usbd_dev,
                                   USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
                                   USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                                   hid_control_request);

    /* C3 fix: soft-disconnect/reconnect wymuszający host re-enumeration.
     *
     * Problem: po reset MCU (SWD, wdg, brownout) zewnętrzny pull-up 1.5k
     * na D+ (PA12) pozostaje aktywny cały czas.  Host nie widzi disconnect
     * → nie wysyła USB bus reset → _usbd_reset() nigdy nie leci → urządzenie
     * wisi: lsusb widzi 1209:de10, ale DADDR=0x0000 (EF=0), EP0 DISABLED,
     * usb_configured=0.  Tylko physical replug lub `usbreset` pomagało.
     *
     * libopencm3 st_usbfs_v1_usbd_init() celowo NIE włącza DADDR.EF ani
     * nie konfiguruje EP0 — to robota _usbd_reset() wywoływanego z ISR.
     * Ale _usbd_reset() wymaga ISTR.RESET od hosta, którego nie ma bez
     * disconnect/reconnect.
     *
     * Fix (2 etapy):
     *
     *   1) Soft-disconnect: power-down transceiver (CNTR.PWDN), przestaw
     *      PA12 na push-pull LOW na ~100 ms.  Host widzi SE0 (D+ < 0.8V)
     *      → rozłącza urządzenie.  Bez PWDN transceiver walczyłby z GPIO.
     *
     *   2) Reconnect + EP0 bootstrap: zwolnij PA12 (analog input), wyczyść
     *      PWDN/ISTR, skonfiguruj EP0 i włącz DADDR.EF.  Host wykrywa D+
     *      rising → wysyła USB reset → _usbd_reset() przejmuje.
     *
     * Konfiguracja EP0 w kroku 2 jest redundantna z _usbd_reset() (który
     * host wywoła po ~10-50 ms), ale zapewnia gotowość od razu.  Bufory:
     * BTABLE@0 (8 ep × 8 B = 64 B), EP0 TX@0x40, RX@0x80.  pm_top zostaje
     * naprawiony przez ep_reset() przy SET_CONFIGURATION (→ 0xC0).
     */
    /* C3 fix wyłączony: soft-disconnect + manualny EP0 setup AFTER usbd_init
     * interfere z enumeracją Windows (race z _usbd_reset hosta). Standardowy
     * flow libopencm3 powinien wystarczyć: usbd_init() ustawia CNTR z RESETM,
     * host wykrywa D+ pull-up, wysyła USB reset → _usbd_reset() konfiguruje
     * EP0 i włącza DADDR.EF automatycznie. */

    /* Włącz przerwania USB. F103 dzieli wektory CAN/USB - używamy obu IRQ. */
    nvic_set_priority(NVIC_USB_LP_CAN_RX0_IRQ, 2);   /* niższy niż DMA  */
    nvic_enable_irq (NVIC_USB_LP_CAN_RX0_IRQ);
    /* HP używany rzadko na F103 - dajemy priorytet niżej (3) niż DMA(1)/LP(2)
     * żeby nie preempt'ować istotnych zdarzeń. */
    nvic_set_priority(NVIC_USB_HP_CAN_TX_IRQ, 3);
    nvic_enable_irq (NVIC_USB_HP_CAN_TX_IRQ);
}

uint8_t usbhid_ready(void) {
    return usb_configured;
}

void usbhid_pump(void) {
    if (!usbd_dev)         return;
    if (!usb_configured)   return;
    if (!protocol_tx_pending()) return;

    /* C6 fix: najpierw sformatuj ramkę przez peek (bez zdejmowania z kolejki).
     * Jeśli usbd_ep_write_packet zwróci 0 (EP zajęty - poprzedni pakiet nie
     * został jeszcze odebrany przez host), ramka ZOSTAJE w kolejce i kolejna
     * iteracja usbhid_pump spróbuje ponownie. Eliminuje cichą utratę ramek. */
    uint8_t used = protocol_pump_peek(hid_in_buf);
    if (used == 0) return;

    /* usbd_ep_write_packet zwraca 1 jeśli pakiet zakolejkowany do wysłania,
     * 0 jeśli EP wciąż zajęty (poprzedni transfer nie zakończony). */
    uint8_t ok = usbd_ep_write_packet(usbd_dev, CFG_HID_EP_IN,
                                       hid_in_buf, CFG_HID_EP_SIZE);
    if (ok) {
        /* Sukces - zdejmij ramkę z kolejki TX. */
        protocol_pump_pop();
    }
    /* Jeśli ok==0: EP zajęty. Pozostawiamy ramkę w kolejce - spróbujemy
     * ponownie w nast. iteracji usbhid_pump (co 1 ms). */
}

/* ===========================================================================
 *  USB ISR (libopencm3 poll-mode)
 *  LP = low-priority events (setup, OUT, SOF)
 *  HP = high-priority events (rarely used, ale na F1 obsługuje też USB)
 * =========================================================================== */
void usb_lp_can_rx0_isr(void) {
    if (usbd_dev) usbd_poll(usbd_dev);
}
void usb_hp_can_tx_isr(void) {
    if (usbd_dev) usbd_poll(usbd_dev);
}
