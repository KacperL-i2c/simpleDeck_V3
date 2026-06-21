# Simple Deck — Instalatory

Kompletne skrypty do zainstalowania aplikacji **na stałe** w systemie.

> **Dla deweloperów** (szybki start, bez instalacji): w [`../desktop/`](../desktop/)
> są skrypty `run.sh` / `run.bat` które tworzą `.venv/` lokalnie i uruchamiają
> aplikację w trybie deweloperskim. Patrz [`../desktop/README.md`](../desktop/README.md).
>
> **Dla użytkowników końcowych** (na stałe, w menu aplikacji): użyj skryptów
> z tego katalogu (poniżej).

---

## Drzewo katalogów

```
installer/
├── icons/                          ← ikony aplikacji (SVG + PNG + ICO)
│   ├── simple_deck.svg
│   ├── simple_deck_{16,32,48,64,128,256}.png
│   ├── simple_deck.ico
│   └── README.md
├── windows/                        ← Instalatory Windows (.exe + .msi)
│   ├── simple_deck.spec              ← PyInstaller spec
│   ├── simple_deck.iss               ← Inno Setup script (.exe)
│   ├── simple_deck.wxs               ← WiX source (.msi, v3 schema)
│   ├── build.ps1                   ← PowerShell pipeline (.exe + .msi)
│   ├── build.bat                   ← Opakowanie .bat
│   ├── LICENSE.txt / BEFORE.txt / AFTER.txt
│   └── output/                     ← (po buildzie) Simple-Deck-Setup-1.0.0.exe
│                                                     + Simple-Deck-1.0.0.msi
└── linux/                          ← Instalator Linux (pojedynczy skrypt)
    ├── install.sh                  ← Self-contained installer
    └── udev/
        └── 99-simple-deck.rules  ← Reguła udev (VID 1209 PID DE10)
```

---

## Linux — `install.sh` (jeden skrypt na wszystko)

```bash
cd installer/linux

./install.sh                # instalacja do ~/.local (bez sudo, pyta o udev)
./install.sh --uninstall    # usuwa wszystko (pyta o udev)
sudo ./install.sh --udev    # instaluje TYLKO regułę udev
./install.sh --help         # pomoc
```

### Co robi `./install.sh`

| Krok | Akcja | sudo? |
|---|---|---|
| 1 | Detekcja distro (`/etc/os-release`) → apt / dnf / pacman / zypper | — |
| 2 | Instalacja pakietów systemowych (`python3-venv xdotool libxkbcommon nss …`) — tylko brakujące | tak |
| 3 | Venv w `~/.local/share/simple-deck/venv/` | nie |
| 4 | `pip install PySide6 hidapi pulsectl python-xlib` + simple-deck (editable) w venv | nie |
| 5 | Launcher `~/.local/bin/simple-deck` | nie |
| 6 | `.desktop` + ikony w `~/.local/share/` | nie |
| 7 | Interaktywne pytanie o regułę udev (dostęp do urządzenia bez sudo) | opcjonalnie |

**Czas**: ~60-90 s (głównie pobieranie PySide6 ~60 MB).
**Disk**: ~150 MB w `~/.local/share/simple-deck/`.

### Po instalacji

- Aplikacja dostępna z menu aplikacji (szukaj **Simple Deck**)
- Lub z terminala: `simple-deck` (launcher w `~/.local/bin/`)
- Profile: `~/.config/simple-deck/profiles/`
- Reguła udev: `/etc/udev/rules.d/99-simple-deck.rules`

### Dlaczego nie `.deb` / AppImage

Większość aplikacji Pythona desktopowych traci czas na pakowaniu do dystrybucyjnych
formatów. Simple Deck celowo tego unika:

| Problem z `.deb` | Nasze rozwiązanie |
|---|---|
| `python3-hidapi` nie istnieje pod tą nazwą w apt | `pip install hidapi` w venv — zawsze działa |
| `pulsectl` w ogóle nie ma w apt | `pip install pulsectl` w venv |
| `python3-pyside6` tylko na Ubuntu 22.04+ | `pip install PySide6` zawsze aktualny |
| PEP 668 blokuje `pip install --user` na Ubuntu 24.04+ | Venv omija PEP 668 |
| Trzeba `dpkg-deb`, nie ma na Fedora/Arch | Nie potrzebujemy — skrypt działa wszędzie |

