# AGENTS.md — Simple Deck V2 (GREJEM OS)

> Handoff file for AI coding sessions. **Read this first.** It tells you what the
> project is, what is already done, and what to work on next.
> Last updated: 2026-08-24 (session 8 - V9 / v1.3.3: resize okna frameless).

## 1. Project at a glance

A Stream Deck–style HID controller. Two build targets + installers:

| Module      | Path          | Stack                                   | Status     |
|-------------|---------------|------------------------------------------|------------|
| Firmware    | `firmware/`   | Bare-metal C11 + libopencm3              | FW 1.2.0   |
| Desktop app | `desktop/`    | Python 3.10+ / PySide6 (Qt 6)            | v1.0 working |
| Installer   | `installer/`  | Linux `install.sh` + Windows Inno Setup `.exe` + WiX `.msi` | done   |
| Docs        | `docs/`       | `ARCHITECTURE.md`, `PROTOCOL.md`, `WIRING.md` | done  |
| Release     | `scripts/release.py` + `.github/workflows/release.yml` | Release CLI + CI auto-build | done |

Docs and many code comments are in **Polish**; identifiers are English.
License: MIT © 2026 GREJEM INDUSTRIES.

## 2. Hardware facts (DO NOT re-derive)

- **MCU:** STM32F103**C6**T6, LQFP48, Cortex-M3 @ 72 MHz.
  - **Flash 32 KB**, **SRAM 10 KB**. ⚠️ *NOT* 6 KB SRAM — that figure belongs
    to the smaller STM32F103**C4**. The C6 has 10 KB and the linker script uses it.
- 5 potentiometers → **PA0–PA4** (ADC1_IN0–IN4)
- 4 buttons        → **PB6–PB9** (active-low, internal pull-up)
- 8 LEDs           → **PB10, PB11** (HW PWM TIM2 CH3/CH4) + **PB12–PB15, PA9, PA10** (SW PWM TIM3) — ⚠️ **8 LEDs (V2 VU bar)**, PA9/PA10 były USART1 TX/RX ( nieużywane, potwierdzone)
- Status LED       → **PC13** (active-low onboard)
- USB FS           → **PA11/PA12** (fixed by silicon)
- HSE 8 MHz on PD0/PD1 → PLL ×9 → **SYSCLK 72 MHz**; **USB clock = 48 MHz** (72 ÷ 1.5)
- USB identity: VID `0x1209` / PID `0xDE10` (pid.codes public range)

## 3. Build & test

```bash
# ---- Firmware (needs arm-none-eabi-gcc + libopencm3) ----
git clone https://github.com/libopencm3/libopencm3.git ~/src/libopencm3
cd ~/src/libopencm3 && make TARGET=stm32/f1
export OPENCM3_DIR=~/src/libopencm3
cd firmware
make                # → build/grejem-fw.{elf,bin,hex}
make -C tests       # host-side unit tests (native gcc + mocks)
make ocdflash       # flash via openocd + ST-Link (programs .elf)
make flash          # flash via st-flash (programs .bin)  [stlink-tools]

# ---- Desktop (Python 3.10+) ----
cd desktop
pip install -e ".[linux]"     # or [windows] / [dev]  (dev = pytest, ruff)
python -m grejem_os           # run the GUI;  --demo skips real USB
pytest tests/                 # 240 tests, headless via QT_QPA_PLATFORM=offscreen
ruff check src tests          # lint (if configured)

# ---- Release (build installers + git tag + GitHub release) ----
python scripts/release.py            # Windows: build .exe+.msi -> tag -> release
python scripts/release.py --ci       # any OS: tag + push only; CI builds in cloud
python scripts/release.py --dry-run  # preview without changes
# CI: .github/workflows/release.yml auto-builds on tag push v*
```

## 4. Protocol summary (full spec: `docs/PROTOCOL.md`)

USB Custom HID, EP1 IN/OUT, 64-byte reports, 1 ms poll.

```
Frame:  SOF(0xA5) | TYPE | CH | LEN | PAYLOAD[0..32] | CRC16-LO | CRC16-HI
        (zero-padded to 64 B; CRC16-CCITT, poly 0x1021, init 0xFFFF,
         computed over TYPE..end-of-PAYLOAD)
```

- MCU→PC: `0x01 HEARTBEAT`, `0x02 BUTTON_EVT`, `0x03 POT_EVT` (12-bit LE),
  `0x13 VERSION`, `0x10 ACK`, `0x11 NAK`.
- PC→MCU: `0x04 LED_CMD` (V2: mode=9 VU_BAR, level 0..255), `0x05 CFG_CMD` (filter tuning),
  `0x12 GET_VERSION`.
- NAK error codes: `0x09 BAD_CRC`, `0x0A BAD_FRAME`, `0x0C BAD_TYPE`,
  `0x0D BAD_CHANNEL`, `0x0E OVERFLOW`.

⚠️ `desktop/src/grejem_os/transport/protocol.py` MUST stay byte-identical to
`firmware/include/protocol.h` (SOF, layout, enum values, CRC). Cross-checked by
`desktop/tests/test_protocol.py::TestFirmwareCRCConsistency`.

## 5. Conventions

- **Firmware** uses a "C1–C7 fix" review-comment convention instead of
  `TODO`/`FIXME`. When you resolve a review item, leave/add a `Cx` marker.
  No RTOS — cooperative super-loop + `__WFI`, SysTick 1 kHz scheduler.
  All math is **Q8 fixed-point** (Cortex-M3 has no FPU).
- **Desktop** is layered: `transport/` (Qt-free, reusable from CLI) → `core/`
  (EventBus, profiles, dispatchers) → `platform/` (Win/Linux audio, hotkeys,
  window-detect, each with a `Null*` fallback so the app always boots) →
  `ui/` (frameless Glassmorphism Qt). The HID reader thread emits **only** via
  private queued Qt signals — never touches UI objects directly.
- `desktop/tests/conftest.py` mocks the `hid` module session-wide, so tests run
  without USB hardware.
