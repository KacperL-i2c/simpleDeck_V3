#!/usr/bin/env bash
# =============================================================================
#  dfu-enter.sh — pomocnik do flashowania STM32F103 przez USB DFU
#
#  Wbudowany bootloader DFU w STM32F103 (ROM pod 0x1FFFF800) pozwala wgrywać
#  firmware przez USB-C bez programatora ST-Link. Wymaga tylko zworki BOOT0.
#
#  Użycie:
#     ./tools/dfu-enter.sh            # buduje (jeśli trzeba) i flashuje
#     ./tools/dfu-enter.sh --no-build # pomiń make, użyj istniejącego .bin
#     ./tools/dfu-enter.sh --check    # tylko sprawdź czy DFU widoczne
#
#  Alternatywa (bez tego skryptu):
#     make dfuflash
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${FW_DIR}/build/grejem-fw.bin"

DO_BUILD=1
CHECK_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        --check)    CHECK_ONLY=1 ;;
        -h|--help)
            sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Nieznany argument: $arg (spróbuj --help)" >&2
            exit 2
            ;;
    esac
done

echo "=== GREJEM OS / DFU flasher ==="
echo ""

# ---- 1. Sprawdź DFU device na USB ----
if ! lsusb 2>/dev/null | grep -q 0483:df11; then
    echo "[!] STM32 nie jest w trybie DFU (brak 0483:df11 na lsusb)."
    echo ""
    echo "Krok po kroku:"
    echo "  1. Odłącz USB-C od płytki"
    echo "  2. Przestaw zworkę BOOT0 na pozycję 1 (do 3V3)"
    echo "     (na Blue Pill: jumper między środkowym pinem a 3V3)"
    echo "  3. Podłącz USB-C ponownie"
    echo "  4. Sprawdź:  lsusb | grep 0483:df11"
    echo ""
    echo "Po flashu: BOOT0 z powrotem na 0, reset — ruszy user firmware."
    exit 1
fi
echo "[OK] STM32 widoczne w trybie DFU."
lsusb | grep 0483:df11

if [ "$CHECK_ONLY" = "1" ]; then
    echo ""
    echo "[--check] Urządzenie gotowe do flashowania. Koniec."
    exit 0
fi

# ---- 2. Sprawdź tool ----
if ! command -v STM32_Programmer_CLI >/dev/null 2>&1; then
    echo ""
    echo "[!] Brak STM32_Programmer_CLI w PATH."
    echo "    Alternatywa:  sudo apt install dfu-util"
    echo "    dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D ${BIN}"
    exit 1
fi

# ---- 3. Buduj jeśli trzeba ----
if [ "$DO_BUILD" = "1" ] || [ ! -f "$BIN" ]; then
    echo ""
    echo "[build] cd ${FW_DIR} && make"
    make -C "$FW_DIR"
fi

if [ ! -f "$BIN" ]; then
    echo "[!] Brak ${BIN} po buildzie." >&2
    exit 1
fi

# ---- 4. Flash ----
echo ""
echo "[flash] STM32_Programmer_CLI -c port=usb -d ${BIN} 0x08000000 -hardRst"
STM32_Programmer_CLI -c port=usb -d "$BIN" 0x08000000 -hardRst

echo ""
echo "=== Gotowe ==="
echo "Przestaw zworkę BOOT0 na 0 i wciśnij RESET (lub odłącz/podłącz USB)."
echo "Urządzenie powinno pojawić się jako 1209:de10:"
echo "  lsusb | grep 1209:de10"
