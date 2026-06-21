"""Bootstrap aplikacji Simple Deck - komponuje wszystkie warstwy.

Tworzy:
  - QApplication + QSS (Glassmorphism)
  - EventBus (dystrybucja zdarzeń z MCU)
  - ConnectionManager (HID + auto-reconnect)
  - ProfileManager (JSON profile)
  - WindowDetector (auto-switch profili wg aktywnej aplikacji)
  - AudioBackend + HotkeyBackend (abstrakcje platformowe)
  - HotkeyDispatcher (reaguje na wciśnięcia przycisków)
  - MainWindow (frameless + Glass UI)
"""
from __future__ import annotations

import logging
import signal
import sys
from typing import Optional

from PySide6.QtWidgets import QApplication

from .core.event_bus import EventBus
from .core.hotkey_dispatcher import HotkeyDispatcher
from .core.led_dispatcher import LedDispatcher
from .core.pot_dispatcher import PotDispatcher
from .core.profile_manager import ProfileManager
from .core.settings import Settings
from .platform.audio import make_audio_backend
from .platform.hotkey import make_hotkey_backend
from .platform.window_detector import WindowDetector, make_backend
from .transport.connection_manager import ConnectionManager
from .ui.main_window import MainWindow

log = logging.getLogger(__name__)


def configure_logging(verbose: bool = False) -> None:
    """Konfiguruje logowanie do konsoli (i pliku w ~/.config/simple-deck/)."""
    level = logging.DEBUG if verbose else logging.INFO
    fmt = "%(asctime)s [%(levelname)5.5s] %(name)-22.22s │ %(message)s"
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter(fmt, datefmt="%H:%M:%S"))
    root = logging.getLogger()
    root.setLevel(level)
    # Czyść poprzednie handlery (gdy uruchamiane wielokrotnie np. w testach)
    for h in list(root.handlers):
        root.removeHandler(h)
    root.addHandler(handler)


def create_app(argv: Optional[list[str]] = None) -> QApplication:
    """Tworzy QApplication z wysoką DPI i metadanymi aplikacji."""
    if argv is None:
        argv = sys.argv
    # V7: Wycisz Qt debug logi (font cache probing, QPA platform warnings pod
    # XWayland, itp.). setdefault by user mógł nadpisać QT_LOGGING_RULES env.
    # Zachowuje warnings/errors (tylko *.debug + qt.qpa poniżej warning).
    import os
    os.environ.setdefault("QT_LOGGING_RULES",
                          "*.debug=false;qt.qpa.*=warning")
    app = QApplication(argv)
    app.setApplicationName("Simple Deck")
    app.setApplicationDisplayName("Simple Deck")
    app.setOrganizationName("GREJEM INDUSTRIES")
    app.setApplicationVersion("1.0.0")
    return app