- **Installer identifiers (stable, do NOT change between versions):**
  - Inno Setup `AppId={{F9CDE1D2-96E9-4A44-AE36-FBCB7012CB1C}}` (`simple_deck.iss`)
  - WiX `UpgradeCode={71BEFB95-F0B6-49EC-9C2F-A3F8C98D3AC7}` (`simple_deck.wxs`)
  - Component GUIDs in `.wxs` are explicit & stable (StartMenu/Desktop/Autostart/
    RegistryInstallInfo). File components are generated by `New-HeatWxs` in
    `build.ps1` (CLI `wix` v4+ **has no `heat` command**) with deterministic
    GUIDs (MD5 of relative path, seed `simple-deck-appfiles:1:`) — stable across
    builds so major upgrades don't orphan files. `wix build` needs
    `-arch x64 -ext WixToolset.UI.wixext` (extension auto-added to `.wix\` cache,
    gitignored). WiX dotnet tool v5/v6 works; v7 requires OSMF EULA (script skips
    .msi with a warning). `wix convert` exits with the *number of conversions*
    (non-zero on success) — never check its `$LASTEXITCODE`.
  - MSI i EXE to osobne produkty z punktu widzenia Windows (różne ProductCode/
    UpgradeCode) — mogą współistnieć bez konfliktu. `build.ps1` buduje oba.

## 6. DONE (v1.0 → V2)

**Sesja 8 (V9 / v1.3.3 — resize okna frameless):**
- **Okno da się zmniejszać** (`ui/main_window.py`): frameless okno nie miało
  uchwytów rozmiaru, a startowe 1280×800 przy min 1024×640 wystawało poza
  ekran przy DPI scaling 125–150% (karta „Gry" z Zapisz była poza ekranem).
  Teraz: **natywny resize krawędziami** — Windows przez `nativeEvent`
  (WM_NCHITTEST → HT*; działa nad child-widgetami, daje natywne kursory +
  Aero Snap), fallback POSIX `startSystemResize` w `mousePressEvent`
  (dociera przez marginesy roota, które ignorują mousePress).
  Min rozmiar obniżony do **820×520**; rozmiar startowy clamped do
  `availableGeometry()` ekranu (`_initial_size`); **rozmiar zapamiętywany**
  w `settings.window_size` (flush w `_flush_settings`, restore+clamp przy
  starcie). Nowe testy: `test_window_resize.py` (15) — łącznie 363 passed.
- **conftest `mock_connection` GC-fix** — `_MockSignals` QObject (bez parenta)
  ginął z GC po wyjściu z fixture (bound-signal nie trzyma źródła) →
  „Signal source has been deleted" przy `connect()`. Dodany keeper
  (`conn._signals_keeper = signals`).

**Sesja 7 (V8 / v1.3.2 — panel „Gry": przycisk Zapisz + WiX .msi fix):**
- **Panel „Gry" przebudowany** (`ui/pages/config_pages.py`): „+ Dodaj" → **„💾 Zapisz"**.
  Input przyjmuje wiele nazw naraz (split po `,`/`;`/whitespace, trim+lowercase+dedup —
  `_parse_game_names()`). Zapis **natychmiastowy** (`_flush_save()`, bez 500 ms
  debouncera) — eliminuje utratę wpisu przy szybkim zamknięciu aplikacji. Usuwanie ✕
  również flushuje natychmiast. Nowe testy: `test_game_apps.py` (16).
- **conftest `tmp_home` naprawiony na Windows** — `Path.home()` (ntpath) czyta
  `USERPROFILE`, nie `HOME`; fixture ustawia teraz oba + HOMEDRIVE/HOMEPATH.
  Wcześniej testy profili (ProfileManager) zapisywały do prawdziwego
  `~/.config/simple-deck/profiles/` → kolizje między runami → 15 fałszywych FAIL-i
  na Windows. Po fixie: **348 passed, 1 skipped**.
- **build.ps1 WiX .msi fix (poprzednia sesja, commit w v1.3.2)**: CLI `wix` v4+ NIE MA
  `heat` → nowy harvester `New-HeatWxs` (deterministyczne GUID-y komponentów z MD5
  ścieżki, seed `simple-deck-appfiles:1:`); `wix convert` jest in-place i zwraca liczbę
  konwersji jako exit code (nie sprawdzać `$LASTEXITCODE`!); `wix build` wymaga
  `-arch x64 -ext WixToolset.UI.wixext` (ext auto-dodawany do `.wix\`, gitignored);
  WiX v7 wymaga EULA OSMF → script pomija .msi z warningiem (używać v5/v6).
  Zainstalowany globalnie: wix 5.0.2 (dotnet tool).

**Firmware** (`firmware/src/`) — **FW 1.2.0 (V3)**:
- ADC: DMA circular (5 ch × 16 samples), oversampling, **adaptive Q8 EMA**
  filter with deadband, dual fast/slow alpha, send-threshold (`adc.c`).
- Buttons: 1 kHz poll + integrator debounce, edge events (`buttons.c`).
- **LEDs: V2 VU bar (8 LED).** Linijka głośności: segmenty + płynny top LED (PWM).
  Auto-focus na poruszany pot, timeout 3 s, fade 300 ms. Pinout: PB10/PB11 (HW PWM
  TIM2 CH3/CH4, 256 poziomów ~1 kHz) + PB12–PB15, PA9, PA10 (SW PWM TIM3 ISR, 64
  poziomy). `LED_CMD(mode=9, level)` — legacy modes 0–7 → NAK(`ERR_BAD_TYPE`)
  (`leds.c`, `timer.c`, `protocol.c`). Scheduler task 20 ms (50 Hz).
- USB: Custom HID stack (libopencm3), framing, CRC16, ACK/NAK, HEARTBEAT
  every 1.5 s, GET_VERSION/VERSION (`usbhid.c`, `protocol.c`).
  **C3 fix (V3):** soft-disconnect/reconnect po MCU reset — STM32F103 ma
  hardwired D+ pull-up (brak software disconnect).  Po SWD/wdg/brownout reset
  host nie widzi reconnect → nie wysyła USB reset → `_usbd_reset()` nigdy nie
  leci → urządzenie wisi (lsusb widzi, dane nie płyną).  Fix: PA12 push-pull
  LOW na ~100 ms wymusza SE0 → host re-enumeruje.  Plus EP0 bootstrap
  (DADDR.EF + bufory) jako belt-and-suspenders (`usbhid.c:258-305`).
- **`CFG_CMD` działa** — runtime tuning filtra ADC (deadband/alpha_slow/
  alpha_fast/send_thr) zapisywany żywo do `adc_cfg_t` (`adc.c`), sterowany z PC.
  fast_thr pozostaje z `config.h` (nie strojone z PC).
- Footprint: ~12.4 KB Flash / ~3.2 KB RAM (of 32 KB / 10 KB). Host unit tests: 8
  binaries (CRC, framing, protocol dispatch V2, ADC filter, buttons, VU bar, LEDs legacy).
  **V5:** `adc_force_all_dirty()` — wysyła wszystkie wartości potencjometrów po
  połączeniu USB (main.c sprawdza `usbhid_ready()` w superloop).

**Desktop** (`desktop/src/grejem_os/`):
- Connection FSM (DISCONNECTED→CONNECTING→CONNECTED↔RECONNECTING),
  auto-reconnect (1 s poll), heartbeat watchdog (4.5 s timeout).
  **HID `open()` naprawione** — `hid.device()` + `dev.open(vid,pid)` (konstruktor
  nie otwiera!). ValueError hardening (C8). **Reguła udev bez `plugdev`** (działa
  na Fedora/Arch/Ubuntu/Debian). **Reconnect fix** — `_handle_disconnect_in_main`
  wywołuje `close()` (martwy uchwyt czyszczony, `is_open`→False, `_try_connect`
  może próbować ponownie). `_try_connect` przechodzi do RECONNECTING przy nieobecności
  device'a (nie zawisa w CONNECTING). Urządzenie łączy się poprawnie ze sprzętem.
- JSON profiles (`~/.config/grejem-os/profiles/*.json`), create/load/save,
  debounced 500 ms save, flush on close.
- **`PotDispatcher`** (`core/pot_dispatcher.py`) — potencjometry STERUJĄ głośnością
  (krzywa linear/log/exp, invert, zakres min/max, czułość, throttle 30 Hz z
  koalescencją). Dawniej pota tylko animowały UI.
- **`LedDispatcher`** (`core/led_dispatcher.py`) — V2: wysyła poziom VU bar
  (`make_vu_cmd`) gdy użytkownik poruszy potencjometrem głośności. Subskrybuje
  `bus.pot_level`, filtruje po `vu_bar_enabled` i akcji potencjometru (SYSTEM_VOLUME /
  APP_VOLUME). Brak timera — czysto event-driven.
- Pot mapping (system/per-app volume/disabled + sensitivity) + **zaawansowane**
  (krzywa, min/max, invert w zwijanej sekcji PotRow).
- **AppPicker edytowalny** — dowolna nazwa procesu (free-text + sugestie
  uruchomionych + ostatnio używane); cel zachowany gdy aplikacja nie działa.
- Button mapping (hotkey/toggle-mute/run-command/none). V2: LED control zastąpiony
  linijką VU bar (8 LED, auto-focus na pot, timeout 3 s, fade 300 ms).
- Live deck-map visualization, overview dashboard, Glassmorphism QSS UI.
- **ProfileSwitcher w headerze** — przełącz + CRUD (nowy/zmień/duplikuj/usuń/
  import/eksport) + sanityzacja nazw (brak path-traversal).
- **SettingsPage przebudowana** z kartami: profile, tuning filtra MCU (CFG_CMD),
  auto-switch rules, appearance (akcent), autostart, urządzenie audio, o aplikacji,
  połączenie + odinstalowanie.
- **Global settings store** (`core/settings.py` → `~/.config/grejem-os/settings.json`)
  dla akcentu/autostartu/urządzenia audio/tuningu CFG/reguł/recent apps.
- **SVG icons** (Lucide-style w `assets/icons/`, generator `_gen.py`) + loader
  `ui/widgets/icon.py` z cache. Nawigacja z ikonami + aktywny wskaźnik.
- **Toast notifications** (`ui/widgets/toast.py`) + `EventBus.notify` signal.
- **Accent theming** na żywo (Appearance → `MainWindow.apply_accent` → QSS + ikony).
- Audio: WASAPI (Win) / PulseAudio (Linux) + `list_output_devices`/`set_default_output`
  (Linux pełny, Windows best-effort). Hotkeys: SendInput (Win) / xdotool (Linux).
  Foreground-window detection: Win32 / X11 EWMH.
- 138 testów przechodzi (protokół, filtry, transport FSM, watchdog, pot_dispatcher,
  led_dispatcher V2 VU, settings, profile_manager CRUD + v1→v2 migration, app_picker, hid_device ValueError
  regression, reconnect cycle).

  **Sesja 2 (desktop bug-fix): 138 → 232 testów.** Naprawiono 8 zgłoszonych
  błędów (firmware nietknięty):
  - **LED page visibility** — `led_page.py` `_add_row()` zwraca wrapper QWidget
    per row; `_update_visibility()` operuje na wrapperach (nie na `parentWidget()`
    które ukrywało całą stronę).
  - **Global pot invert** — `invert_all_pots: bool` w `Settings`; XOR z per-pot
    `cfg.invert` w `PotDispatcher._map_volume()`. Karta w SettingsPage.
  - **Pot value persistence** — `last_pot_values: list[int]` w `Settings`;
    `PotDispatcher` cache + debounced 2s persist; `DeckMap` restoruje przy starcie;
    `app._cleanup()` flush.
  - **CalibrationDialog** — modal z live ADC + "Zapisz min/max" (zastąpił
    event-capture który łapał szum). Auto-swap min>max, Cancel zachowuje oryginał.
  - **Manual hotkey input** — "✎ Wpisz ręcznie…" w `HotkeyCaptureDialog`;
    `_normalize_combo_token()` z synonimami modyfikatorów + media keys.
  - **X11 keyboard grab** — `platform/x11_grab.py` (`X11KeyboardGrabber`,
    `XGrabKey` na wszystkich keycode'ach × modyfikatory). `python-xlib` w deps.
  - **Button on_press** — dispatcher verified correct; toast notification gdy
    akcja odpala (`BTN N → action (press/release)`) + jaśniejszy label checkboxa.
  - **Profile flow** — verified working: CRUD, save/load roundtrip, schema
    migration, auto-switch rules, signal propagation. 12 testów integracyjnych.
  - Nowe pliki testowe: `test_led_page.py` (7), `test_calibration.py` (9),
    `test_hotkey_field.py` (38), `test_x11_grab.py` (5), `test_hotkey_dispatcher.py` (11),
    `test_button_row.py` (6), `test_profile_flow.py` (12), +18 w istniejących.

  **Sesja 3 (Wayland + RUN_COMMAND + LED master + FW pot init): 232 → 240 testów.**
  - **Wayland hotkey backend** — całkowity rewrite `platform/hotkey.py`.
    `LinuxAutoBackend` auto-detekuje: wtype (Wayland-native) → ydotool (uinput) →
    xdotool (X11 fallback). Mapowania evdev codes + wtype XKB names.
    Toast warning gdy żaden backend niedostępny. **Wymaga `dnf install wtype`.**
  - **RUN_COMMAND z polem komendy** — `ButtonRow` ma teraz `QLineEdit` dla komendy
    (widoczny tylko dla RUN_COMMAND). Dispatcher czyta z `cfg.target` (nie `cfg.hotkey`).
    Dodatkowo `QLineEdit` dla TOGGLE_MUTE target. Pola dynamicznie show/hide.
  - **Pot display invert** — `DeckMap._display_value()` aplikuje `cfg.invert XOR
    settings.invert_all_pots` do wyświetlanej wartości ADC. Profil przekazywany do
    DeckMap przez `OverviewPage.set_profile()`.
  - **Firmware: initial pot values on connect** — `adc_force_all_dirty()` w adc.c,
    wołane z main.c gdy `usbhid_ready()` pierwszy raz true. Wysyła wszystkie 5 wartości
    potencjometrów natychmiast po połączeniu USB.
  - **LED Master slider** — w trybie Manual dodany slider "WSZYSTKIE" który ustawia
    wszystkie LEDy jednocześnie. Pojedyncze slidery nadal pozwalają override per-LED.

  **Sesja 4 (LED decoupled + hotkey error surfacing): 240 → 263 testów.**
  Naprawiono 2 zgłoszone błędy użytkownika:
  - **LED indicator only for 2/5 pots** — `LedDispatcher` filtrował potencjometry
    po akcji (tylko SYSTEM_VOLUME / APP_VOLUME sterowały VU barem). Pots 3-4 w
    profilu usera miały `action="none"` → brak wskaźnika. Pot 2 miał zepsutą
    kalibrację (min=0.93, max=0.94, sens=1.2 + global invert) → `_map_volume`
    zawsze zwracał 1.0 → bar zamarznięty na maksa. **Fix V4:** `pot_level` emit
    jest teraz decoupled od akcji — każdy włączony pot (`cfg.enabled=True`)
    steruje linijką. `PotDispatcher._on_pot` emituje sygnał przed gate'ami
    audio/action; dla potów non-volume używa `_raw_level()` (ADC/4095 z invert).
    `LedDispatcher._on_pot_level` usunął check `cfg.action in (...)`. Reset
    kalibracji Pot 2 w `Default.json`. Nowa klasa testowa `TestPotLevelDecoupled`.
  - **Shortcuts from buttons don't work** — user na Wayland bez wtype/ydotool/
    xdotool. `LinuxAutoBackend` spadał do `NullHotkeyBackend` i milcząco no-op'ował.
    **Fix V4:**
    1. `simulate_combo` zwraca teraz `bool` (True=sukces). Wszystkie 5 backendów
       zaktualizowane (Null/Windows/Wtype/Ydotool/Xdotool).
    2. Nowy helper `_run_capture()` — `subprocess.run(capture_output=True, timeout=2)`
       zamiast `Popen(stdout=DEVNULL, stderr=DEVNULL)`. Błędy compositora
       (np. Wayland odrzuca protokół) są teraz logowane na poziomie WARNING
       zamiast znikać w /dev/null.
    3. `LinuxAutoBackend._candidates()` pomija `LinuxXdotoolBackend` gdy
       `XDG_SESSION_TYPE == "wayland"` LUB `WAYLAND_DISPLAY` ustawione —
       xdotool startuje ale nie ma jak dotrzeć do compositora, dawniej był
       wybierany jako fallback i milcząco zawodził.
    4. `HotkeyDispatcher._dispatch` emituje `bus.notify("warning", …)` gdy
       `simulate_combo` zwraca False — użytkownik widzi w UI dlaczego skrót
       nie zadziałał (z podpowiedzią `dnf install wtype`).
    5. Pusty hotkey / pusty RUN_COMMAND target → toast warning zamiast ciszy.
  - Nowe pliki testowe: `test_hotkey_backend.py` (14) — env-aware selection,
    `_run_capture` success/failure/timeout/FileNotFoundError, simulate_combo
    bool returns. +9 w `test_hotkey_dispatcher.py` (`TestHotkeyFailureToast`),
    +4 w `test_pot_dispatcher.py` (`TestPotLevelDecoupled`), +1 inverted w
     `test_led_dispatcher.py` (`test_none_action_pot_drives_led`).

  **Sesja 5 (V6 performance optimization + tray app): 263 → 303 testów.**
  Pełna optymalizacja wydajnościowa (Tier 1-4) + tray icon (opt-in).

  **Tier 1 — hot path (największy wpływ na "responsiveness"):**
  - **Audio cache** (`audio.py`) — cache sesji audio per-target (TTL 5 s) Linux
    (sink_input index) + Windows (AudioSession). Eliminuje pełną enumerację sesji
    PulseAudio/WASAPI na każdym `set_volume` (pot wiggle 30 Hz → dawniej 30 RPC/s).
    Lazy init `Pulse("grejem-os")` — połączenie dopiero przy pierwszym użyciu.
  - **Shared AppListCache** (`core/app_list_cache.py`) — jeden QTimer dla wszystkich
    5 AppPickerów (dawniej 5 osobnych timerów → 5 RPC co 5 s). Singleton.
  - **SettingsPage debounce** (`config_pages.py`) — `_save_settings()` jest teraz
    debounced (500 ms QTimer) zamiast synchronicznego zapisu na każdym tick suwaka.
    Flush w `closeEvent`.
  - **VolumeBar setProperty** (`deck_map.py`) — `_VolumeBar.set_level` używa
    `setProperty("state", …)` + `style().polish()` zamiast 3× `setStyleSheet()` per
    poziom. ~10× szybsze (brak QSS re-parse). Albo zmiana stanu = brak polish.
  - **Async hotkey dispatch** (`hotkey_dispatcher.py`) — `simulate_combo` uruchamiane
    w `QThreadPool` (`QRunnable`) zamiast na głównym wątku. Eliminuje do 2 s UI stall.
    `_HotkeyJob` + `_SignalsBridge` (done signal → `_on_hotkey_done` callback).
  - **Overview throttle** (`overview.py`) — log zdarzeń throttle'owany do ~10 Hz
    (100 ms coalescing QTimer + `deque(maxlen=6)`) zamiast `setText` na każdym z ~250
    zdarzeń/s przy wiggle 5 potów.

  **Tier 2 — idle/background:**
  - **X11 atom cache** (`window_detector.py`) — atomy `_NET_ACTIVE_WINDOW` etc.
    cache'owane w `__init__` (dawniej `intern_atom` 4×/s). Nowa metoda
    `active_window_info()` → jeden X round-trip (dawniej 2×). `_poll` używa
    `active_window_info()`.
  - **Shadow optimization** (`main_window.py`) — `QGraphicsDropShadowEffect` tylko
    na header + sidebar (dawniej na wszystkich ~10-20 kartach — najdroższy efekt Qt).
  - **Single-instance guard** (`core/single_instance.py`) — `QLockFile` w
    `~/.config/grejem-os/grejem-os.lock`. Druga instancja kończy po cichu (kod 0).
    `--demo` pomija blokadę.

  **Tier 3 — startup:**
  - **Lazy page construction** (`main_window.py`) — Overview budowany od razu,
    Pots/Buttons/LED/Settings budowane przy pierwszej wizycie (`_ensure_page(idx)`).
    Oszczędza ~200-400 ms cold-start. `set_profile` stosuje profil tylko do
    zbudowanych stron; `_current_profile` zapamiętany dla lazily-built.
  - **Lazy Pulse/Display init** (`audio.py`, `window_detector.py`) — `Pulse()` i
    `Display()` tworzone dopiero przy pierwszym użyciu (`_ensure_pulse()`).
  - **Lazy QtSvg import** (`icon.py`) — `from PySide6.QtSvg import QSvgRenderer`
    przeniesione do `icon_pixmap()` (ładowane dopiero przy pierwszym wywołaniu).

  **Tier 4 — micro:**
  - **CRC16 LUT** (`protocol.py`) — 256-elementowa tabela LUT zamiast bit-by-bit
    (~8× szybsze). Output **byte-identical** (weryfikowane przez `test_protocol_crc_lut.py`
    — exhaustive test wszystkich 65 536 par bajtów + standardowy CRC test "123456789").
  - **Dedupe uptime** (`event_bus.py`, `connection_manager.py`) — używają
    `parse_heartbeat_payload()` zamiast ręcznego dekodowania LE4.

  **Tray app (opt-in, domyślnie wyłączony):**
  - **New `ui/widgets/tray.py`** — `TrayController(QObject)`. Menu: Pokaż/Ukryj okno,
    Połącz ponownie, Zakończ. Ikona: SVG "home" recolored na akcent + status dot
    (green/yellow/red). Tooltip pokazuje stan połączenia. Disconnect >5 s →
    `showMessage("Urządzenie rozłączone")`. Reconnect → "Połączone ponownie ✓".
  - **`app.py`** — `setQuitOnLastWindowClosed(False)` gdy `show_tray_icon=True`.
    `TrayController` parented to `app` (przeżywa hide okna). Cleanup w `aboutToQuit`.
  - **`main_window.py` closeEvent** — gdy `minimize_to_tray_on_close=True` +
    `show_tray_icon=True`, X button ukrywa okno zamiast kończyć aplikację.
  - **Settings** (`settings.py`) — `show_tray_icon: bool = False`,
    `minimize_to_tray_on_close: bool = False`. SettingsPage karta "Zasobnik systemowy".
  - Nowe pliki testowe: `test_protocol_crc_lut.py` (7), `test_app_list_cache.py` (6),
    `test_single_instance.py` (2), `test_tray.py` (11), `test_hotkey_async.py` (6),
    `test_perf_v6.py` (8). Zaktualizowano `test_hotkey_dispatcher.py` (async),
    `test_profile_flow.py` (async).

**Installer:** 544-line `install.sh` (distro-aware), Inno Setup `.iss`,
WiX `.wxs` (v3 schema, auto-convert v4 w build.ps1), PyInstaller spec,
udev rule, icon set. `build.ps1` buduje domyślnie **oba** formaty
(`Simple-Deck-Setup-1.0.0.exe` + `Simple-Deck-1.0.0.msi`); auto-detekcja
WiX v4 (`wix build`) vs v3 (`candle`+`light`); `-SkipExe`/`-SkipMsi` selektywne.

**Sesja 6 (V7 further CPU/RAM optimization): 303 → 307 testów.**
Cel: dalsza redukcja CPU/RAM desktop app bez utraty jakości (pozostałe
nisko wiszące owoce po V6). Wszystkie zmiany w `desktop/src/grejem_os/`,
firmware nietknięty. Zero regresji (303 stare + 4 nowe testy = 307).

**Tier A — bug fixes (największy sustained CPU win):**
- **`LedDispatcher` throttle** (`core/led_dispatcher.py`) — dodana koalescencja
  33 ms (mirror `PotDispatcher`). MCU LED scheduler = 50 Hz, dawniej dispatcher
  wysyłał jedną ramkę USB OUT na każdy `pot_level` emit (do ~100/s przy
  szybkim wiggle) — >50% odrzucane przez MCU. Teraz ~30 Hz host cap, pierwsza
  ramka po okresie spoczynku leci natychmiast, końcowa wartość zawsze trafia.
- **`AppListCache` emit-on-change** (`core/app_list_cache.py`) — dawniej
  `apps_changed.emit(list(apps))` wołane zawsze (nawet gdy lista identyczna),
  a każdy z 5 `AppPicker`-ów w `refresh()` + `_update_hint()` wołał własne
  `audio.list_apps()` ignorując argument z sygnału. **11 enumeracji PA co 5 s**
  → teraz **1** (tylko w cache). Nowy `cached_apps()` accessor dla nie-tworzącego
  odczytu. Nowy `peek_app_list_cache()` (non-creating singleton accessor).
- **`AppPicker`** (`ui/widgets/app_picker.py`) — `_on_apps_changed(apps)`
  buforuje listę w `self._cached_apps`; `refresh()` i `_update_hint()` z niej
  korzystają zamiast wołać `list_apps()`. Eliminacja 5× redundandate enumeracji
  PA co 5 s gdy PotsPage zbudowany.

**Tier B — idle/tray waste (największy battery win):**
- **`MainWindow.showEvent`/`hideEvent`** + nowy signal `visibility_changed(bool)`
  (`ui/main_window.py`). `app.py` podpina throttling sub-systemów:
  `WindowDetector.set_idle(not visible)` (1 s → 3 s), `AppListCache.stop()`.
  Połączenie HID, hotkeye, LED VU bar, watchdog, reconnect NADAL DZIAŁAJĄ
  w tray — throttle dotyczy tylko UI-fidelity work.
- **`isVisible()` guards** w `OverviewPage._on_event`,
  `DeckMap._on_pot`/`_on_button`/`_VolumeBar.set_level`. Eliminuje ~80
  polish/s + ~500 setText/s gdy user na innej karcie (QStackedWidget) lub
  tray'd. Po powrocie pierwszy event odświeża w ~30 ms.
- **`LinuxX11Backend` wid-cache** (`platform/window_detector.py`) — gdy aktywne
  okno X11 się nie zmieniło, pomiń round-trip o `_NET_WM_PID` + `/proc/{pid}/comm`.
  Steady-state: 3 X round-trips/s → 1/s.
- **Lazy init Xlib** — `from Xlib.display import Display` odroczone z `__init__`
  do `_ensure_display()` (cold-start −30-60 ms, RSS −1 MB). Błędy init stawiają
  backend w tryb no-op (`_init_failed=True`).
- **`WindowDetector.start()` gated** (`app.py:117-122`) — uruchamiany TYLKO gdy
  `settings.auto_switch_rules` niepuste (często puste — większość userów
  przełącza profile ręcznie). Oszczędność 1 X round-trip/s sustained.
- **`WindowDetector.set_idle(bool)`** (`platform/window_detector.py`) — przełącza
  `POLL_INTERVAL_MS` (1 s) ↔ `IDLE_INTERVAL_MS` (3 s) na żywo.

**Tier C — cold-start:**
- **Lazy page module imports** (`ui/main_window.py`, `ui/pages/__init__.py`) —
  `PotsPage`/`ButtonsPage`/`SettingsPage`/`LedPage` importowane lokalnie w
  `_ensure_page(idx)` zamiast eager z `pages/__init__.py`. Oszczędność ~80-150 ms
  cold-start (897-line `config_pages.py` + 361-line `led_page.py` nie parsowane
  gdy user tylko patrzy na Overview). `pages/__init__.py` zredukowany do
  `__all__` — brak eager re-exportów.
- **Lazy pulsectl** (`platform/audio.py`) — `import pulsectl` odroczone z
  `LinuxPulseAudioBackend.__init__` do `_ensure_pulse()`. Cold-start −50-100 ms,
  RSS −2-3 MB (ctypes + PulseAudio client lib ładują się dopiero przy
  pierwszym użyciu audio). Aplikacje bez regulacji głośności nie płaci.

**Tier D — micro:**
- **Martwe sygnały usunięte** (`core/event_bus.py`) — `profile_changed`,
  `led_changed`, `vu_level` (zadeklarowane, nigdy niewyemitowane,
  nigdy niesubskrybowane — grep = 0). Emit'y `heartbeat`/`version`/`ack`/`nak`
  w `route()` usunięte — subskrybenci używają kanonicznych sygnałów z
  `ConnectionManager` (`heartbeat_received`, `fw_version_received`). ACK/NAK
  logowane na poziomie debug/warning zamiast emit'owane do 0 odbiorców.
- **`_PotCell.set_value` last-value cache** (`ui/widgets/deck_map.py`) — pomiń
  identyczne `setValue`/`setText` (eliminuje ~500 setText/s gdy ADC tnie przez
  deadband i skacze między 2-3 sąsiadami).
- **`_ButtonCell` precompute QSS** (`ui/widgets/deck_map.py`) — class-level
  `_STATE_QSS_PRESSED` / `_STATE_QSS_RELEASED` (dawniej build stringa na każdym
  wciśnięciu + QSS re-parse).
- **Usunięto redundant `_install_shadows()`** z `MainWindow.set_profile` — header/
  sidebar mają cienie z `__init__`, nowe karty dostają w `_ensure_page()`.
- **Usunięto double `_refresh_icons()`** w `NavSidebar.__init__` — dawniej 5
  throwaway `setIcon` calls (pierwszy refresh nadpisany przez drugi).
- **`reset_app_list_cache()`** dodane do `app._cleanup()` — gwarantowane
  zatrzymanie timer'a singletonu przy wyjściu.
- **`QThreadPool.globalInstance().waitForDone(500)`** w `_cleanup` — drenuje
  pending hotkey jobs (do 2 s) przy quit (dawniej Ctrl-C / logout mógł uciąć).
- **`SIGTERM` handler** (`app.py`) → `app.quit()` zamiast natychmiastowego exit.
  Teraz `systemctl --user stop` i logout sesji odpalają `_cleanup` (flush
  ustawień potencjometrów do `settings.json`).
- **`QT_LOGGING_RULES` env** (`create_app`) — wycisza Qt debug logi (font cache
  probing, QPA warnings pod XWayland). `setdefault` by user mógł nadpisać.

Nowe pliki testowe: `test_no_emit_when_unchanged`, `test_cached_apps_returns_last_snapshot`
(+2 w `test_app_list_cache.py`), `test_invisible_skips_polish`,
`test_invisible_skips_event_processing` (+2 w `test_perf_v6.py`). Zaktualizowano
`test_emits_apps_changed` (V7 kontrakt: emit tylko przy zmianie), istniejące
testy `test_perf_v6.py` dodały `bar.show()` / `page.show()` (V7 wymóg
`isVisible()` guard). 307/307 przechodzi.

**Co NIE zmienione (zachowanie jakości):**
- Header/sidebar `QGraphicsDropShadowEffect` (V6 zostawił tylko te).
- Tray icon + 5 s disconnect timer.
- `PotDispatcher` 33 ms koalescencja.
- `HeartbeatWatchdog` 4500 ms (MCU 3×1.5 s spec).
- HID reader 500 ms `read()` timeout.
- Cienie, animacje, akcent theming, hotkeye.

## 7. KNOWN GAPS — work to do next (priority order)

### Phase 1 — Close functional gaps (highest ROI, do first)
1. ~~**`CFG_CMD` is dead end-to-end.**~~ **DONE** — firmware `protocol.c` zapisuje
   tuning do `adc_cfg_t`, desktop SettingsPage ma kartę "Filtr ADC" wysyłającą
   `make_cfg_cmd`. Wymaga tej wersji firmware'u (reflash).
2. ~~**Profile auto-switch is wired but unused.**~~ **DONE** — SettingsPage karta
   "Auto-przełączanie" edytuje reguły, `ProfileManager.set_rules` je synchronizuje,
   `app.py` ładuje je ze `settings.json` przy starcie.
3. **Overview ignores VERSION patch byte.** `ui/pages/overview.py:64-67`
   łączy tylko heartbeat — podłącz `fw_version_received` by pokazać pełne
   `major.minor.patch`. (Otwarte)
4. ~~**`protocol_get_drops()` defined but never queried.~~ (Wciąż nieexposed -
   opcjonalne: dodaj `GET_STATS` lub włącz drop do HEARTBEAT.)
5. ~~**Quick security fixes:**~~ **DONE** — `hotkey_dispatcher.py` RUN_COMMAND wciąż
   używa `shell=True` (pozostawione), ale `profile_manager.save()` sanityzuje nazwę
   profilu (`_sanitize_name`) — brak path-traversal.

### Phase 2 — Quality & testing
- ~~Add tests for profile (de)serialization, platform backends (audio/hotkey/~~
  ~~window-detect against `Null*` + fakes), and UI pages (`pytest-qt`).~~
  **Done (session 2):** 232 tests covering profiles, calibration, LED page,
  hotkey field, X11 grab, dispatcher logic, button row, pot caching/invert.
  Still missing: platform backend tests (audio/hotkey/window-detect against
  real backends), `pytest-qt` interaction tests.
- Add CI: `.github/workflows/ci.yml` — firmware `make` + `make -C tests`
  (host gcc), desktop `pytest` (offscreen), `ruff check`. Add `ruff` config
  to `desktop/pyproject.toml` and `.clang-format` for firmware.

### Phase 3 — Hardware / PCB
- **No schematic/PCB exists** — only prose `docs/WIRING.md`. Create a KiCad
  project under `hardware/` (`.kicad_pro`, `.kicad_sch`, BOM CSV). Footprints
  must match `WIRING.md` §2 BOM (LQFP48 MCU, USB-C 16-pin, AMS1117-3.3 SOT-223,
  HC-49 SMD crystal, 0805 passives, 6×6 tact, 5 mm LEDs). Update `WIRING.md`
  to reference the schematic as authoritative.

### Phase 4 — New features (roadmap)
1. ~~**PWM LED dimming.**~~ **DONE (C10) → superseded by V2 VU bar.**
   V1: Hybrid PWM (TIM2 HW + TIM3 SW, 8 modes). V2: **8-LED VU bar** (FW 1.1.0) —
   linijka głośności: segmenty + płynny top LED (PWM), auto-focus na pot, timeout
   3 s, fade 300 ms. Pinout: PB10/PB11 (HW PWM) + PB12–PB15, PA9, PA10 (SW PWM;
   USART1 sacrificed). Profile schema v2 (removed `LedConfig`, added
   `vu_bar_enabled`). Legacy LED modes 0–7 → NAK(`ERR_BAD_TYPE`). 138 desktop +
   8 firmware tests pass.
2. **Flash persistence.** Store last LED states + `CFG_CMD` tuning in the last
   flash page (page 31 of 32). Use libopencm3 `flash_program_half_word`; load
   on boot, write on change with debounce.
3. **PipeWire native audio** (Linux) — replace/augment `pulsectl` (PulseAudio)
   with PipeWire bindings; keep PulseAudio as fallback.
4. **Wayland native window detection** — `wlr-foreign-toplevel-management-v1`
   via `pywayland`; keep X11 path.
5. **PCA9685 I2C LED expander** — adds 16 PWM channels, but I2C1 default
   (PB6/PB7) collides with buttons → must remap buttons or use I2C1 remap
   (PB8/PB9). Major architectural change; defer.
6. Longer-term: GitHub Releases auto-update, EV code signing (Windows),
   cloud profile sync, mobile companion app.

## 8. Doc inconsistencies already known
- ~~`docs/ARCHITECTURE.md:82` said "CMake"~~ — **fixed**, firmware uses Make.
- ~~`installer/linux/udev/99-grejem-streamdeck.rules:16` had stale `.deb`/
  `debian/postinst` reference~~ — **fixed**.
- ~~`installer/icons/README.md:8-9` marked `.ico`/`_256.png` as "to generate"
  though they exist~~ — **fixed**.

## 9. Gotchas
- The "6 KB SRAM" figure describes STM32F103**C4**, not C6 (10 KB).
- LED count is **8** (V2): PB10/PB11 (HW PWM TIM2) + PB12–PB15, PA9, PA10
  (SW PWM TIM3). V1 miało 5 LED (PB10–PB14); V2 dodała 3 piny — PA9/PA10 były
  USART1 TX/RX (potwierdzone nieużywane, komunikacja tylko przez USB).
- hidapi on the host prepends a `0x00` Report-ID byte that the MCU never sees.
- **hidapi raises `ValueError("not open")`** (NOT OSError/HIDError) when the handle
  is invalid — typ. brak uprawnień do `/dev/bus/usb/*` (udev) lub device martwy.
  `write_frame`/`send_frame`/`_reader_loop`/`open()` łapią ValueError (C8 fix) -
  bez tego aplikacja krashedła przy starcie z podłączonym ale niedostępnym
  urządzeniem. `send_frame` dotrzymuje teraz kontraktu "nigdy nie rzuca".
- **`hid.device(vid, pid)` NIE otwiera urządzenia!** Konstruktor ignoruje argumenty
  (`sig: (self, /, *args, **kwargs)`) — trzeba wywołać `dev.open(vid, pid)` osobno.
  Był to latent bug v1.0: aplikacja tworzyła nieotwarty uchwyt, read/write rzucały
  `ValueError("not open")` → reconnect loop (lub crash przed C8). Naprawione:
  `hid.device()` + `dev.open(vid, pid)`. Konstruktora bez `open()` nigdy nie używaj.
- **Reguła udev NIE może używać `GROUP="plugdev"`** na Fedorze/Arch — grupa nie
  istnieje, udev przerywa aplikowanie całej reguły ("Failed to resolve group"),
  więc `MODE="0666"` i `TAG+="uaccess"` nigdy nie wchodzą. Reguła używa teraz
  tylko `MODE="0666"` + `TAG+="uaccess"` (distro-agnostic). Działa wszędzie.
- `tests/conftest.py` rebinduje `grejem_os.transport.hid_device.hid` na mock, bo
  moduł importuje prawdziwe `hid` zanim fixture session wystartuje. Bez tego
  testy były hardware-flaky (zależały od tego czy STM32 jest podłączony).
- The firmware reader loop / desktop reader thread never raises — all decode
  errors are swallowed by `protocol.decode_frame` (returns `None`).
- `firmware/build/` already contains a successful ~12 KB build, proving the
  toolchain path works (once libopencm3 is present at `$OPENCM3_DIR`).
- On this machine the ARM GCC lives at
  `/home/kacper/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin/`
  (ST CubeCLT) and **STM32CubeProgrammer** is the flasher (no `st-flash`).
  `openocd` is at `/usr/bin/openocd`.
- **STM32F103 USB: hardwired D+ pull-up = no software disconnect.** Po MCU-only
  reset (SWD, watchdog, brownout) pull-up 1.5k na D+ pozostaje aktywny — host
  nie widzi disconnect/reconnect → nie re-enumeruje → urządzenie wisi (lsusb
  widzi 1209:de10 ale `usb_configured=0`, `DADDR=0x0000`).  **C3 fix (V3):**
  `usbhid_init()` robi soft-disconnect (PA12 push-pull LOW ~100 ms) + EP0
  bootstrap (DADDR.EF + bufory EP0).  Tylko physical replug lub `usbreset`
  pomagało przed fixem.  Nie dotyczy DFU flash (BOOT0 reset = physical
  reconnect).  Szczegóły: `usbhid.c` komentarz C3.
- **Wayland: xdotool nie działa!** Na natywnym Wayland `xdotool` jest bezużyteczny
  (to narzędzie X11). Hotkey backend auto-detekuje: `wtype` (Wayland) → `ydotool`
  (uinput) → `xdotool` (X11/XWayland). **V4: `xdotool` jest celowo pomijany na
  sesjach Wayland** (`XDG_SESSION_TYPE=wayland` lub `WAYLAND_DISPLAY` ustawione) —
  wybieranie go jako fallback prowadziło do milczącego zawodzenia. **Bez
  zainstalowanego wtype lub ydotool skróty klawiszowe z przycisków NIE działają.**
  Instalacja: `dnf install wtype` (najprostsze) lub `dnf install ydotool`
  (wymaga `ydotoold` daemon). **V4:** `simulate_combo` zwraca `bool`; błędy
  compositora są logowane (stderr przechwytywane) i toast warning pokazywany
  użytkownikowi przy każdej nieudanej próbie.
- **Pusty `hotkey` w `ButtonConfig` jest milczącym no-opem (V4: naprawione).**
  Gdy `action=HOTKEY` ale `hotkey=""`, dispatcher dawniej nic nie robił (cisza).
  Teraz emituje toast warning "Skrót klawiszowy nie jest ustawiony". Analogicznie
  dla RUN_COMMAND z pustym `target`. Gdy diagnozujesz „przycisk nie działa",
  sprawdź profil — często puste pole jest winne, nie firmware.

## 10. Flashing

USB DFU (wbudowany bootloader ROM w STM32F103) jest metodą codzienną — wymaga
tylko kabla USB-C i zworki BOOT0, bez programatora. ST-Link/SWD jest fallbackiem
(do debugu lub brick recovery).

Four options work. Pick by situation:

```bash
# (a) USB DFU + STM32CubeProgrammer  (RECOMMENDED, no programmer needed)
#     1. BOOT0=1, plug USB-C → lsusb | grep 0483:df11  (ST DFU)
#     2. make dfuflash
#     3. BOOT0=0, reset → user firmware (lsusb | grep 1209:de10)
cd firmware && make dfuflash

# (a-alt) USB DFU + dfu-util  (open-source, every Linux distro)
dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D build/grejem-fw.bin

# (b) openocd + ST-Link V2  (Makefile target, programs the .elf)
cd firmware && make ocdflash

# (c) STM32CubeProgrammer CLI over SWD  (present on this machine)
STM32_Programmer_CLI -c port=SWD reset=HWrst \
    -d build/grejem-fw.bin 0x08000000 -hardRst

# (d) stlink-tools  (if installed)
make flash        # → st-flash write build/grejem-fw.bin 0x08000000
```

USB DFU wiring: none extra — USB-C D+/D- already on PA11/PA12. Just need
BOOT0 selectable (Blue Pill jumper or a button on custom PCB).

ST-Link V2 wiring to the board (SWD): SWDIO→PA13, SWCLK→PA14, GND→GND,
3V3→3V3. Verify the device enumerates after flash:
`lsusb | grep 1209:de10`.
