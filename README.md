# GREJEM OS

> Kompletny ekosystem oprogramowania dla urządzenia typu **Stream Deck**
> pod szyldem **GREJEM INDUSTRIES**.
> Firmware dla **STM32F103C6T6** + aplikacja desktopowa (Windows/Linux)
> z interfejsem w stylu **Glassmorphism**.

```
        ┌─────────────────────────────────────┐
        │  ◈  GREJEM OS                       │
        │     by GREJEM INDUSTRIES            │
        └─────────────────────────────────────┘
                        │
                        │ USB-C  (Custom HID)
                        │
        ┌───────────────▼─────────────────────┐
        │       STM32F103C6T6 (Cortex-M3)      │
        │  • 5 potencjometrów (ADC + DMA)     │
        │  • 4 przyciski (debouncing)         │
        │  • 8 LED VU bar (PWM)               │
        │  • Adaptacyjny filtr EMA + deadband │
        │  • Heartbeat co 1.5 s               │
        │  • 100% non-blocking (libopencm3)   │
        └─────────────────────────────────────┘
```

---

## Moduły projektu

| Moduł       | Stack                                          | Linie kodu | Status       |
|-------------|------------------------------------------------|------------|--------------|
| `firmware/` | libopencm3 (C11), arm-none-eabi-gcc            | 1 440      | ✓ Zbudowane  |
| `desktop/`  | Python 3.10+, PySide6 (Qt 6), hidapi            | 4 093      | ✓ Działające |
| `installer/`| Inno Setup .exe + WiX .msi (Win), install.sh (Linux)  | ~1100       | ✓ Gotowe     |
| `docs/`     | Architektura, protokół, schemat                 | ~700       | ✓ Napisane   |

---

## Spis treści

