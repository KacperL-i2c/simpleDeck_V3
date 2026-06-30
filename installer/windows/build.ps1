<#
.SYNOPSIS
    Buduje aplikację Simple Deck i instalatory Windows (.exe + .msi)

.DESCRIPTION
    Kompletny pipeline:
      1) Tworzy/odświeża virtualenv
      2) Instaluje zależności aplikacji + PyInstaller
      3) Zbuduje folder dist/Simple-Deck/ przez PyInstaller (.spec)
      4) Uruchomi Inno Setup (ISCC.exe) aby skompresować do instalatora .exe
      5) Uruchomi WiX Toolset (wix/candle+light) aby zbudować instalator .msi
      6) Podsumowanie z rozmiarami obu artefaktów

    Domyślnie buduje OBA formaty (.exe i .msi). Użyj -SkipExe / -SkipMsi by
    pominąć jeden z nich.

.PARAMETER Clean
    Usuwa katalogi build/dist/output przed budowaniem.

.PARAMETER SkipExe
    Pomiń krok Inno Setup (tylko PyInstaller + .msi).

.PARAMETER SkipInno
    Przestarzały alias -SkipExe (zachowany dla wstecznej kompatybilności).

.PARAMETER SkipMsi
    Pomiń krok WiX (tylko PyInstaller + .exe).

.PARAMETER PythonExe
    Ścieżka do python.exe (domyślnie "python" z PATH).

.EXAMPLE
    .\build.ps1                # pełny build: .exe + .msi
    .\build.ps1 -Clean         # clean + pełny build
    .\build.ps1 -SkipMsi       # tylko Inno Setup .exe (stare zachowanie)
    .\build.ps1 -SkipExe       # tylko WiX .msi
    .\build.ps1 -SkipInno      # = -SkipExe (alias)
#>

[CmdletBinding()]
param(
    [switch] $Clean,
    [switch] $SkipExe,
    [switch] $SkipInno,
    [switch] $SkipMsi,
    [string] $PythonExe = "python"
)

$ErrorActionPreference = "Stop"
$here       = Split-Path -Parent $MyInvocation.MyCommand.Path
$root       = Resolve-Path "$here\..\.."
$desktop    = Join-Path $root "desktop"
$innoScript = Join-Path $here "simple_deck.iss"
$pySpec     = Join-Path $here "simple_deck.spec"
$wixSource  = Join-Path $here "simple_deck.wxs"
$outputDir  = Join-Path $here "output"
$venv       = Join-Path $desktop ".venv-build"

# Wersja aplikacji - czytana z pyproject.toml (do nazw plików .exe/.msi i -d AppVersion)
$appVersion = "1.2.1"
$projFile   = Join-Path $desktop "pyproject.toml"
if (Test-Path $projFile) {
    foreach ($line in (Get-Content $projFile)) {
        if ($line -match '^\s*version\s*=\s*"([^"]+)"') {
            $appVersion = $Matches[1]
            break
        }
    }
}
$exeInstaller = Join-Path $outputDir "Simple-Deck-Setup-$appVersion.exe"
$msiInstaller = Join-Path $outputDir "Simple-Deck-$appVersion.msi"

# Przestarzały alias: -SkipInno → -SkipExe
if ($SkipInno -and -not $SkipExe) {
    Write-Warning "-SkipInno jest przestarzały; użyj -SkipExe. Traktuję jako -SkipExe."
    $SkipExe = $true
}

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "=== $msg ===" -ForegroundColor Cyan
}

# ============================================================
#  Detekcja WiX Toolset (zwraca hashtable z wersją i ścieżkami)
# ============================================================
function Find-WixToolset {
    # --- WiX v4: pojedyncze `wix.exe` (.NET global tool) ---
    $wix4Candidates = @()
    $dotnetTools = Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe"
    $wix4Candidates += $dotnetTools
    $wix4Candidates += "wix.exe"  # z PATH
    foreach ($c in $wix4Candidates) {
        try {
            $resolved = if ($c -eq "wix.exe") {
                (Get-Command wix.exe -ErrorAction SilentlyContinue).Source
            } else { $c }
            if ($resolved -and (Test-Path $resolved)) {
                # Dowolny 'wix.exe' to v4+ - v3 nie dystrybuował unified 'wix.exe'
                # (miało osobne candle.exe/light.exe/heat.exe). Nie sprawdzamy
                # wersji: dotnet tool rozdaje v5/v6/v7, wszystkie mają tę samą CLI.
                Write-Host "  WiX v4+ znaleziony: $resolved" -ForegroundColor DarkGray
                return @{ Version = "v4"; WixExe = $resolved }
            }
        } catch { }
    }

    # --- WiX v3: candle.exe + light.exe ---
    $v3Dirs = @(
        "C:\Program Files (x86)\WiX Toolset v3.14\bin",
        "C:\Program Files (x86)\WiX Toolset v3.11\bin"
    )
    $candle = $null; $light = $null; $heat = $null
    # Najpierw z PATH
    $candle = (Get-Command candle.exe -ErrorAction SilentlyContinue).Source
    $light  = (Get-Command light.exe  -ErrorAction SilentlyContinue).Source
    $heat   = (Get-Command heat.exe   -ErrorAction SilentlyContinue).Source
    # Potem z typowych ścieżek instalacyjnych
    if (-not $candle -or -not $light -or -not $heat) {
        foreach ($d in $v3Dirs) {
            if (Test-Path $d) {
                if (-not $candle) { $candle = Join-Path $d "candle.exe" }
                if (-not $light)  { $light  = Join-Path $d "light.exe" }
                if (-not $heat)   { $heat   = Join-Path $d "heat.exe" }
                if ($candle -and $light -and $heat) { break }
            }
        }
    }
    if ($candle -and $light -and $heat -and
        (Test-Path $candle) -and (Test-Path $light) -and (Test-Path $heat)) {
        return @{ Version = "v3"; Candle = $candle; Light = $light; Heat = $heat }
    }

    return $null
}