| Problem z AppImage | Nasze rozwiązanie |
|---|---|
| Wymaga `appimagetool` (rzadko zainstalowane) | Skrypt używa systemowego `python3` |
| Pobiera `Python.AppImage` z sieci (~100 MB) | Używa venv, brak downloadu |
| `AppRun` zrywa się gdy Python innej wersji | Bez AppRun, bez problemu |
| Brak możliwości aktualizacji bez rebuildu | `pip install -e` → live update ze źródeł |

---

## Windows — `installer/windows/` (Inno Setup .exe + WiX .msi + PyInstaller)

`build.ps1` buduje domyślnie **oba** formaty instalatora z tego samego folderu
`dist/Simple-Deck/` (wyplutego przez PyInstaller). Wybierz jeden lub oba:

| Format | Narzędzie | Cel |
|---|---|---|
| **`.exe`** | Inno Setup 6+ | Instalator dla użytkownika końcowego (przyjazny kreator, okna BEFORE/AFTER, checkboxy na pulpit/autostart) |
| **`.msi`** | WiX Toolset 3.14 lub 4+ | Instalator korporacyjny (GPO/SCCM/Intune, `msiexec /x`, ciche wdrożenia, dziennik transakcji) |

### Wymagania build

- Windows 10/11 x64
- Python 3.10+ z <https://python.org>
- **Dla `.exe`**: Inno Setup 6+: <https://jrsoftware.org/isdl.php>
- **Dla `.msi`**: WiX Toolset — jedno z:
  - **v4 (zalecany)**: `dotnet tool install -g wix` (wymaga .NET SDK 7+)
  - **v3 (klasyczny)**: instalator MSI z <https://wixtoolset.org/releases/v3.14/>
- `build.ps1` auto-wykrywa wersję WiX i dobiera odpowiednią komendę.

### Build jedną komendą

```powershell
cd installer\windows
.\build.ps1                 # pełny build: .exe + .msi (domyślnie oba)
.\build.ps1 -Clean          # clean + pełny build
.\build.ps1 -SkipMsi        # tylko Inno Setup .exe (stare zachowanie)
.\build.ps1 -SkipExe        # tylko WiX .msi
.\build.ps1 -SkipInno       # = -SkipExe (przestarzały alias)
```

Lub przez `.bat` (zwykły cmd.exe):

```cmd
build.bat            REM pełny build (.exe + .msi)
build.bat clean      REM clean + pełny build
build.bat nomsi      REM tylko .exe
build.bat noexe      REM tylko .msi
build.bat noinno     REM = noexe (alias)
```

### Co robi `build.ps1`

1. Tworzy `.venv-build/` w `desktop/`
2. Instaluje zależności + PyInstaller
3. `pyinstaller simple_deck.spec` → `dist/Simple-Deck/` (folder)
4. `ISCC.exe simple_deck.iss` → `output/Simple-Deck-Setup-1.0.0.exe` *(jeśli Inno Setup obecny)*
5. WiX:
   - **v4**: `wix heat` (harvest) → `wix convert` (v3→v4 schema) → `wix build` → `.msi`
   - **v3**: `heat` (harvest) → `candle` (compile) → `light` (link) → `.msi`
6. Podsumowanie z rozmiarami obu artefaktów

Gdy Inno Setup lub WiX nie są zainstalowane, odpowiedni krok jest pomijany
z ostrzeżeniem (ale drugi format nadal się buduje).

### Dystrybucja