def wire_application(app: QApplication, demo_mode: bool = False,
                     lock=None) -> MainWindow:
    """Skomponuj wszystkie warstwy aplikacji.

    Args:
        app: istniejące QApplication
        demo_mode: jeśli True, transport jest pominięty - okno działa ale
                   nie próbuje łączyć z MCU (przydatne do testów UI)
        lock: QLockFile nabyty przez ``acquire_single_instance()`` (lub None w
              trybie demo/testach). Trzymany na żywo by blokada obowiązywała;
              jednocześnie użyty przez ``SingleInstanceCoordinator`` do startu
              IPC serwera przyjmującego pingi od drugiej instancji.
    """
    # 1. EventBus - centralny hub zdarzeń
    bus = EventBus()

    # 2. Backendy platformowe (z graceful fallback)
    audio_backend = make_audio_backend()
    hotkey_backend = make_hotkey_backend()
    window_backend = make_backend()

    # 3. Connection manager (HID + auto-reconnect)
    connection = ConnectionManager(parent=app)

    # 4. Routing ramek: Connection → EventBus
    connection.frame_received.connect(bus.route)

    # 5. Profile manager - wczytaj / utwórz domyślny
    profile_mgr = ProfileManager(parent=app)
    profile_mgr.ensure_default()
    profile = profile_mgr.active

    # 5b. Globalne ustawienia aplikacji (settings.json)
    settings = Settings()
    settings.load()
    # Zastosuj reguły auto-przełączania z ustawień do ProfileManager'a
    for proc, prof in settings.auto_switch_rules.items():
        profile_mgr.set_rule(proc, prof)

    # 6. Hotkey dispatcher - nasłuchuje button_event i wykonuje akcje
    hotkey_disp = HotkeyDispatcher(bus=bus, hotkey_backend=hotkey_backend,
                                    audio_backend=audio_backend, parent=app)
    if profile is not None:
        hotkey_disp.set_profile(profile)

    # 6b. Pot dispatcher - nasłuchuje pot_event i steruje głośnością audio.
    # Bez tego potencjometry tylko animują UI, nie regulują dźwięku.
    pot_disp = PotDispatcher(bus=bus, audio_backend=audio_backend,
                              settings=settings, parent=app)
    if profile is not None:
        pot_disp.set_profile(profile)

    # 6c. V2: LED dispatcher - wysyła poziom VU bar gdy pot głośności się zmienia
    led_disp = LedDispatcher(bus=bus, connection=connection,
                             profile_mgr=profile_mgr, parent=app)
    if profile is not None:
        led_disp.set_profile(profile)

    # 7. Window detector - auto-switch profili (jeśli backend dostępny).
    # V7: Startuj pollera TYLKO gdy użytkownik skonfigurował reguły — gdy
    # auto_switch_rules == {} (przypadek większości userów), 1 Hz poll X11 +
    # /proc read to czysty waste. ProfileManager jest nadal gotowy przyjąć
    # reguły (dodane w locie przez SettingsPage), ale sam poller śpi aż do
    # restartu. Większość userów i tak przełącza profile ręcznie.
    window_det = WindowDetector(backend=window_backend, parent=app)
    window_det.active_app_changed.connect(
        lambda proc, _title: profile_mgr.on_foreground_process(proc)
    )
    if settings.auto_switch_rules:
        window_det.start()
    else:
        log.info("  window detector  : idle (no auto-switch rules configured)")

    # 8. UI - główne okno (z ProfileManager - strony go używają do zapisu)
    window = MainWindow(bus=bus, connection=connection,
                         audio_backend=audio_backend,
                         profile_mgr=profile_mgr,
                         settings=settings)
    if profile is not None:
        window.set_profile(profile)

    # 8b. V6: System tray icon (opt-in — domyślnie wyłączone)
    # V8: Wspólny callback _raise_window używany też przez SingleInstanceCoordinator.
    def _raise_window():
        window.showNormal()
        window.raise_()
        window.activateWindow()

    tray = None
    if settings.show_tray_icon:
        app.setQuitOnLastWindowClosed(False)
        from .ui.widgets.tray import TrayController
        tray = TrayController(app=app, connection=connection, bus=bus,
                              settings=settings, parent=app)
        tray.show_window_requested.connect(_raise_window)
        tray.hide_window_requested.connect(window.hide)
        tray.reconnect_requested.connect(
            lambda: (connection.stop(), connection.start()))
        tray.quit_requested.connect(app.quit)
        # Pass accent changes to tray
        window._tray_ref = tray  # prevent GC

    # 8c. V8: Single-instance IPC server. Druga instancja (klik w menu aplikacji,
    # skrót na pulpicie) ping'uje nas by przywołać ukryte/zminimalizowane okno.
    # Działa niezależnie od traya — obsługuje też zwykłą minimalizację OS-ową.
    coordinator = None
    if lock is not None:
        from .core.single_instance import SingleInstanceCoordinator
        coordinator = SingleInstanceCoordinator(lock=lock, parent=app)
        coordinator.raise_requested.connect(_raise_window)

    # 9. Po zmianie profilu - aktualizuj dispatcher'y i UI
    def _on_profile_changed(new_profile):
        hotkey_disp.set_profile(new_profile)
        pot_disp.set_profile(new_profile)
        led_disp.set_profile(new_profile)
        window.set_profile(new_profile)
    profile_mgr.active_profile_changed.connect(_on_profile_changed)

    # 9b. V7: Throttle sub-systemów gdy okno ukryte (tray / minimize).
    # WindowDetector: 1 s → 3 s polling. AppListCache: zatrzymany gdy nie ma
    # kim patrzeć na AppPickery. Połączenie HID, hotkeye, LED VU, watchdog,
    # reconnect nadal działają — one nie słuchają visibility_changed.
    from .core.app_list_cache import peek_app_list_cache

    def _on_visibility(visible: bool):
        if settings.auto_switch_rules:
            window_det.set_idle(not visible)
        cache = peek_app_list_cache()
        if cache is not None:
            if visible:
                cache.start()
            else:
                cache.stop()
    window.visibility_changed.connect(_on_visibility)

    # 10. Start połączenia (chyba że demo)
    if not demo_mode:
        connection.start()

    # 11. Cleanup przy wyjściu - zatrzymaj timery i wątki grzecznie.
    # MainWindow.closeEvent robi to samo dla window-close, tu obejście
    # dla app.quit() / SIGINT / logout etc.
    def _cleanup():
        try:
            pot_disp._flush_persist()
        except Exception:
            log.exception("pot_disp._flush_persist() failed")
        try:
            window_det.stop()
        except Exception:
            log.exception("window_det.stop() failed")
        try:
            # V7: Zatrzymaj singleton cache (zatrzymuje timer + zwalnia ref'y)
            from .core.app_list_cache import reset_app_list_cache
            reset_app_list_cache()
        except Exception:
            log.exception("reset_app_list_cache() failed")
        try:
            if tray is not None:
                tray.cleanup()
        except Exception:
            log.exception("tray.cleanup() failed")
        try:
            if coordinator is not None:
                coordinator.cleanup()
        except Exception:
            log.exception("coordinator.cleanup() failed")
        try:
            connection.stop()
        except Exception:
            log.exception("connection.stop() failed")
        try:
            # V7: Drenuj QThreadPool by pending hotkey jobs (do 2 s) zdążyły.
            # Bez tego wciśnięcie przycisku tuż przed quit może zostać ucięte.
            from PySide6.QtCore import QThreadPool
            QThreadPool.globalInstance().waitForDone(500)
        except Exception:
            log.exception("QThreadPool.waitForDone failed")
    app.aboutToQuit.connect(_cleanup)

    log.info("Simple Deck application wired%s", " [DEMO MODE]" if demo_mode else "")
    log.info("  audio backend    : %s", type(audio_backend).__name__)
    hk_name = type(hotkey_backend).__name__
    if hasattr(hotkey_backend, "backend_name"):
        hk_name = hotkey_backend.backend_name()
    hk_ok = hotkey_backend.available()
    log.info("  hotkey backend   : %s (%s)", hk_name,
             "available" if hk_ok else "UNAVAILABLE")
    if not hk_ok:
        bus.notify.emit("warning",
                        "Brak backendu skrótów! Zainstaluj: wtype "
                        "(dnf install wtype) lub ydotool (dnf install ydotool)")
    log.info("  window detector  : %s", type(window_backend).__name__)

    return window


def run(argv: Optional[list[str]] = None, demo_mode: bool = False,
        lock=None) -> int:
    """Główny entrypoint aplikacji."""
    configure_logging(verbose="--verbose" in (argv or sys.argv))
    log.info("=== Simple Deck  by GREJEM INDUSTRIES ===")

    app = create_app(argv)

    # Pozwól Ctrl-C z konsoli zabić aplikację (domyślnie Qt to tłumi)
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    # V7: SIGTERM (np. systemctl --user stop, logout sesji) → graceful quit
    # by odpalił się _cleanup (flush ustawień, zatrzymanie timerów). Bez tego
    # process ginie natychmiast z niezapisanymi ustawieniami potencjometrów.
    signal.signal(signal.SIGTERM, lambda *_: app.quit())

    window = wire_application(app, demo_mode=demo_mode, lock=lock)
    window.show()
    return app.exec()
