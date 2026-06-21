# =============================================================================
#  GREJEM OS - Firmware Makefile
#  Target  : STM32F103C6T6  (Cortex-M3, 32 KB Flash, 10 KB RAM, 72 MHz HSE)
#  Toolchain: arm-none-eabi-gcc
#  Library : libopencm3 (bare-metal)
# =============================================================================
#
# Wymagania:
#   1) arm-none-eabi-gcc >= 10.x   (apt install gcc-arm-none-eabi)
#   2) libopencm3 sklonowany i zbudowany:
#        git clone https://github.com/libopencm3/libopencm3.git ~/src/libopencm3
#        cd ~/src/libopencm3 && make TARGET=stm32/f1
#   3) Opcjonalnie stlink-tools / openocd do flashowania.
#
# Budowanie:
#   make                -> build/grejem-fw.{elf,bin,hex}
#   make flash          -> wgrywa .bin przez st-flash
#   make ocdflash       -> wgrywa .elf przez openocd + stlink
#   make dfuflash       -> wgrywa .bin przez USB DFU (wbudowany bootloader ROM)
#   make clean
# =============================================================================

# ---- Plik wynikowy ----
TARGET      := grejem-fw
BUILD_DIR   := build

# ---- Toolchain ----
PREFIX      ?= arm-none-eabi-
CC          := $(PREFIX)gcc
CXX         := $(PREFIX)g++
LD          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
OBJDUMP     := $(PREFIX)objdump
SIZE        := $(PREFIX)size

# ---- libopencm3 ----
# Ustaw przez env:  export OPENCM3_DIR=$HOME/src/libopencm3
OPENCM3_DIR ?= $(HOME)/src/libopencm3

# ---- Linker script (własny, samowystarczalny) ----
LDSCRIPT    := ld/stm32f103x6.ld

# ---- Flagi kompilacji ----
ARCHFLAGS   := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

CFLAGS      := -Os -g3 -std=c11 -Wall -Wextra -Wno-unused-parameter
CFLAGS      += $(ARCHFLAGS)
CFLAGS      += -ffunction-sections -fdata-sections
CFLAGS      += -fno-common -fno-builtin-printf
CFLAGS      += -Iinclude -I$(OPENCM3_DIR)/include
CFLAGS      += -DSTM32F1 -DSTM32F103C6
CFLAGS      += $(EXTRA_CFLAGS)

# ---- Flagi linkowania ----
LDFLAGS     := $(ARCHFLAGS)
LDFLAGS     += -T$(LDSCRIPT) -nostartfiles
LDFLAGS     += -Wl,--gc-sections -Wl,--cref
LDFLAGS     += -Wl,--Map=$(BUILD_DIR)/$(TARGET).map
LDFLAGS     += -Wl,--print-memory-usage
LDFLAGS     += -Wl,--build-id=none

LDLIBS      := -L$(OPENCM3_DIR)/lib -lopencm3_stm32f1
LDLIBS      += -lc -lgcc -lnosys

# ---- Sources ----
C_SOURCES   := $(wildcard src/*.c)
OBJS        := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

# =============================================================================

.PHONY: all clean flash ocdflash dfuflash dump size libcheck

all: size

$(BUILD_DIR):
	@mkdir -p $@

# Libopencm3 sanity check
libcheck:
	@test -f $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a || { \
		echo ""; \
		echo "  [!] Nie znaleziono libopencm3_stm32f1.a w $(OPENCM3_DIR)/lib"; \
		echo "      Zbuduj bibliotekę:  cd $(OPENCM3_DIR) && make TARGET=stm32/f1"; \
		echo ""; \
		exit 1; }

$(BUILD_DIR)/%.o: src/%.c include/*.h | $(BUILD_DIR) libcheck
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) | $(BUILD_DIR) libcheck
	@echo "  LD  $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O ihex $< $@

size: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex
	@echo ""
	@echo "  ==== Memory usage ===="
	@$(SIZE) $(BUILD_DIR)/$(TARGET).elf
	@echo ""

dump: $(BUILD_DIR)/$(TARGET).elf
	@$(OBJDUMP) -D $< > $(BUILD_DIR)/$(TARGET).lst
	@echo "  Listing: $(BUILD_DIR)/$(TARGET).lst"

clean:
	@rm -rf $(BUILD_DIR)
	@echo "  cleaned."

# ---- Flashowanie ----
# Opcja A: st-flash (stlink-tools)
flash: $(BUILD_DIR)/$(TARGET).bin
	st-flash write $< 0x08000000

# Opcja B: openocd (uniwersalne - ST-Link v2/v3, J-Link itp.)
ocdflash: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg \
	        -f target/stm32f1x.cfg \
	        -c "program $< verify reset exit"

# Opcja C: USB DFU (wbudowany bootloader ROM w STM32F103, bez ST-Linka).
# Wymaga:
#   1. BOOT0 = 1 (zworka na Blue Pill) -> start z System Memory
#   2. Podłączony USB-C -> urządzenie widoczne jako 0483:df11 (ST DFU)
#   3. Po flashu: BOOT0 = 0 + reset -> uruchamia się user firmware
# Sprawdzenie: lsusb | grep 0483:df11
dfuflash: $(BUILD_DIR)/$(TARGET).bin
	@echo "  [DFU] Sprawdzam STM32CubeProgrammer..."
	@command -v STM32_Programmer_CLI >/dev/null 2>&1 || { \
	    echo ""; \
	    echo "  [!] Nie znaleziono STM32_Programmer_CLI w PATH."; \
	    echo "      Alternatywa bez CubeCLT:"; \
	    echo "        sudo apt install dfu-util"; \
	    echo "        dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D build/$(TARGET).bin"; \
	    echo ""; \
	    exit 1; }
	@echo "  [DFU] Szukam urządzenia DFU (0483:df11)..."
	@lsusb | grep -q 0483:df11 || { \
	    echo ""; \
	    echo "  [!] Nie widzę STM32 w trybie DFU (0483:df11)."; \
	    echo "      1. Ustaw zworkę BOOT0 = 1 (na 3V3)"; \
	    echo "      2. Odłącz i podłącz ponownie USB-C"; \
	    echo "      3. lsusb | grep 0483:df11  (powinno pokazać urządzenie)"; \
	    echo ""; \
	    exit 1; }
	@echo "  [DFU] Wgrywanie $< ..."
	STM32_Programmer_CLI -c port=usb -d $< 0x08000000 -hardRst
	@echo ""
	@echo "  [DFU] OK. Ustaw zworkę BOOT0 = 0 i wciśnij RESET (lub odłącz/podłącz USB)."
