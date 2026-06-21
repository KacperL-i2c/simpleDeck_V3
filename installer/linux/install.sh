#!/usr/bin/env bash
# ============================================================================
#  Simple Deck - instalator Linuksa
#  ----------------------------------------------------------------------------
#  Jedna komenda zainstaluje aplikację na stałe w Twoim $HOME:
#    1) zainstaluje pakiety systemowe (python3-venv, xdotool, libxkbcommon…)
#    2) utworzy izolowany venv w ~/.local/share/simple-deck/venv
#    3) zainstaluje PySide6 + hidapi + pulsectl + simple-deck w venv
#    4) utworzy skrót w menu aplikacji (.desktop)
#    5) zainstaluje ikony
#    6) zapyta o regułę udev (dostęp do urządzenia bez sudo)
#
#  Działa na: Debian, Ubuntu, Fedora, Arch, OpenSUSE, Mint, Pop!_OS, Manjaro
#
#  Użycie:
#    ./install.sh                # instalacja do ~/.local (bez sudo, pyta o udev)
#    ./install.sh --uninstall    # usuwa wszystko (pyta o udev)
#    sudo ./install.sh --udev    # instaluje TYLKO regułę udev
#    ./install.sh --help
#
#  Dlaczego nie .deb / AppImage?
#    - .deb wymaga poprawnych zależności apt, których często nie ma (pulsectl,
#      python3-hidapi o złej nazwie, PEP 668 blokuje pip install --user)
#    - AppImage wymaga pobierania Python.AppImage + appimagetool z sieci
#    - ten skrypt jest samowystarczalny: potrzebuje tylko python3 i apt/dnf/pacman
# ============================================================================
set -euo pipefail

# ============================================================================
#  Konfiguracja
# ============================================================================
APP_NAME="Simple Deck"
APP_PRETTY="Simple Deck"
APP_SLUG="simple-deck"
APP_VENDOR="GREJEM INDUSTRIES"
VERSION="1.0.0"

# Ścieżki z XDG (z fallback do ~/.local)
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_DATA_DIRS_TEST="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"

INSTALL_PREFIX="$XDG_DATA_HOME/$APP_SLUG"
VENV_DIR="$INSTALL_PREFIX/venv"
LAUNCHER_BIN="$XDG_BIN_HOME/$APP_SLUG"
DESKTOP_FILE="$XDG_DATA_HOME/applications/$APP_SLUG.desktop"
ICONS_BASE="$XDG_DATA_HOME/icons/hicolor"

# Źródła (katalogi względem tego skryptu)
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC_ROOT="$(cd "$HERE/../.." && pwd)"
DESKTOP_SRC="$SRC_ROOT/desktop"
ICONS_SRC="$SRC_ROOT/installer/icons"
UDEV_RULE_SRC="$HERE/udev/99-simple-deck.rules"
UDEV_RULE_DST="/etc/udev/rules.d/99-simple-deck.rules"
# Legacy udev rule (z poprzedniej nazwy grejem-os) — usuwany przy install/uninstall.
UDEV_RULE_LEGACY="/etc/udev/rules.d/99-grejem-streamdeck.rules"

# Kolorki (jeśli terminal je obsługuje)
if [[ -t 1 ]]; then
    C_RESET=$'\e[0m'
    C_BOLD=$'\e[1m'
    C_CYAN=$'\e[36m'
    C_GREEN=$'\e[32m'
    C_YELLOW=$'\e[33m'
    C_RED=$'\e[31m'
    C_DIM=$'\e[2m'
else
    C_RESET=""; C_BOLD=""; C_CYAN=""; C_GREEN=""; C_YELLOW=""; C_RED=""; C_DIM=""
fi

# ============================================================================
#  Helpery
# ============================================================================
log()  { echo "${C_BOLD}${C_CYAN}▶${C_RESET} $*"; }
ok()   { echo "${C_BOLD}${C_GREEN}✓${C_RESET} $*"; }
warn() { echo "${C_BOLD}${C_YELLOW}!${C_RESET} $*" >&2; }
err()  { echo "${C_BOLD}${C_RED}✗${C_RESET} $*" >&2; }
step() { echo ""; echo "${C_BOLD}${C_CYAN}═══ $* ═══${C_RESET}"; }

die() { err "$*"; exit 1; }