# ============================================================
#  Sanity checks
# ============================================================
Write-Step "Sanity checks"
if (-not (Test-Path $pySpec))    { throw "Brak: $pySpec" }
if (-not (Test-Path $innoScript)){ throw "Brak: $innoScript" }
if (-not (Test-Path $wixSource)) { throw "Brak: $wixSource" }
if (-not (Test-Path $projFile))  { throw "Brak pyproject.toml w $desktop" }
if (-not (Get-Command $PythonExe -ErrorAction SilentlyContinue)) {
    throw "Brak '$PythonExe' w PATH. Zainstaluj Python 3.10+ z https://python.org"
}
Write-Host "  Wersja aplikacji: $appVersion"

# Wczesna detekcja WiX (by ostrzec, jeśli .msi nie da się zbudować)
if (-not $SkipMsi) {
    $wix = Find-WixToolset
    if ($wix) {
        Write-Host "  WiX $($wix.Version) wykryty" -ForegroundColor Green
    } else {
        Write-Warning "WiX Toolset nie znaleziony - krok .msi zostanie pominięty."
        Write-Warning "  WiX v4 (zalecany):  dotnet tool install -g wix"
        Write-Warning "  WiX v3 (klasyczny): https://wixtoolset.org/releases/v3.14/"
        $SkipMsi = $true
    }
}

# ============================================================
#  Clean
# ============================================================
if ($Clean) {
    Write-Step "Czyszczenie"
    $cleanPaths = @(
        "$here\dist", "$here\build", $outputDir, $venv,
        "$here\simple_deck_heat.wxs",        # heat (v3)
        "$here\simple_deck_heat_v4.wxs",     # wix convert (v4)
        "$here\simple_deck_v4.wxs"
    )
    foreach ($p in $cleanPaths) {
        if (Test-Path $p) {
            Write-Host "  usuwam: $p"
            Remove-Item -Recurse -Force $p
        }
    }
    # Posprzątanie .wixobj/.wixpdb (w build/)
}

# ============================================================
#  Krok 1: Virtualenv
# ============================================================
Write-Step "Krok 1/6: Virtualenv"
if (-not (Test-Path $venv)) {
    & $PythonExe -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "venv create failed" }
}
$python = Join-Path $venv "Scripts\python.exe"
$pip    = Join-Path $venv "Scripts\pip.exe"
& $python -m pip install --upgrade pip wheel setuptools | Out-Null
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed" }

# ============================================================
#  Krok 2: Zależności aplikacji + PyInstaller
# ============================================================
Write-Step "Krok 2/6: Zależności aplikacji + PyInstaller"
& $pip install -e "$desktop[windows]"
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }
& $pip install pyinstaller
if ($LASTEXITCODE -ne 0) { throw "pyinstaller install failed" }

# ============================================================
#  Krok 3: PyInstaller build
# ============================================================
Write-Step "Krok 3/6: PyInstaller build (simple_deck.spec)"
Push-Location $here
try {
    & $python -m PyInstaller $pySpec `
        --noconfirm `
        --clean `
        --distpath "$here\dist" `
        --workpath "$here\build"
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed" }
} finally {
    Pop-Location
}
$appDist = Join-Path $here "dist\Simple-Deck"
if (-not (Test-Path $appDist)) {
    throw "Brak $appDist po buildzie PyInstallerem"
}

# ============================================================
#  Krok 4: Inno Setup (.exe)
# ============================================================
if (-not $SkipExe) {
    Write-Step "Krok 4/6: Inno Setup (.exe)"
    $iscc = $null
    foreach ($candidate in @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
    )) {
        if (Test-Path $candidate) { $iscc = $candidate; break }
    }
    if (-not $iscc) {
        # Z PATH
        $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
    }
    if (-not $iscc) {
        Write-Warning "Inno Setup (ISCC.exe) nie znaleziony - pomijam krok .exe"
        Write-Warning "  Pobierz z: https://jrsoftware.org/isdl.php"
    } else {
        Write-Host "  ISCC: $iscc"
        & $iscc /Q $innoScript
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
    }
} else {
    Write-Host "  (pominięto -SkipExe)"
}