Wynikowe pliki w `installer\windows\output\`:
- `Simple-Deck-Setup-1.0.0.exe` (~80-120 MB) — instalator Inno Setup
- `Simple-Deck-1.0.0.msi` (~80-120 MB) — instalator WiX

USB HID na Windows nie wymaga żadnego sterownika — działa out-of-the-box
na Win10/11 w obu formatach. Licencja MIT jest kopiowana do katalogu
instalacyjnego (`LICENSE.txt`).

### Skróty opcjonalne w .msi

W kreatorze Inno Setup (`Setup.exe`) są checkboxy „Pulpit" i „Autostart".
Kreator MSI (`WixUI_InstallDir`) jest minimalistyczny — skróty opcjonalne
włącza się z linii komend (przydatne dla GPO/SCCM):

```cmd
msiexec /i Simple-Deck-1.0.0.msi INSTALLDESKTOP=1 INSTALLAUTOSTART=1
msiexec /i Simple-Deck-1.0.0.msi /qb!                   REM cicha instalacja z paskiem
msiexec /x Simple-Deck-1.0.0.msi                        REM deinstalacja
```

Skrót w Menu Start tworzy się zawsze w obu instalatorach.

---

## Reguła udev (kluczowa dla Linux)

Bez niej urządzenie HID jest tworzone jako `/dev/hidraw0` z prawami
`0660 root:root` — aplikacja nie ma dostępu bez `sudo`.

Reguła `99-simple-deck.rules` nadaje:
- `MODE="0666"` — każdy użytkownik może czytać/pisać (główny mechanizm, działa na każdej dystrybucji)
- `TAG+="uaccess"` — dodatkowo ACL dla aktywnego użytkownika konsoli (systemd-logind)

Reguła używa **wyłącznie** `MODE` + `uaccess` — nie zależy od grupy `plugdev`
(która nie istnieje na Fedorze/Arch, a której brak powodował przerwanie
aplikowania całej reguły przez udev). Dzięki temu działa wszędzie: Fedora,
Ubuntu, Debian, Arch.

**Instalacja** (jeśli nie przez `install.sh`):
```bash
sudo cp installer/linux/udev/99-simple-deck.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Po instalacji:
```bash
ls -la /dev/hidraw*
# powinno pokazać crw-rw-rw- ... 1209:de10 ... /dev/hidrawX
```

---

## Aktualizacja

Po zmianie kodu aplikacji:

- **Linux**: uruchom `./install.sh` ponownie — pip wykryje zmiany i przeinstaluje.
  Simple-Deck jest instalowany w trybie editable (`-e`), więc kod Pythona aktualizuje się live.
- **Windows**: przebuduj instalator przez `.\build.ps1` (oba formaty) i uruchom
  nowy `.exe` (lub `.msi` przez `msiexec /i`). MSI z nowszą wersją automatycznie
  odinstaluje starszą (major upgrade, ten sam `UpgradeCode`).

---

## Wydanie nowej wersji (Release)

Nowa wersja = git tag `v{version}` + GitHub Release z artefaktami `.exe` + `.msi`.

### Opcja A — lokalnie (Windows, najszybsza)

```powershell
python scripts\release.py
#   1. Buduje installer\windows\build.ps1  -> output\*.exe + *.msi
#   2. Tworzy tag v{version} + push
#   3. gh release create z obu artefaktami + auto-notes
```

### Opcja B — przez CI (dowolny OS)

```bash
python scripts/release.py --ci
#   1. Tworzy tag v{version} + push
#   2. CI (.github/workflows/release.yml) buduje na windows-latest i tworzy release
```

Lub recznie: `git tag v1.0.0 && git push origin v1.0.0` → CI robi reszte.

### Pozostale flagi

```
python scripts/release.py --skip-build     # tag + release (pre-built artefakty)
python scripts/release.py --version 1.1.0  # nadpisz wersje
python scripts/release.py --prerelease     # oznacz jako pre-release
python scripts/release.py --dry-run        # podglad bez zmian
python scripts/release.py --notes FILE     # wlasne release notes
```

### Wymagania

- `git` + `gh` CLI (`gh auth login`) — tag + release
- Windows 10/11 + Inno Setup + WiX — budowanie instalatorow (patrz wyzej)
- Wersja czytana z `desktop/pyproject.toml` (`version = "x.y.z"`)