# ============================================================================
#  Detekcja dystrybucji
# ============================================================================
detect_package_manager() {
    if [[ ! -f /etc/os-release ]]; then
        echo "unknown"
        return
    fi
    . /etc/os-release
    local id="${ID:-}"
    local id_like="${ID_LIKE:-}"
    case "$id:$id_like" in
        *ubuntu*|*debian*|*mint*|*pop*|*kali*|*raspbian*) echo "apt" ;;
        *fedora*|*rhel*|*centos*|*rocky*|*alma*|*amzn*)    echo "dnf" ;;
        *arch*|*manjaro*|*endeavouros*|*garuda*)           echo "pacman" ;;
        *opensuse*|*suse*|*sles*)                          echo "zypper" ;;
        *) echo "unknown" ;;
    esac
}

# ============================================================================
#  Instalacja pakietów systemowych (wymaga sudo)
# ============================================================================
install_system_packages() {
    local pm
    pm=$(detect_package_manager)
    log "Wykryto menedżer pakietów: ${C_BOLD}$pm${C_RESET}"

    local apt_pkgs=(python3 python3-venv python3-pip xdotool
                    libxkbcommon0 libxcb-cursor0 libnss3 libxcb-xinerama0
                    libgl1 libegl1 libdbus-1-3)
    local dnf_pkgs=(python3 python3-pip xdotool
                    libxkbcommon xcb-util-cursor nss
                    mesa-libGL mesa-libEGL dbus-libs)
    local pacman_pkgs=(python python-pip xdotool
                       libxkbcommon nss
                       mesa libglvnd dbus)
    local zypper_pkgs=(python3 python3-pip xdotool
                       libxkbcommon0 libxcb-cursor0 nss
                       Mesa-libGL1 libdbus-1-3)

    local pkgs
    case "$pm" in
        apt)     pkgs=("${apt_pkgs[@]}") ;;
        dnf)     pkgs=("${dnf_pkgs[@]}") ;;
        pacman)  pkgs=("${pacman_pkgs[@]}") ;;
        zypper)  pkgs=("${zypper_pkgs[@]}") ;;
        unknown)
            warn "Nieznana dystrybucja - pomijam instalację pakietów systemowych."
            warn "Upewnij się ręcznie że masz: python3 (≥3.10), python3-venv, xdotool,"
            warn "libxkbcommon, nss, mesa (OpenGL/EGL)."
            return 0
            ;;
    esac

    # Sprawdź co już jest zainstalowane i zainstaluj tylko brakujące
    local missing=()
    case "$pm" in
        apt)
            for p in "${pkgs[@]}"; do
                if ! dpkg -s "$p" >/dev/null 2>&1; then
                    missing+=("$p")
                fi
            done
            if [[ ${#missing[@]} -gt 0 ]]; then
                log "Instalacja ${#missing[@]} pakietów przez apt (wymaga sudo)…"
                sudo apt-get update -qq
                sudo apt-get install -y -qq "${missing[@]}"
            else
                ok "Wszystkie pakiety systemowe już zainstalowane"
            fi
            ;;
        dnf)
            for p in "${pkgs[@]}"; do
                if ! dnf list installed "$p" >/dev/null 2>&1; then
                    missing+=("$p")
                fi
            done
            if [[ ${#missing[@]} -gt 0 ]]; then
                log "Instalacja ${#missing[@]} pakietów przez dnf (wymaga sudo)…"
                sudo dnf install -y -q "${missing[@]}"
            else
                ok "Wszystkie pakiety systemowe już zainstalowane"
            fi
            ;;
        pacman)
            for p in "${pkgs[@]}"; do
                if ! pacman -Qi "$p" >/dev/null 2>&1; then
                    missing+=("$p")
                fi
            done
            if [[ ${#missing[@]} -gt 0 ]]; then
                log "Instalacja ${#missing[@]} pakietów przez pacman (wymaga sudo)…"
                sudo pacman -S --noconfirm --needed --quiet "${missing[@]}"
            else
                ok "Wszystkie pakiety systemowe już zainstalowane"
            fi
            ;;
        zypper)
            # D2 fix: LC_ALL=C wymusza angielski output (inaczej polskie
            # "Zainstalowane: Tak" nie matchuje grep "^Installed.*Yes").
            for p in "${pkgs[@]}"; do
                if ! LC_ALL=C zypper --no-refresh info "$p" 2>/dev/null | grep -qi "^Installed.*Yes"; then
                    missing+=("$p")
                fi
            done
            if [[ ${#missing[@]} -gt 0 ]]; then
                log "Instalacja ${#missing[@]} pakietów przez zypper (wymaga sudo)…"
                sudo zypper --non-interactive --quiet install "${missing[@]}"
            else
                ok "Wszystkie pakiety systemowe już zainstalowane"
            fi
            ;;
    esac
}

# ============================================================================
#  Venv + Python deps
# ============================================================================
create_venv() {
    if [[ -d "$VENV_DIR" && -x "$VENV_DIR/bin/python" ]]; then
        log "Venv już istnieje: $VENV_DIR"
        return
    fi
    log "Tworzenie venv w $VENV_DIR…"
    mkdir -p "$INSTALL_PREFIX"
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install --quiet --upgrade pip wheel setuptools
}

install_python_deps() {
    # Idempotentnie: jeśli simple_deck + PySide6 + hid są importowalne - pomijaj
    # UWAGA: pip package "hidapi" importuje się jako moduł `hid`
    if "$VENV_DIR/bin/python" -c "import simple_deck, PySide6, hid" 2>/dev/null; then
        ok "Zależności Pythona już zainstalowane"
        return
    fi
    log "Instalacja PySide6 + hidapi + pulsectl + python-xlib (potrwa ~60 s)…"
    "$VENV_DIR/bin/pip" install --quiet PySide6 hidapi pulsectl python-xlib

    log "Instalacja simple-deck ze źródeł ($DESKTOP_SRC)…"
    "$VENV_DIR/bin/pip" install --quiet -e "$DESKTOP_SRC"
}

# ============================================================================
#  Launcher / .desktop / ikony
# ============================================================================
install_launcher() {
    log "Tworzenie launchera: $LAUNCHER_BIN"
    mkdir -p "$(dirname "$LAUNCHER_BIN")"
    cat > "$LAUNCHER_BIN" <<EOF
#!/bin/sh
# Auto-generated by Simple Deck installer - do not edit
exec "$VENV_DIR/bin/python" -m simple_deck "\$@"
EOF
    chmod +x "$LAUNCHER_BIN"
}

install_desktop_file() {
    log "Tworzenie skrótu: $DESKTOP_FILE"
    mkdir -p "$(dirname "$DESKTOP_FILE")"
    cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Simple Deck
GenericName=Stream Deck Controller
GenericName[pl]=Kontroler Stream Deck
Comment=Control application for the GREJEM Stream Deck hardware device
Comment[pl]=Aplikacja kontrolna dla urządzenia GREJEM Stream Deck
# D4 fix: quotowanie ścieżki w Exec= (gdy $HOME ma spację np. "/home/Jan K")
Exec="$LAUNCHER_BIN" %U
Icon=$APP_SLUG
Terminal=false
Categories=Utility;Settings;HardwareSettings;
Keywords=streamdeck;hid;usb;macro;controller;
StartupWMClass=Simple Deck
StartupNotify=true
EOF
}

install_icons() {
    log "Instalacja ikon…"
    # SVG (skalowalna)
    mkdir -p "$ICONS_BASE/scalable/apps"
    if [[ -f "$ICONS_SRC/simple_deck.svg" ]]; then
        cp -f "$ICONS_SRC/simple_deck.svg" "$ICONS_BASE/scalable/apps/$APP_SLUG.svg"
    fi
    # PNG w różnych rozmiarach
    for size in 16 32 48 64 128 256; do
        local src="$ICONS_SRC/simple_deck_${size}.png"
        if [[ -f "$src" ]]; then
            mkdir -p "$ICONS_BASE/${size}x${size}/apps"
            cp -f "$src" "$ICONS_BASE/${size}x${size}/apps/$APP_SLUG.png"
        fi
    done
}

refresh_caches() {
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q "$(dirname "$DESKTOP_FILE")" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f "$ICONS_BASE" 2>/dev/null || true
    fi
}

# ============================================================================
#  Reguła udev (interaktywna - wymaga sudo)
# ============================================================================
ask_install_udev() {
    if [[ ! -f "$UDEV_RULE_SRC" ]]; then
        warn "Plik reguły udev nie istnieje: $UDEV_RULE_SRC"
        return
    fi

    if [[ -f "$UDEV_RULE_DST" ]]; then
        ok "Reguła udev już zainstalowana: $UDEV_RULE_DST"
        # Ale może istnieć jeszcze legacy reguła sprzed rename — usuń ją.
        if [[ -f "$UDEV_RULE_LEGACY" ]]; then
            log "Usuwanie legacy reguły udev: $UDEV_RULE_LEGACY (wymaga sudo)…"
            sudo rm -f "$UDEV_RULE_LEGACY" 2>/dev/null || true
            sudo udevadm control --reload-rules 2>/dev/null || true
        fi
        return
    fi

    echo ""
    echo "${C_BOLD}Reguła udev${C_RESET} (dostęp do urządzenia bez sudo):"
    echo "  Aby aplikacja mogła komunikować się z urządzeniem USB-C bez"
    echo "  uprawnień roota, wymagana jest reguła udev w $UDEV_RULE_DST"
    echo ""
    printf "  ${C_BOLD}Zainstalować teraz?${C_RESET} [T/n] "
    # D1 fix: 'read -r ans || ans=""' chroni przed EOF (</dev/null, curl|bash)
    read -r ans || ans=""
    if [[ "${ans:-T}" =~ ^[TtYy].* || -z "$ans" ]]; then
        log "Instalacja reguły udev (wymaga sudo)…"
        sudo cp -f "$UDEV_RULE_SRC" "$UDEV_RULE_DST"
        sudo chmod 0644 "$UDEV_RULE_DST"
        # Usuń legacy regułę sprzed rename (grejem-streamdeck → simple-deck).
        if [[ -f "$UDEV_RULE_LEGACY" ]]; then
            sudo rm -f "$UDEV_RULE_LEGACY"
        fi
        sudo udevadm control --reload-rules 2>/dev/null || true
        sudo udevadm trigger --subsystem-match=usb --attr-match="idVendor=1209" \
                                          --attr-match="idProduct=de10" 2>/dev/null || true
        sudo udevadm trigger --subsystem-match=hidraw 2>/dev/null || true
        ok "Reguła udev zainstalowana"
    else
        warn "Pominięto. Aby zainstalować później:"
        warn "  sudo $HERE/install.sh --udev"
    fi
}

ask_remove_udev() {
    local removed_any=0
    # Usuń oba: nową regułę (simple-deck) i legacy (grejem-streamdeck).
    for rule in "$UDEV_RULE_DST" "$UDEV_RULE_LEGACY"; do
        if [[ ! -f "$rule" ]]; then
            continue
        fi
        if [[ $removed_any -eq 0 ]]; then
            echo ""
            printf "  ${C_BOLD}Usunąć regułę udev?${C_RESET} [T/n] "
            # D1 fix: 'read -r ans || ans=""' chroni przed EOF
            read -r ans || ans=""
            if [[ ! "${ans:-T}" =~ ^[TtYy].* && ! -z "$ans" ]]; then
                warn "Pominięto usuwanie reguł udev."
                return
            fi
        fi
        log "Usuwanie reguły udev (wymaga sudo): $rule"
        sudo rm -f "$rule"
        removed_any=1
    done
    if [[ $removed_any -eq 1 ]]; then
        sudo udevadm control --reload-rules 2>/dev/null || true
        ok "Reguła/y udev usunięte"
    fi
}

# ============================================================================
#  Legacy cleanup — usuwa resztki instalacji sprzed zmiany nazwy (grejem-os → simple-deck)
# ============================================================================
cleanup_legacy_grejem_install() {
    local legacy_prefix="$XDG_DATA_HOME/grejem-os"
    local legacy_launcher="$XDG_BIN_HOME/grejem-os"
    local legacy_desktop="$XDG_DATA_HOME/applications/grejem-os.desktop"
    local legacy_config="$HOME/.config/grejem-os"
    local legacy_socket="$HOME/.config/simple-deck/grejem-os-raise"
    local legacy_lock="$HOME/.config/simple-deck/grejem-os.lock"
    local found=0

    # Stary install dir (venv ~150 MB)
    if [[ -d "$legacy_prefix" ]]; then
        log "Czyszczenie legacy install: $legacy_prefix"
        rm -rf "$legacy_prefix"
        found=1
    fi
    # Stary launcher
    if [[ -f "$legacy_launcher" ]]; then
        log "Czyszczenie legacy launchera: $legacy_launcher"
        rm -f "$legacy_launcher"
        found=1
    fi
    # Stary .desktop
    if [[ -f "$legacy_desktop" ]]; then
        log "Czyszczenie legacy .desktop: $legacy_desktop"
        rm -f "$legacy_desktop"
        found=1
    fi
    # Stare ikony (pod nazwą grejem-os)
    if [[ -d "$ICONS_BASE" ]]; then
        local legacy_icons=(
            "$ICONS_BASE/scalable/apps/grejem-os.svg"
            "$ICONS_BASE/16x16/apps/grejem-os.png"
            "$ICONS_BASE/32x32/apps/grejem-os.png"
            "$ICONS_BASE/48x48/apps/grejem-os.png"
            "$ICONS_BASE/64x64/apps/grejem-os.png"
            "$ICONS_BASE/128x128/apps/grejem-os.png"
            "$ICONS_BASE/256x256/apps/grejem-os.png"
        )
        for ic in "${legacy_icons[@]}"; do
            if [[ -f "$ic" ]]; then
                rm -f "$ic"
                found=1
            fi
        done
    fi
    # Stary katalog konfiguracji (migracja danych już wykonana przez aplikację;
    # grejem-os/ zawiera co najwyżej pusty profiles/ + stary settings.json).
    if [[ -d "$legacy_config" ]]; then
        log "Czyszczenie legacy config: $legacy_config"
        rm -rf "$legacy_config"
        found=1
    fi
    # Stale lockfile/socket w nowym config dir (nazwy sprzed rename)
    for stale in "$legacy_socket" "$legacy_lock"; do
        if [[ -e "$stale" ]]; then
            rm -f "$stale"
            found=1
        fi
    done
    # Legacy udev rule (wymaga sudo — tylko jeśli jest i user zgodzi się w ask_install_udev).
    # Tutaj usuwamy bez pytania bo ask_install_udev już o to pyta / installuje nową.

    if [[ $found -eq 1 ]]; then
        ok "Legacy instalacja grejem-os wyczyszczona"
    fi
}

# ============================================================================
#  Akcje główne
# ============================================================================
do_install() {
    echo ""
    echo "${C_BOLD}${C_CYAN}╔══════════════════════════════════════════════════════════════╗${C_RESET}"
    echo "${C_BOLD}${C_CYAN}║   Simple Deck  -  instalacja ($VERSION)                      ║${C_RESET}"
    echo "${C_BOLD}${C_CYAN}║   by GREJEM INDUSTRIES                                       ║${C_RESET}"
    echo "${C_BOLD}${C_CYAN}╚══════════════════════════════════════════════════════════════╝${C_RESET}"
    echo ""
    echo "  ${C_DIM}Tryb:${C_RESET}         user (instalacja do $INSTALL_PREFIX)"
    echo "  ${C_DIM}Launcher:${C_RESET}     $LAUNCHER_BIN"
    echo "  ${C_DIM}Skrót:${C_RESET}        $DESKTOP_FILE"
    echo ""

    # Sanity
    command -v python3 >/dev/null 2>&1 || die "python3 nie znaleziony"
    [[ -d "$DESKTOP_SRC" ]] || die "Nie znaleziono katalogu desktop/ pod $DESKTOP_SRC"
    [[ -f "$DESKTOP_SRC/pyproject.toml" ]] || die "Brak pyproject.toml w $DESKTOP_SRC"

    step "1/6  Pakiety systemowe"
    install_system_packages

    step "2/6  Virtualenv"
    create_venv

    step "3/6  Zależności Pythona"
    install_python_deps

    step "4/6  Launcher + skrót + ikony"
    install_launcher
    install_desktop_file
    install_icons

    step "5/6  Cache odświeżenie"
    refresh_caches
    ok "Cache zaktualizowany"

    step "6/6  Reguła udev"
    ask_install_udev

    # Cleanup legacy instalacji sprzed zmiany nazwy (grejem-os → simple-deck).
    # Usuwa stary venv (~150 MB), launcher, .desktop, ikony, stary config dir,
    # i stale lockfile/socket. Nie psuje nowej instalacji.
    cleanup_legacy_grejem_install

    # Podsumowanie
    echo ""
    echo "${C_BOLD}${C_GREEN}╔══════════════════════════════════════════════════════════════╗${C_RESET}"
    echo "${C_BOLD}${C_GREEN}║   Simple Deck zainstalowany pomyślnie! ✓                     ║${C_RESET}"
    echo "${C_BOLD}${C_GREEN}╚══════════════════════════════════════════════════════════════╝${C_RESET}"
    echo ""
    echo "  ${C_BOLD}Uruchom:${C_RESET}"
    echo "    • Z menu aplikacji (szukaj: \"Simple Deck\")"
    echo "    • Z terminala:  ${C_CYAN}$LAUNCHER_BIN${C_RESET}"
    echo ""
    echo "  ${C_BOLD}Odinstaluj:${C_RESET}"
    echo "    ${C_CYAN}$HERE/install.sh --uninstall${C_RESET}"
    echo ""
    echo "  ${C_BOLD}Profil urządzenia:${C_RESET}  $HOME/.config/simple-deck/profiles/"
    echo ""
}

do_uninstall() {
    echo ""
    echo "${C_BOLD}${C_YELLOW}!  Odinstalowywanie Simple Deck${C_RESET}"
    echo ""

    local removed=0

    # D3 fix: defensive guard przed rm -rf z pustą/niebezpieczną ścieżką.
    # Wymagamy że INSTALL_PREFIX kończy się na /simple-deck i nie jest /, /home, /root.
    case "$INSTALL_PREFIX" in
        ""|"/"|"$HOME"|"/root"|"/home")
            die "Odmawiam: INSTALL_PREFIX=$INSTALL_PREFIX wygląda na błędne" ;;
        */simple-deck)  : ;;  # OK - oczekiwany wzorzec
        *)
            warn "INSTALL_PREFIX=$INSTALL_PREFIX nie kończy się na /simple-deck - kontynuuję ostrożnie" ;;
    esac

    if [[ -d "$INSTALL_PREFIX" ]]; then
        log "Usuwanie $INSTALL_PREFIX"
        rm -rf "$INSTALL_PREFIX"
        removed=1
    fi
    if [[ -f "$LAUNCHER_BIN" ]]; then
        log "Usuwanie $LAUNCHER_BIN"
        rm -f "$LAUNCHER_BIN"
        removed=1
    fi
    if [[ -f "$DESKTOP_FILE" ]]; then
        log "Usuwanie $DESKTOP_FILE"
        rm -f "$DESKTOP_FILE"
        removed=1
    fi
    # Ikony
    if [[ -d "$ICONS_BASE" ]]; then
        local icon_files=(
            "$ICONS_BASE/scalable/apps/$APP_SLUG.svg"
            "$ICONS_BASE/16x16/apps/$APP_SLUG.png"
            "$ICONS_BASE/32x32/apps/$APP_SLUG.png"
            "$ICONS_BASE/48x48/apps/$APP_SLUG.png"
            "$ICONS_BASE/64x64/apps/$APP_SLUG.png"
            "$ICONS_BASE/128x128/apps/$APP_SLUG.png"
            "$ICONS_BASE/256x256/apps/$APP_SLUG.png"
        )
        for ic in "${icon_files[@]}"; do
            if [[ -f "$ic" ]]; then
                rm -f "$ic"
                removed=1
            fi
        done
        # Odśwież cache ikon (lusterko install.sh:284 gtk-update-icon-cache).
        # Bez tego icon-theme.cache wskazuje na usunięte PNG-i.
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -q -t -f "$ICONS_BASE" 2>/dev/null || true
        fi
    fi

    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q "$(dirname "$DESKTOP_FILE")" 2>/dev/null || true
    fi

    ask_remove_udev

    # Cleanup legacy instalacji grejem-os (jeśli user aktualizuje z poprzedniej nazwy).
    cleanup_legacy_grejem_install

    # Opcjonalne usunięcie profili użytkownika (~/.config/simple-deck/).
    # Domyślnie zachowujemy (można też --purge-profiles nieinteraktywnie).
    local purge=0
    if [[ "$PURGE_PROFILES" == "1" ]]; then
        purge=1
    fi
    if [[ $purge -eq 1 ]] && [[ -d "$HOME/.config/simple-deck" ]]; then
        log "Usuwanie profili użytkownika: $HOME/.config/simple-deck"
        rm -rf "$HOME/.config/simple-deck"
    fi

    echo ""
    if [[ $removed -eq 1 ]]; then
        ok "Simple Deck odinstalowany"
        if [[ $purge -eq 1 ]]; then
            ok "Profile użytkownika usunięte: $HOME/.config/simple-deck/"
        elif [[ -d "$HOME/.config/simple-deck" ]]; then
            warn "Profile użytkownika ZACHOWANE: $HOME/.config/simple-deck/"
            warn "Aby usunąć:  rm -rf $HOME/.config/simple-deck  (lub uruchom z --purge-profiles)"
        fi
    else
        warn "Nic do usunięcia - Simple Deck nie był zainstalowany"
    fi
    echo ""
}

do_install_udev_only() {
    echo ""
    if [[ $EUID -ne 0 ]]; then
        die "Tryb --udev wymaga uruchomienia z sudo:  sudo $0 --udev"
    fi
    [[ -f "$UDEV_RULE_SRC" ]] || die "Brak pliku reguły: $UDEV_RULE_SRC"

    log "Instalacja reguły udev…"
    cp -f "$UDEV_RULE_SRC" "$UDEV_RULE_DST"
    chmod 0644 "$UDEV_RULE_DST"
    # Usuń legacy regułę sprzed rename (grejem-streamdeck → simple-deck).
    if [[ -f "$UDEV_RULE_LEGACY" ]]; then
        rm -f "$UDEV_RULE_LEGACY"
    fi
    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger --subsystem-match=usb --attr-match="idVendor=1209" \
                                      --attr-match="idProduct=de10" 2>/dev/null || true
    udevadm trigger --subsystem-match=hidraw 2>/dev/null || true
    ok "Reguła udev zainstalowana w $UDEV_RULE_DST"
    echo ""
}

do_help() {
    cat <<EOF
${C_BOLD}Simple Deck - instalator Linuksa${C_RESET}  v$VERSION

${C_BOLD}Użycie:${C_RESET}
  $0                 Instalacja do ~/.local (bez sudo, pyta o udev)
  $0 --uninstall     Odinstalowuje wszystko (pyta o udev; zachowuje profile)
  $0 --uninstall --purge-profiles
                     Odinstalowuje + usuwa profile w ~/.config/simple-deck/
  $0 --udev          Instaluje TYLKO regułę udev (wymaga sudo)
  $0 --help          Ta pomoc

${C_BOLD}Ścieżki instalacji:${C_RESET}
  Venv:        $VENV_DIR
  Launcher:    $LAUNCHER_BIN
  Skrót:       $DESKTOP_FILE
  Ikony:       $ICONS_BASE/{scalable,NxN}/apps/$APP_SLUG.*
  Reguła udev: $UDEV_RULE_DST  (z sudo)

${C_BOLD}Obsługiwane dystrybucje:${C_RESET}
  Debian, Ubuntu, Mint, Pop!_OS, Kali           (apt)
  Fedora, RHEL, CentOS, Rocky, AlmaLinux        (dnf)
  Arch, Manjaro, EndeavourOS, Garuda            (pacman)
  openSUSE, SLES                                (zypper)

${C_BOLD}Dlaczego nie .deb / AppImage?${C_RESET}
  .deb:    wymaga python3-hidapi (błędna nazwa), pulsectl (nie w apt),
           PEP 668 blokuje pip install --user na Ubuntu 24.04+
  AppImage: wymaga pobierania Python.AppImage + appimagetool z sieci

  Ten skrypt jest samowystarczalny - potrzebuje tylko python3 i apt/dnf/pacman.
EOF
}

# ============================================================================
#  Main
# ============================================================================
ACTION="install"
PURGE_PROFILES=0   # --purge-profiles: usuń też ~/.config/simple-deck przy uninstall
for arg in "$@"; do
    case "$arg" in
        --uninstall|-u) ACTION="uninstall" ;;
        --purge-profiles) PURGE_PROFILES=1 ;;
        --udev)         ACTION="udev" ;;
        --help|-h)      ACTION="help" ;;
        --system)
            # D11 fix: --system niedostępny - jasny błąd zamiast cichego fallbacku
            echo "Tryb --system nie jest jeszcze zaimplementowany." >&2
            echo "Obecnie obsługiwany jest tylko --user (instalacja do ~/.local)." >&2
            echo "Aby zainstalować dla wszystkich użytkowników, skopiuj ręcznie:" >&2
            echo "  sudo cp -r $INSTALL_PREFIX /opt/simple-deck" >&2
            exit 2
            ;;
        --user)         : ;;  # domyślny, akceptowany explicite
        *)              warn "Nieznany argument: $arg (ignoruję)" ;;
    esac
done

case "$ACTION" in
    install)   do_install ;;
    uninstall) do_uninstall ;;
    udev)      do_install_udev_only ;;
    help)      do_help ;;
esac
