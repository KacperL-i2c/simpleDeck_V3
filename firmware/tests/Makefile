# =============================================================================
#  GREJEM OS - Firmware Unit Tests (native PC build with mocks)
#
#  Buduje moduły firmware z gcc natywnym (nie arm-none-eabi) i linkuje z
#  mockami libopencm3. Testy są osobnymi plikami .c z main() i asercjami.
#
#  Użycie:
#    make            - buduje i uruchamia wszystkie testy
#    make test_X     - buduje i uruchamia konkretny test
#    make clean
# =============================================================================
CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 -g
CFLAGS  += -I../include -I./mocks
CFLAGS  += -DTEST -DMOCK_LIBOPENCM3
CFLAGS  += -Wl,--allow-multiple-definition
# Wymuś include mocks/libopencm3_mock.h na początku każdego TU
CFLAGS  += -include mocks/libopencm3_mock.h

# Katalog ze źródłami firmware
SRC_DIR = ../src

# Mocki
MOCK_SRC = mocks/mock_state.c

# Wspólny szkielon testów: brak main() w module testowanym, własny main()
# w test_*.c. Moduły firmware'owe dostają NULL_main przez makro.

# Lista testów - każdy = osobny plik wykonywalny
TESTS = \
    test_protocol_crc \
    test_protocol_framing \
    test_protocol_queue \
    test_protocol_dispatch \
    test_adc_filter \
    test_buttons \
    test_leds \
    test_vu_bar

# Sprzęt stuby - moduł testu + moduły firmware których potrzebuje + mocki
# Konkretne zależności per test deklarujemy poniżej.

.PHONY: all clean test
all: test

# Buduje wszystkie testy i uruchamia sekwencyjnie.
test: test_protocol_crc test_protocol_framing test_protocol_queue \
       test_protocol_dispatch test_adc_filter test_buttons test_leds \
       test_vu_bar
	@echo ""
	@echo "=========================="
	@echo "=== ALL FIRMWARE TESTS ==="
	@echo "=========================="

# === Test 1: CRC ===
test_protocol_crc: test_protocol_crc.c \
                    $(SRC_DIR)/protocol.c \
                    $(SRC_DIR)/leds.c \
                    $(SRC_DIR)/adc.c \
                    $(SRC_DIR)/board.c \
                    $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_protocol_crc

# === Test 2: Framing ===
test_protocol_framing: test_protocol_framing.c \
                        $(SRC_DIR)/protocol.c \
                        $(SRC_DIR)/leds.c \
                        $(SRC_DIR)/adc.c \
                        $(SRC_DIR)/board.c \
                        $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_protocol_framing

# === Test 3: Queue ===
test_protocol_queue: test_protocol_queue.c \
                      $(SRC_DIR)/protocol.c \
                      $(SRC_DIR)/leds.c \
                      $(SRC_DIR)/adc.c \
                      $(SRC_DIR)/board.c \
                      $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_protocol_queue

# === Test 4: Dispatch ===
# Potrzebuje leds.c (dispatch woła led_set) i buttons.c (emit_button z drop)
test_protocol_dispatch: test_protocol_dispatch.c \
                        $(SRC_DIR)/protocol.c \
                        $(SRC_DIR)/leds.c \
                        $(SRC_DIR)/adc.c \
                        $(SRC_DIR)/board.c \
                        $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_protocol_dispatch

# === Test 5: ADC filter (EMA Q8) ===
test_adc_filter: test_adc_filter.c \
                 $(SRC_DIR)/adc.c \
                 $(SRC_DIR)/protocol.c \
                 $(SRC_DIR)/leds.c \
                 $(SRC_DIR)/board.c \
                 $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_adc_filter

# === Test 6: Buttons debounce ===
test_buttons: test_buttons.c \
              $(SRC_DIR)/buttons.c \
              $(SRC_DIR)/protocol.c \
              $(SRC_DIR)/leds.c \
              $(SRC_DIR)/adc.c \
              $(SRC_DIR)/board.c \
              $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_buttons

# === Test 7: LEDs ===
# scheduler_millis() pochodzi z mock_state.c (scheduler.c ma referencje do
# innych modułów których tu nie potrzebujemy).
test_leds: test_leds.c \
           $(SRC_DIR)/leds.c \
           $(SRC_DIR)/board.c \
           $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_leds

# === Test 8: VU bar (V2) ===
test_vu_bar: test_vu_bar.c \
             $(SRC_DIR)/leds.c \
             $(SRC_DIR)/board.c \
             $(MOCK_SRC)
	@echo "[BUILD] $@"
	@$(CC) $(CFLAGS) -o $@ $^ 2>&1
	@echo "[RUN]   $@"
	@./$@ && echo "  ✓ $@ PASS" || (echo "  ✗ $@ FAIL"; exit 1)
.PHONY: test_vu_bar

clean:
	rm -f test_protocol_crc test_protocol_framing test_protocol_queue \
	      test_protocol_dispatch test_adc_filter test_buttons test_leds \
	      test_vu_bar
