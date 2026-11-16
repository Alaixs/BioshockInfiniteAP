<#
.SYNOPSIS
    Builds the mod (xinput1_3.dll) and the apworld, and optionally installs or tests them.

.EXAMPLE
    .\build.ps1                 # build mod\bin\Release\xinput1_3.dll and apworld\build\bioshock_infinite.apworld
    .\build.ps1 -Install        # ... then copy the DLL into the game and the apworld into Archipelago
    .\build.ps1 -Test           # ... then run the loop test against a local Archipelago server
    .\build.ps1 -Format         # only format the sources (the rewriting hooks of .pre-commit-config.yaml)
    .\build.ps1 -Lint           # only run every check of the CI (pre-commit on all files)

.NOTES
    Needs CMake, Visual Studio 2022 (C++ desktop workload) and Python 3.11+ with requirements-dev.txt.
    The project's virtual environment (.venv) is used when there is one.
    The game folder is found in the Steam libraries; use -GameDir (or BIOSHOCK_INFINITE_DIR) otherwise.
    -Test uses the Archipelago install of -ArchipelagoDir. With a source checkout instead, it also runs
    the apworld's tests (apworld/run_tests.py), which need Archipelago's requirements in that Python.
#>
param(
    [switch]$Install,
    [switch]$Test,
    [switch]$Format,
    [switch]$Lint,
    [string]$GameDir = $env:BIOSHOCK_INFINITE_DIR,
    [string]$ArchipelagoDir = "C:\ProgramData\Archipelago"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$python = Join-Path $root ".venv\Scripts\python.exe"
if (-not (Test-Path $python)) { $python = "python" }

function Invoke-Step([string]$name, [scriptblock]$block) {
    Write-Host "==> $name" -ForegroundColor Cyan
    # Native tools write progress and warnings to stderr: only their exit code counts.
    $ErrorActionPreference = "Continue"
    & $block 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE) { throw "$name failed (exit code $LASTEXITCODE)" }
}

function Find-GameDir {
    $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    $libraries = @()
    if ($steam) {
        $libraries += $steam
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            $libraries += Select-String -Path $vdf -Pattern '"path"\s+"(.+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
        }
    }
    foreach ($library in $libraries) {
        $dir = Join-Path $library "steamapps\common\BioShock Infinite"
        if (Test-Path (Join-Path $dir "Binaries\Win32\BioShockInfinite.exe")) { return $dir }
    }
    return $null
}

if ($Format -or $Lint) {
    Push-Location $root
    try {
        & $python -m pre_commit --version *> $null
        if ($LASTEXITCODE) { throw "pre-commit not found: $python -m pip install -r requirements-dev.txt" }
        if ($Format) {
            # The hooks that rewrite files; their exit code only says that they changed something.
            foreach ($hook in "fix-byte-order-marker", "mixed-line-ending", "trailing-whitespace", "end-of-file-fixer",
                "clang-format", "ruff-check", "ruff-format") {
                & $python -m pre_commit run $hook --all-files
            }
            $global:LASTEXITCODE = 0
        } else {
            Invoke-Step "pre-commit run --all-files" { & $python -m pre_commit run --all-files --show-diff-on-failure }
        }
    } finally {
        Pop-Location
    }
    return
}

Invoke-Step "Configure" { cmake -S "$root\mod" -B "$root\mod\build" -A Win32 }
Invoke-Step "Build the mod and the loop test" { cmake --build "$root\mod\build" --config Release --target BioShockAP loop_test }
Invoke-Step "Package the apworld" { & $python "$root\apworld\build.py" }

if ($Install) {
    if (-not $GameDir) { $GameDir = Find-GameDir }
    if (-not $GameDir) { throw "BioShock Infinite not found: pass -GameDir or set BIOSHOCK_INFINITE_DIR." }
    $target = Join-Path $GameDir "Binaries\Win32"
    Write-Host "==> Install the mod in $target" -ForegroundColor Cyan
    try {
        Copy-Item "$root\mod\bin\Release\xinput1_3.dll" $target -Force
    } catch {
        throw "Cannot copy the DLL (is the game running?): $_"
    }
    Invoke-Step "Install the apworld" { & $python "$root\apworld\build.py" --install (Join-Path $ArchipelagoDir "custom_worlds") }
}

if ($Test) {
    Invoke-Step "Loop test" { & $python "$root\mod\tests\run_loop_test.py" $ArchipelagoDir }
    if (Test-Path (Join-Path $ArchipelagoDir "Generate.py")) {
        Invoke-Step "Apworld tests" { & $python "$root\apworld\run_tests.py" $ArchipelagoDir }
    }
}
