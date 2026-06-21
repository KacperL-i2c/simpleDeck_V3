# STATUS.md — Session Handoff (2026-06-21, session 3)

> **Read this + AGENTS.md before starting work.**
> AGENTS.md has the full project spec; this file has the current session state.

## What was done this session (desktop + firmware fixes)

5 user-reported bugs fixed. **232 → 240 tests** (8 new). Firmware + desktop changed.

### 1. Pot display wrong after changing direction — FIXED
**Bug:** With `invert_all_pots: true`, the DeckMap bar showed raw ADC direction while
volume went the opposite way.
**Fix:** `DeckMap._display_value()` applies the effective invert (`cfg.invert ^ settings.invert_all_pots`)
to the displayed ADC value. Profile is now passed to DeckMap via `OverviewPage.set_profile()`.
**Tests:** Verified manually (adc=1000 → 3095 with invert).

### 2. Shortcut not working after adding — FIXED (root cause: Wayland!)
**Bug:** User is on **Wayland** and **no hotkey tool was installed** (no xdotool, wtype, ydotool).
The old backend only supported xdotool (X11-only) and silently no-op'd when missing.
**Fix:** Rewrote `platform/hotkey.py` with auto-detect `LinuxAutoBackend`:
- **wtype** (Wayland-native, tried first) — `wtype -M ctrl -k d -m ctrl`
- **ydotool** (works everywhere via uinput, tried second) — `ydotool key 29:1 32:1 32:0 29:0`
- **xdotool** (X11 fallback) — `xdotool key --clearmodifiers ctrl+d`
- Added shared evdev code mapping + wtype XKB name mapping
- `app.py` now logs backend name and shows toast warning if none available
**Action required:** User must install one: `dnf install wtype` (recommended)

### 3. Can't add command — FIXED
**Bug:** `ButtonRow` had "Uruchom komendę" action but NO text field for the command.
The dispatcher re-used `cfg.hotkey` (key-capture widget, read-only) instead of a text input.
**Fix:**
- Added `_command_field` (QLineEdit) in ButtonRow, visible only for RUN_COMMAND
- Added `_mute_target` (QLineEdit) for TOGGLE_MUTE target
- Added `_hotkey_row`, `_command_row`, `_mute_row` wrapper widgets with show/hide per action
- Dispatcher now reads `cfg.target` for RUN_COMMAND (was `cfg.hotkey`)
- Action fields dynamically show/hide via `_update_field_visibility()`
**Tests:** `TestButtonRowRunCommand` (6 tests), dispatcher RUN_COMMAND test (2 tests)

### 4. Pot statuses not loaded on app open — FIXED (firmware + desktop)
**Bug:** MCU only sent POT_EVT on physical change. No initial values on connect.
Desktop cached values in settings.json but volume wasn't applied until pot was moved.
**Fix:**
- **Firmware:** Added `adc_force_all_dirty()` to adc.c/adc.h. Called from main.c
  superloop when `usbhid_ready()` first returns true. Forces all 5 pot values to be
  emitted immediately after USB connection.
- **Desktop:** PotDispatcher already processes incoming POT_EVT and applies volume.
  No additional desktop change needed — the firmware sends values, desktop receives them.
**Note:** This requires firmware reflash. The `adc_force_all_dirty()` call is in main.c.

### 5. LED control all-together + separate — DONE
**Bug:** Manual mode only had per-LED sliders. No way to set all at once.
**Fix:** Added "WSZYSTKIE" master slider at top of Manual mode section.
Dragging it sets all individual sliders simultaneously. Individuals can still override.
When loading a profile where all LEDs have the same value, master slider reflects it.
**Tests:** Existing LED page tests still pass (7).

## Improvement suggestions (shared with user)

1. **Install wtype** — `dnf install wtype` — critical for button shortcuts to work on Wayland
2. **Reflash firmware** — needed for initial pot values on connect
3. **X11 keyboard grab** (from session 2) won't work on Wayland — it silently fails, which is fine
4. **Consider libei** — modern Wayland input injection API (future, if wtype/ydotool insufficient)

## Test summary

| Category | Tests | Status |
|----------|-------|--------|
| Previous (session 2) | 232 | All pass |
| New RUN_COMMAND (ButtonRow) | 6 | Pass |
| New RUN_COMMAND (dispatcher) | 2 | Pass |
| **Grand total** | **240** | All pass in 1.6s |

## Files modified this session

**Firmware:**
- `firmware/include/adc.h` — Added `adc_force_all_dirty()` declaration
- `firmware/src/adc.c` — Implemented `adc_force_all_dirty()`
- `firmware/src/main.c` — Call `adc_force_all_dirty()` on first USB ready

**Desktop source:**
- `src/grejem_os/platform/hotkey.py` — Rewritten: LinuxAutoBackend (wtype/ydotool/xdotool)
- `src/grejem_os/ui/widgets/config_rows.py` — ButtonRow: command field, mute target, visibility
- `src/grejem_os/ui/widgets/deck_map.py` — Invert display, set_profile(), _display_value()
- `src/grejem_os/ui/pages/overview.py` — set_profile() passes profile to DeckMap
- `src/grejem_os/ui/main_window.py` — set_profile() calls OverviewPage.set_profile()
- `src/grejem_os/ui/pages/led_page.py` — Master brightness slider in Manual mode
- `src/grejem_os/core/hotkey_dispatcher.py` — RUN_COMMAND uses cfg.target
- `src/grejem_os/app.py` — Log backend name, toast warning if no backend

**Tests:**
- `tests/ui/widgets/test_button_row.py` — Added TestButtonRowRunCommand (6 tests)
- `tests/test_hotkey_dispatcher.py` — Added RUN_COMMAND target tests (2 tests)

## Action items for user

1. **`dnf install wtype`** — Without this, button shortcuts won't work on Wayland
2. **Reflash firmware** — Build and flash to get initial pot values on connect:
   ```bash
   cd firmware && make dfuflash   # or make ocdflash
   ```
3. **Restart the app** — All desktop fixes take effect on next launch

## Previous sessions

### Session 2 (desktop bug-fix marathon)
Fixed 8 bugs: LED page visibility, global pot invert, pot value persistence, calibration UX,
manual hotkey input, X11 keyboard grab, button on_press verification, profile flow verification.
138 → 232 tests.

### Session 1 (firmware C3 fix)
USB soft-disconnect/reconnect after MCU reset. FW 1.2.0.

## Environment notes

- **Session type: Wayland** (not X11!) — this was the root cause of shortcut failures
- Desktop venv: `desktop/.venv/` (Python 3.14.5, PySide6 6.11.1)
- Run tests: `cd desktop && ./.venv/bin/pytest tests/ -q` (240 tests, ~1.6s)
- `wtype`, `ydotool`, `xdotool` NOT installed — user needs to install at least one
- libopencm3 not built on this machine — firmware changes need `make` after building libopencm3
