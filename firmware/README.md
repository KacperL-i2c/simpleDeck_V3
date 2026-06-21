# GREJEM OS — Firmware (ETAP 1 + 2)

Bare-metal firmware dla **STM32F103C6T6** (Cortex-M3 @ 72 MHz), napisany w C
z wykorzystaniem [libopencm3](https://github.com/libopencm3/libopencm3).
Odczyt 5 potencjometrów przez ADC+DMA z adaptacyjnym filtrem EMA, debouncing
4 przycisków, sterowanie 8 diodami LED (linijka VU bar), USB Custom HID do komunikacji z PC.

---

## 1. Struktura katalogów

```
firmware/
├── include/
│   ├── board.h         ← mapowanie pinów (PA0..PA4 / PB6..PB15, PA9, PA10)
│   ├── config.h        ← wszystkie strojalne stałe (deadband, alfa, debounce…)
│   ├── protocol.h      ← format ramki + enum komend + CRC16
│   ├── scheduler.h     ← API non-blocking schedulera (SysTick 1 kHz)
│   ├── adc.h           ← API odczytu potencjometrów (DMA + filtr EMA)
│   ├── buttons.h       ← debouncing przycisków
│   ├── leds.h          ← sterowanie LED (off/on/blink/dim/pulse/breathe/strobe/heartbeat)
│   ├── timer.h         ← konfiguracja TIM2 (HW PWM) + TIM3 (SW PWM ISR)
│   ├── heartbeat.h     ← periodyczny pakiet "żyję"
│   └── usbhid.h        ← Custom HID (EP1 IN/OUT, 64 B)
├── src/
│   ├── main.c          ← superloop + inicjalizacja peryferiów
│   ├── board.c         ← tablice opisujące piny
│   ├── scheduler.c     ← tabela zadań periodycznych
│   ├── adc.c           ← ADC1+DMA1 Ch1 + adaptacyjny EMA + deadband
│   ├── buttons.c       ← integrator debouncingu
│   ├── leds.c          ← sterowanie LED + PWM (V2: VU bar 8 LED, segmenty + smooth top)
│   ├── timer.c         ← TIM2 HW PWM (PB10/PB11) + TIM3 ISR 8 kHz (SW PWM PB12..PB15, PA9, PA10)
│   ├── heartbeat.c     ← pakiet heartbeat + LED statusowy
│   ├── protocol.c      ← CRC16-CCITT + ramkowanie TX/RX
│   └── usbhid.c        ← deskryptory USB + endpointy HID
├── ld/
│   └── stm32f103x6.ld  ← linker script (Flash 32K, RAM 10K)
├── tools/              ← (miejsce na skrypty testowe - Etap 3)
├── Makefile
└── README.md
```

---

## 2. Wymagania

| Komponent | Wersja / instalacja |
|---|---|
| **arm-none-eabi-gcc** | ≥ 10.x  (`sudo apt install gcc-arm-none-eabi`) |
| **libopencm3** | Najnowszy master z GitHuba (zbudowany dla `stm32/f1`) |
| **stlink-tools** | `st-flash` do flashowania (opcjonalne) |
| **openocd** | Alternatywny programator (opcjonalne) |

---

## 3. Pierwsza instalacja

```bash
# 1) Sklonuj i zbuduj libopencm3
git clone https://github.com/libopencm3/libopencm3.git ~/src/libopencm3
cd ~/src/libopencm3
make TARGET=stm32/f1            # buduje lib/libopencm3_stm32f1.a
cd ..

# 2) Skonfiguruj zmienną środowiskową
export OPENCM3_DIR=~/src/libopencm3

# 3) Skompiluj firmware
cd "/home/kacper/Nextcloud/Projekty/Simple Deck V2/firmware"
make
```

Po udanej kompilacji powinieneś zobaczyć raport zużycia pamięci:

```
   text	   data	    bss	    dec	    hex	filename
   6424	     24	    488	   6936	   1b18	build/grejem-fw.elf
```

(≤ 32 KB Flash i ≤ 10 KB RAM — spory margines).

---

## 4. Flashowanie

### Opcja A: ST-Link v2 + st-flash (najszybsze)

```bash
make flash
# równoważnie: st-flash write build/grejem-fw.bin 0x08000000
```

### Opcja B: openocd (uniwersalne)

```bash
make ocdflash
```

### Opcja C: ręcznie przez DFU (USB bootloader)

```bash
dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D build/grejem-fw.bin
```

---

## 5. Weryfikacja — czy urządzenie działa?

Po wgraniu i podłączeniu USB:

```bash
# Linux
lsusb | grep 1209:de10
# powinno pokazać: Bus xxx Device xxx: ID 1209:de10 Generic GREJEM Stream Deck

# Sprawdzenie deskryptorów
lsusb -v -d 1209:de10 | head -50

# Sprawdzenie ścieżki hidraw
ls -l /dev/hidraw*
```

Na MCULED 0 + LED onboard (PC13) zaczną migać co ~500 ms — dowód, że
scheduler + heartbeat działają. Po skonfigurowaniu hosta (SET_CONFIGURATION)
rozpoczną wysyłane pakiety heartbeat co 1.5 s.

---

## 6. Format ramki (krótkie przypomnienie)

Pełna specyfikacja w `include/protocol.h` oraz `docs/PROTOCOL.md`.

```
SOF(0xA5) | TYPE | CH | LEN | PAYLOAD[0..LEN-1] | CRC16_LO | CRC16_HI
```

Po stronie PC każdy 64-bajtowy raport HID musi być poprzedzony bajtem
**Report ID = 0x00** (konwencja hidapi — stack hosta go konsumuje i nie
trafia do MCU).

---

## 7. Tuning

Wszystkie progi filtra i czasy zadań są w **`include/config.h`**.

| Stała | Domyślnie | Co robi |
|---|---|---|
| `CFG_DEADBAND` | 8 | poniżej tej zmiany surowej = szum, ignoruj |
| `CFG_FAST_THR` | 128 | powyżej tej zmiany = szybki ruch (alfa=0.80) |
| `CFG_ALPHA_SLOW_X256` | 13 (≈0.05) | współczynnik EMA dla spokojnego ruchu |
| `CFG_ALPHA_FAST_X256` | 205 (≈0.80) | współczynnik EMA dla gwałtownego ruchu |
| `CFG_SEND_THR` | 16 | próg wysyłki do PC (ogranicza ruch USB) |
| `CFG_DEBOUNCE_TICKS` | 5 ms | czas integratora przycisku |
| `CFG_HEARTBEAT_PERIOD_MS` | 1500 ms | okres heartbeatu |

---

## 8. Co dalej (ETAP 3)

- `tools/hid_probe.py` — prosty skrypt testowy do debugowania z konsoli
- Aplikacja desktopowa `GREJEM OS` w Pythonie + PySide6 — auto-reconnect,
  detekcja okien, kontrola głośności, hotkeys, Glossy UI.
- Instalatory Windows (Inno Setup) + Linux (.deb, reguła udev).
