#!/usr/bin/env bash
# ============================================================================
#  Simple Deck - uruchamianie deweloperskie (Linux / macOS / WSL)
#  ----------------------------------------------------------------------------
#  Jedna komenda zrobi wszystko:
#    1) stworzy .venv jeśli brakuje (jednorazowo, ~3 s)
#    2) zainstaluje zależności z pyproject.toml (jednorazowo, ~60 s)
#    3) uruchomi aplikację w trybie normalnym lub --demo
#
#  Użycie:
#    ./run.sh                # normalny start (łączy z MCU)
#    ./run.sh --demo         # tryb demo (bez urządzenia, do testów UI)
#    ./run.sh --verbose      # debug logging
#    ./run.sh --help
#
#  Idempotentny: drugie uruchomienie pomija setup i startuje w ~1 s.
# ============================================================================
set -euo pipefail

# D7 fix: przenośny resolver ścieżki skryptu (bez readlink -f które jest GNU-only).
# macOS BSD readlink nie wspiera -f. Ten sed-zamiast-readlink działa wszędzie.
script_path="$0"
while [ -L "$script_path" ]; do
    # Idź za symlinkami aż do fizycznego pliku
    dir="$(cd "$(dirname "$script_path")" >/dev/null 2>&1 && pwd)"
    link="$(readlink "$script_path")"
    case "$link" in
        /*) script_path="$link" ;;
        *)  script_path="$dir/$link" ;;
    esac
done
cd "$(dirname "$script_path")"

VENV=".venv"
PYTHON="$VENV/bin/python"
PIP="$VENV/bin/pip"

# ---- Help ----
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'EOF'
Simple Deck - uruchamianie

Użycie:
  ./run.sh                # normalny start (łączy z MCU)
  ./run.sh --demo         # tryb demo (bez urządzenia)
  ./run.sh --verbose      # debug logging
  ./run.sh --help         # ta pomoc

Flagi są przekazywane do aplikacji, więc można je łączyć:
  ./run.sh --demo --verbose
EOF
    exit 0
fi

# ---- Krok 1: venv (jednorazowo) ----
if [ ! -d "$VENV" ]; then
    echo "[setup] Tworzenie virtualenv (.venv/)..."
    if ! command -v python3 >/dev/null 2>&1; then
        echo "BŁĄD: python3 nie znaleziony. Zainstaluj Python 3.10+." >&2
        exit 1
    fi
    PYVER=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
    if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)'; then
        echo "BŁĄD: python3 >= 3.10 wymagany (mam $PYVER)" >&2
        exit 1
    fi
    echo "[setup]   python: $PYVER"
    python3 -m venv "$VENV"
    "$PIP" install --quiet --upgrade pip wheel setuptools
    echo "[setup]   venv gotowy"
fi

# ---- Krok 2: instalacja zależności (idempotentnie) ----
# Szybki check: czy PySide6 i hid (moduł Pythona z pakietu hidapi) są już w venv?
# UWAGA: pip package = "hidapi", ale import = "import hid"
if ! "$PYTHON" -c "import PySide6, hid" 2>/dev/null; then
    echo "[setup] Instalacja zależności (jednorazowo, potrwa ~60 s)..."

    # Wykryj system: Linux dostaje extras (pulsectl, python-xlib),
    # macOS/WSL - bez extras (extras są Linux-only).
    case "$(uname -s)" in
        Linux*)
            echo "[setup]   wykryto Linux → instalacja z extras [linux]"
            "$PIP" install -e ".[linux]"
            ;;
        Darwin*)
            echo "[setup]   wykryto macOS → instalacja bez extras (brak WASAPI/PulseAudio)"
            "$PIP" install -e .
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "[setup]   wykryto Windows (Git Bash) → uruchom run.bat zamiast run.sh"
            exit 1
            ;;
        *)
            echo "[setup]   nieznany system $(uname -s) → instalacja bez extras"
            "$PIP" install -e .
            ;;
    esac
    echo "[setup]   zależności gotowe"
fi

# ---- Krok 3: uruchom aplikację ----
echo "[run] Simple Deck - start"
exec "$PYTHON" -m simple_deck "$@"