# ============================================================
#  Krok 5: WiX Toolset (.msi)
# ============================================================
if (-not $SkipMsi) {
    Write-Step "Krok 5/6: WiX Toolset (.msi)"

    if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir -Force | Out-Null }

    if ($wix.Version -eq "v4") {
        # === WiX v4: pojedyncza komenda `wix build` ===
        # v4 używa innej przestrzeni nazw XML niż v3 - konwertujemy main source.
        # heat v4 wypluwa natywnie v4, więc go nie konwertujemy.
        $heatV4 = Join-Path $here "simple_deck_heat_v4.wxs"
        $mainV4 = Join-Path $here "simple_deck_v4.wxs"

        Write-Host "  [v4] heat: harvest dist\Simple-Deck -> simple_deck_heat_v4.wxs"
        & $wix.WixExe heat dir "$appDist" `
            -cg AppFiles -dr APPINSTALLDIR `
            -ke -sfrag -srd `
            -out "$heatV4"
        if ($LASTEXITCODE -ne 0) { throw "wix heat failed" }

        Write-Host "  [v4] convert: simple_deck.wxs (v3) -> simple_deck_v4.wxs"
        & $wix.WixExe convert "$wixSource" -o "$mainV4"
        if ($LASTEXITCODE -ne 0) { throw "wix convert failed" }

        Write-Host "  [v4] build: -> output\Simple-Deck-$appVersion.msi"
        & $wix.WixExe build "$mainV4" "$heatV4" `
            -d "AppVersion=$appVersion" `
            -o "$msiInstaller"
        if ($LASTEXITCODE -ne 0) { throw "wix build failed" }

    } else {
        # === WiX v3: candle + light (dwuetapowy) ===
        $heatOut = Join-Path $here "simple_deck_heat.wxs"
        $objDir  = Join-Path $here "build"
        if (-not (Test-Path $objDir)) { New-Item -ItemType Directory -Path $objDir -Force | Out-Null }

        Write-Host "  [v3] heat: harvest dist\Simple-Deck -> simple_deck_heat.wxs"
        & $wix.Heat dir "$appDist" `
            -cg AppFiles -dr APPINSTALLDIR `
            -ke -sfrag -srd -suid `
            -out "$heatOut"
        if ($LASTEXITCODE -ne 0) { throw "heat failed" }

        Write-Host "  [v3] candle: compile .wxs -> .wixobj"
        # WiX v3 candle wymaga '-dName=Value' jako jeden token (bez spacji).
        # PowerShell tokenizuje '-d "Name=Value"' na dwa argumenty.
        & $wix.Candle `
            "-dAppVersion=$appVersion" `
            "-out" "$objDir\" `
            "$wixSource" "$heatOut"
        if ($LASTEXITCODE -ne 0) { throw "candle failed" }

        Write-Host "  [v3] light: link -> output\Simple-Deck-$appVersion.msi"
        & $wix.Light `
            -ext WixUIExtension `
            -ext WixUtilExtension `
            -out "$msiInstaller" `
            (Join-Path $objDir "simple_deck.wixobj") `
            (Join-Path $objDir "simple_deck_heat.wixobj")
        if ($LASTEXITCODE -ne 0) { throw "light failed" }
    }
} else {
    Write-Host "  (pominięto -SkipMsi)"
}

# ============================================================
#  Krok 6: Podsumowanie
# ============================================================
Write-Step "Krok 6/6: Podsumowanie"
$built = @()
if ((-not $SkipExe) -and (Test-Path $exeInstaller)) {
    $sz = [math]::Round((Get-Item $exeInstaller).Length / 1MB, 1)
    Write-Host ("  .exe : {0}  ({1} MB)" -f $exeInstaller, $sz) -ForegroundColor Green
    $built += $exeInstaller
} elseif (-not $SkipExe) {
    Write-Warning "Nie znaleziono: $exeInstaller"
}
if ((-not $SkipMsi) -and (Test-Path $msiInstaller)) {
    $sz = [math]::Round((Get-Item $msiInstaller).Length / 1MB, 1)
    Write-Host ("  .msi : {0}  ({1} MB)" -f $msiInstaller, $sz) -ForegroundColor Green
    $built += $msiInstaller
} elseif (-not $SkipMsi) {
    Write-Warning "Nie znaleziono: $msiInstaller"
}

if ($built.Count -eq 0) {
    Write-Warning "Nie zbudowano żadnego instalatora (folder dist/Simple-Deck/ jest gotowy ręcznie)."
} else {
    Write-Host ""
    Write-Host "  Gotowe: $([string]::Join(', ', $built))" -ForegroundColor Green
}
