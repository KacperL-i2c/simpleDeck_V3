#!/bin/sh
# ============================================================================
#  Simple Deck - thin wrapper uruchamiający deinstalator.
#  Przekazuje wszystkie argumenty do install.sh (np. --purge-profiles).
#  Utworzone dla wygodnej odkrywalności (obok install.sh w installer/linux/).
# ----------------------------------------------------------------------------
#  Użycie:
#    ./uninstall.sh                       # odinstaluj (zachowuje profile)
#    ./uninstall.sh --purge-profiles      # odinstaluj + usuń profile
# ============================================================================
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
INSTALL_SH="$HERE/install.sh"
if [ ! -f "$INSTALL_SH" ]; then
    echo "Nie znaleziono install.sh obok uninstall.sh ($HERE)" >&2
    exit 1
fi
exec sh "$INSTALL_SH" --uninstall "$@"