1. [Szybki start](#1-szybki-start)
2. [Architektura](#2-architektura)
3. [Firmware](#3-firmware)
4. [Aplikacja desktopowa](#4-aplikacja-desktopowa)
5. [Instalatory](#5-instalatory)
6. [Materiały](#6-materiały)

---

## 1. Szybki start

### Dla deweloperów (podgląd kodu, szybkie testy)

```bash
# Linux / macOS
cd desktop && ./run.sh --demo       # ~60 s pierwszy raz (pobiera PySide6),
                                    # ~1 s drugi raz (venv już gotowy)

# Windows
cd desktop && .\run.bat --demo
```

`run.sh` / `run.bat` robią wszystko za Ciebie: tworzą `.venv/`, instalują
zależności z `pyproject.toml`, uruchamiają aplikację. Bez konfiguracji.

### Dla użytkowników końcowych (instalacja na stałe w menu aplikacji)

```bash
# Linux (Debian/Ubuntu/Fedora/Arch/openSUSE)
cd installer/linux && ./install.sh
# → instaluje do ~/.local/, tworzy skrót w menu aplikacji,
#   pyta o regułę udev (dostęp do urządzenia bez sudo)

# Windows — buduj instalatory .exe i .msi:
cd installer\windows && .\build.ps1
# → installer\windows\output\GREJEM-OS-Setup-1.0.0.exe  (Inno Setup)
# → installer\windows\output\GREJEM-OS-1.0.0.msi        (WiX Toolset)
```

### Wymagania wstępne

- **Python 3.10+** (Linux/macOS — zwykle już jest; Windows — instaluj z <https://python.org>)
- **Linux**: `sudo` dostępne (do instalacji pakietów systemowych i reguły udev)
- **Windows** (do budowy instalatorów): [Inno Setup 6+](https://jrsoftware.org/isdl.php) (.exe) + [WiX Toolset](https://wixtoolset.org/) (.msi; v4: `dotnet tool install -g wix`, v3: instalator MSI)
- `arm-none-eabi-gcc` + `libopencm3` (do firmware'u — patrz [`firmware/README.md`](firmware/README.md))
- **USB DFU** (bootloader wbudowany w ROM — wystarczy kabel USB-C + zworka BOOT0)
- ST-Link v2/v3 opcjonalnie (do debugu / brick recovery)

### Pełny proces (urządzenie gotowe)

```bash
# 1. Wgraj firmware na STM32 przez USB-C (bez programatora)
#    a) odłącz USB-C, przestaw zworkę BOOT0 = 1 (do 3V3)
#    b) podłącz USB-C — urządzenie wejdzie w tryb DFU (lsusb | grep 0483:df11)
cd firmware && make && make dfuflash
#    c) przestaw zworkę BOOT0 = 0, wciśnij RESET

# 2. Zainstaluj aplikację na stałe
cd ../installer/linux && ./install.sh
# (lub cd ../desktop && ./run.sh dla trybu deweloperskiego)

# 3. Podłącz urządzenie USB-C i uruchom aplikację z menu
```

> **Alternatywa:** flashowanie przez ST-Link V2 (SWD) — patrz
> [`docs/WIRING.md`](docs/WIRING.md) §6B. Przydaje się do debugu lub gdy USB DFU
> niedostępne. USB DFU jest metodą codzienną (bootloader wbudowany w ROM).

---

## 2. Architektura

Pełen opis w [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

```
PC  ──USB HID──▶  STM32
 ▲                   │
 │                   ├── DMA1 ←─ ADC1 ←─ 5× POT (PA0-PA4)
 │                   ├── Scheduler ──── 4× BTN (PB6-9)  + debounce
 │                   ├── GPIO ──────── 8× LED VU bar (PB10-15, PA9, PA10) + PWM
 │                   └── Heartbeat ──→  PC (co 1.5 s)
 │
 ├── ConnectionManager (auto-reconnect FSM + watchdog)
 ├── EventBus (Qt signals)
 ├── ProfileManager (JSON profile + auto-switch wg foreground window)
 ├── AudioBackend (WASAPI / PulseAudio-PipeWire)
 ├── HotkeyBackend (SendInput / xdotool)
 └── UI (PySide6 + Glassmorphism QSS)
```

---

## 3. Firmware

Lokalizacja: [`firmware/`](firmware/) (1 440 linii)

- **Bare-metal libopencm3** (bez CubeMX) — pełna kontrola nad kodem
- **Zero-blocking** — scheduler + SysTick 1 kHz + `__WFI` w superloopie
- **ADC + DMA** kołowo — CPU praktycznie bezczynny między przerwaniami HT/TC
- **Adaptacyjny EMA** (Q8 fixed-point) — alfa 0.05 → 0.80 dynamicznie
- **Debouncing** integratorem 5 ms — odszumia mechaniczne switch
- **Custom HID** (EP1 IN/OUT, 64 B) — VID `0x1209`, PID `0xDE10`
- **CRC16-CCITT** (CCITT-FALSE) na każdej ramce

**Build:**
```bash
cd firmware
export OPENCM3_DIR=~/src/libopencm3     # zbudowane: make TARGET=stm32/f1
make                                    # build/grejem-fw.{elf,bin,hex}
make dfuflash                           # wgrywa przez USB DFU (BOOT0=1, USB-C)
make flash                              # wgrywa przez st-flash (SWD)
```

**Footprint:** 9 KB Flash / 2.5 KB RAM (28% / 24% z 32/10 KB).

Patrz: [`firmware/README.md`](firmware/README.md)

---

## 4. Aplikacja desktopowa

Lokalizacja: [`desktop/`](desktop/) (4 093 linii)

- **PySide6 (Qt 6)** — natywne wydajności + świetne wsparcie QSS
- **Auto-reconnect** — USB unplugged lub heartbeat timeout → ciche ponowne łączenie
- **Profile JSON** w `~/.config/grejem-os/profiles/`
- **Window detection** — auto-switch profilu wg aktywnej aplikacji
- **Audio control** — Windows WASAPI (`pycaw`), Linux PulseAudio-PipeWire (`pulsectl`)
- **Hotkeys** — Windows `SendInput` (ctypes), Linux `xdotool`
- **Glassmorphism UI** — ciemnoszafir + frosted glass + neon cyan/magenta

**Uruchomienie:**
```bash
cd desktop
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[linux,dev]"
python -m grejem_os
```

**Testy (29/29):**
```bash
pytest tests/
```

Patrz: [`desktop/README.md`](desktop/README.md)

---

## 5. Instalatory

Lokalizacja: [`installer/`](installer/)

### Linux — `installer/linux/install.sh` (rekomendowane)

```bash
cd installer/linux
./install.sh                # instalacja do ~/.local (bez sudo, pyta o udev)
./install.sh --uninstall    # usuwa wszystko
sudo ./install.sh --udev    # tylko reguła udev
./install.sh --help         # pomoc
```

Działa na każdej dystrybucji: Debian, Ubuntu, Fedora, Arch, openSUSE. Automatycznie:
1. Detekcja menedżera pakietów (apt/dnf/pacman/zypper)
2. Instalacja brakujących pakietów systemowych
3. Venv w `~/.local/share/grejem-os/venv`
4. PySide6 + hidapi + pulsectl przez pip (zawsze aktualne wersje)
5. Skrót w menu aplikacji + ikony + launcher w PATH
6. Interaktywne pytanie o regułę udev

Po instalacji GREJEM OS jest w menu aplikacji (szukaj "GREJEM OS") lub
przez `grejem-os` w terminalu.

> **Dlaczego nie .deb / AppImage?** Patrz [`installer/README.md`](installer/README.md).
> Krótko: `.deb` ma błędne nazwy pakietów (np. `python3-hidapi` nie istnieje),
> `pulsectl` w ogóle nie ma w apt, a AppImage wymaga pobierania Python.AppImage
> z sieci. Pojedynczy skrypt `install.sh` jest samowystarczalny i zawsze działa.

### Windows — `installer/windows/` (Inno Setup .exe + WiX .msi)

```powershell
cd installer\windows
.\build.ps1                # pełny pipeline (venv → wheel → PyInstaller → ISCC + WiX)
# Wynik (oba formaty z domyślnym buildem):
#   installer\windows\output\Simple-Deck-Setup-1.0.0.exe  (Inno Setup)
#   installer\windows\output\Simple-Deck-1.0.0.msi        (WiX Toolset)
.\build.ps1 -SkipMsi       # tylko .exe
.\build.ps1 -SkipExe       # tylko .msi
```

`build.ps1` auto-wykrywa WiX v4 (`wix.exe`) lub v3 (`candle.exe`+`light.exe`).
Brak WiX → krok `.msi` pominięty z ostrzeżeniem (ale `.exe` nadal się buduje).

USB HID na Windows nie wymaga żadnego sterownika — działa out-of-the-box.

> Szczegóły, porównanie `.exe` vs `.msi` i flagi builda: [`installer/README.md`](installer/README.md).

### Reguła udev (kluczowa dla Linux)

Bez niej aplikacja musi działać jako `sudo`. Reguła `99-grejem-streamdeck.rules`
nadaje `MODE=0666` + `TAG+="uaccess"` dla VID `0x1209` PID `0xDE10`.
Instalowana automatycznie przez `install.sh` lub ręcznie:

```bash
sudo cp installer/linux/udev/99-grejem-streamdeck.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Patrz: [`installer/README.md`](installer/README.md)

---

## 6. Materiały

| Temat                    | Plik                                    |
|--------------------------|-----------------------------------------|
| Architektura (diagramy)  | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| Protokół binarny         | [`docs/PROTOCOL.md`](docs/PROTOCOL.md)  |
| Schemat podłączeń        | [`docs/WIRING.md`](docs/WIRING.md)      |
| Firmware README          | [`firmware/README.md`](firmware/README.md) |
| Desktop README           | [`desktop/README.md`](desktop/README.md) |
| Installer README         | [`installer/README.md`](installer/README.md) |

### Wydawanie nowej wersji

```bash
python scripts/release.py            # Windows: build .exe+.msi → tag → GitHub release
python scripts/release.py --ci       # dowolny OS: tag + push, CI buduje w chmurze
python scripts/release.py --dry-run  # podgląd
```

CI (`.github/workflows/release.yml`) automatycznie buduje instalatory na
`windows-latest` przy pushu tagu `v*` i tworzy GitHub Release.

---

## Stack technologiczny

| Warstwa       | Firmware                  | Aplikacja                 |
|---------------|---------------------------|---------------------------|
| Język         | C11                       | Python 3.10+              |
| Framework     | libopencm3 (bare-metal)  | PySide6 (Qt 6)            |
| Build         | arm-gcc + Makefile       | setuptools / pip          |
| Komunikacja   | USB Custom HID            | hidapi                    |
| Memory        | 9 KB Flash, 2.5 KB RAM   | ~80-110 MB RAM            |
| Footprint     | 28% Flash, 24% RAM        | Standard Qt               |

---

## Licencja

[MIT](LICENSE) — Copyright © 2026 GREJEM INDUSTRIES.

Biblioteki trzecie zachowują własne licencje: PySide6 (LGPLv3 / commercial),
hidapi (HIDAPI/BSD/GPLv3), pycaw (MIT), pulsectl (MIT).

---

## Roadmap (poza zakresem Etapów 1-4)

- **I2C LED expandery** (PCA9685) — gdyby było więcej diod RGB
- **Wayland native window detection** — przez `wlr-foreign-toplevel`
- **PipeWire native audio** — bezpośrednio, bez PulseAudio-Pulse proxy
- **Mobile companion app** — konfiguracja z telefonu
- **Cloud profile sync** — profile na koncie GREJEM INDUSTRIES
- **Auto-update** — sprawdzanie nowej wersji z GitHub Releases
- **Code signing** — certyfikat EV dla Windows (smarter niż „Unknown Publisher")
