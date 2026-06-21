# Simple Deck — Aplikacja desktopowa (ETAP 3)

Aplikacja kontrolna dla urządzenia **GREJEM Stream Deck**. Python 3.10+,
**PySide6** (Qt 6) z interfejsem w stylu **Glassmorphism / Glossy**. Auto-reconnect
USB HID, wykrywanie aktywnej aplikacji, kontrola głośności (WASAPI / PulseAudio /
PipeWire), symulacja skrótów klawiszowych.

---

## 1. Struktura

```
desktop/
├── pyproject.toml
├── src/simple_deck/
│   ├── __main__.py          ← entrypoint (python -m simple_deck)
│   ├── app.py               ← wire_application: kompozycja wszystkich warstw
│   ├── transport/           ← HID + protokół binarny + auto-reconnect
│   │   ├── protocol.py      ← lustro firmware/protocol.h (CRC16-CCITT)
│   │   ├── hid_device.py    ← wrapper hidapi + reader thread
│   │   ├── watchdog.py      ← heartbeat timeout
│   │   └── connection_manager.py ← FSM DISCONNECTED→CONNECTING→CONNECTED→RECONNECTING
│   ├── core/                ← logika aplikacji
│   │   ├── event_bus.py     ← dystrybucja ramek (Qt signals)
│   │   ├── profile.py       ← Profile / PotConfig / ButtonConfig / vu_bar_enabled (JSON)
│   │   ├── profile_manager.py ← load/save, auto-switch wg aktywnej aplikacji
│   │   └── hotkey_dispatcher.py ← wykonuje akcje przycisków
│   ├── platform/            ← abstrakcje platformowe
│   │   ├── window_detector.py ← Windows GetForegroundWindow / Linux X11 EWMH
│   │   ├── audio.py         ← Windows WASAPI (pycaw) / Linux PulseAudio (pulsectl)
│   │   └── hotkey.py        ← Windows SendInput / Linux xdotool
│   └── ui/                  ← interfejs użytkownika
│       ├── main_window.py   ← frameless window + header Simple Deck
│       ├── widgets/         ← status_chip, nav_sidebar, deck_map, config_rows, app_picker, hotkey_field
│       └── pages/           ← overview, pots, buttons, leds, settings
├── assets/themes/
│   ├── glossy.qss           ← główny arkusz Glassmorphism (~430 linii)
│   └── palette.py           ← paleta kolorów
└── tests/
    ├── test_protocol.py     ← CRC + framing (parowanie z firmware C)
    └── test_filters.py      ← adaptacyjny EMA + deadband
```

---

## 2. Instalacja i uruchomienie (NAJSZYBCIEJ)

### 2.1. Linux / macOS

```bash
cd "/home/kacper/Nextcloud/Projekty/Simple Deck V2/desktop"
./run.sh             # stworzy .venv, zainstaluje deps, uruchomi aplikację
./run.sh --demo      # tryb demo (bez urządzenia)
```

**Pierwsze uruchomienie**: trwa ~60 s (pobiera PySide6 + hidapi + pulsectl).
**Kolejne**: ~1 s (venv i deps już gotowe).

### 2.2. Windows

```powershell
cd "C:\...\Simple Deck V2\desktop"
.\run.bat            # stworzy .venv, zainstaluje deps, uruchomi aplikację
.\run.bat --demo
```

### 2.3. Instalacja na stałe (do menu aplikacji)

Jeśli chcesz mieć Simple Deck w menu aplikacji na stałe (a nie tylko z terminala
deweloperskiego), użyj instalatora:

```bash
cd "../installer/linux"
./install.sh         # instalacja do ~/.local (bez sudo, pyta o udev)
```

Patrz [`installer/README.md`](../installer/README.md) po szczegóły.

---

## 3. Ręczna konfiguracja (dla zaawansowanych)

Skrypty `run.sh` / `run.bat` automatyzują poniższe kroki. Jeśli wolisz ręcznie:

```bash
# Linux/macOS
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[linux]"      # Linux: pulsectl + python-xlib
# pip install -e .             # macOS: bez extras
# pip install -e ".[windows]"  # Windows (zastąp źródłem): pycaw + pywin32

# Narzędzia systemowe (Linux hotkey backend):
sudo apt install xdotool         # Debian/Ubuntu
# sudo dnf install xdotool       # Fedora
# sudo pacman -S xdotool         # Arch

python -m simple_deck              # uruchom
```

Wszystkie zależności są deklarowane w [`pyproject.toml`](pyproject.toml) (PEP 621).
**Nie ma pliku `requirements.txt` celowo** — `pyproject.toml` jest jedynym źródłem prawdy,
a `run.sh` / `run.bat` same wołają `pip install -e ".[linux]"` / `.[windows]`.

---

## 4. Funkcjonalności

### Transport (USB HID)
- **Custom HID** (VID 0x1209, PID 0xDE10), EP1 IN/OUT, 64-bajtowe raporty
- **Auto-reconnect**: przy rozłączeniu kabla lub utracie heartbeatu (>4.5 s) cyklicznie
  próbuje połączyć ponownie w tle - UI się **NIE zawiesza**
- Heartbeat watchdog: 3 × 1.5 s = 4.5 s timeout
- Reader w osobnym wątku daemon → Qt sygnały automatycznie kolejkowane do głównego wątku

### Profile (JSON w `~/.config/simple-deck/profiles/`)
- 5 potencjometrów → głośność systemowa / głośność aplikacji / wyłączony
- 4 przyciski → skrót klawiszowy / toggle-mute / uruchom komendę
- 8 LED VU bar → wskaźnik głośności (auto-focus na pot, timeout 3 s, fade 300 ms)
- Auto-switch wg aktywnej aplikacji (np. `discord` → profil Discord)

### Audio
- **Windows**: WASAPI przez `pycaw` (per-process volume)
- **Linux**: PulseAudio / PipeWire-Pulse przez `pulsectl`
- Fallback `NullAudioBackend` jeśli backend niedostępny

### Hotkeys
- **Windows**: `SendInput` przez ctypes (bez zewnętrznych zależności)
- **Linux**: `xdotool` (wymaga pakietu w PATH)
- Fallback `NullHotkeyBackend`

### Window detection
- **Windows**: `GetForegroundWindow` + `QueryFullProcessImageNameW` (ctypes)
- **Linux X11**: EWMH `_NET_ACTIVE_WINDOW` + `_NET_WM_PID` (python-xlib)
- **Linux Wayland**: nie wspierane (wymaga integracji per-compositor)

### UI / Glossy
- Ciemnoszafirnowe tło z subtelnym gradientem
- Karty frosted glass (rgba 78% + 1px biała obwódka + radius 18)
- Gradient świetlny na górze każdej karty (efekt glossy)
- Neonowe akcenty: cyan `#2DD4FF` (primary), magenta `#FF2EC4`, purple `#9B5CFF`
- Drop shadows przez `QGraphicsDropShadowEffect`
- Animacja kropki statusu (4 kolory wg stanu połączenia)

---

## 5. Testy

```bash
pytest tests/
```

Powinno przejść wszystkie testy:
- `test_protocol.py` — weryfikacja CRC16-CCITT (znane wektory) + round-trip encode/decode
- `test_filters.py` — adaptacyjny EMA + deadband (parowanie z firmware/src/adc.c)

---

## 6. Zobacz też

- [`installer/README.md`](../installer/README.md) — instalator na stałe (Linux `install.sh`, Windows `.exe`)
- [`firmware/README.md`](../firmware/README.md) — firmware STM32
- [`docs/`](../docs/) — pełna dokumentacja architektury, protokołu i schematu
