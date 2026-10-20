# Downloads and extracts the project toolchain into ./tools (gitignored).
# Idempotent: skips anything already present. System-wide tools (NASM, 86Box)
# are installed via winget.

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root 'tools'
$dl    = Join-Path $tools 'downloads'
New-Item -ItemType Directory -Force $dl | Out-Null

$owBuild   = '2026-10-01-Build'
$mtcpVer   = '2026-10-10'
$fdVer     = '1.4'
$romsVer   = 'v6.0'
$dbxVer    = '2026.10.01'

function Get-File($url, $name) {
    $dest = Join-Path $dl $name
    if (-not (Test-Path $dest)) {
        Write-Host "Downloading $name"
        curl.exe -sSL -o $dest $url
        if ($LASTEXITCODE) { throw "download failed: $url" }
    }
    $dest
}

# winget packages
foreach ($id in 'NASM.NASM', '86Box.86Box') {
    winget list --id $id -e --accept-source-agreements | Out-Null
    if ($LASTEXITCODE) {
        winget install --id $id -e --silent --accept-source-agreements --accept-package-agreements
    }
}

# DOSBox-X, MinGW portable build: unlike the winget (Visual Studio) build it
# has the SLiRP network backend, so the emulated NE2000 works without Npcap.
if (-not (Test-Path "$tools\dosbox-x\mingw-build\mingw\dosbox-x.exe")) {
    $f = Get-File "https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v$dbxVer/dosbox-x-mingw64-$dbxVer-portable.zip" "dosbox-x-mingw64-$dbxVer.zip"
    Expand-Archive -Force $f "$tools\dosbox-x"
}

# OpenWatcom 2 (portable snapshot)
if (-not (Test-Path "$tools\openwatcom\binnt64\wcl.exe")) {
    $f = Get-File "https://github.com/open-watcom/open-watcom-v2/releases/download/$owBuild/ow-snapshot.tar.xz" 'ow-snapshot.tar.xz'
    New-Item -ItemType Directory -Force "$tools\openwatcom" | Out-Null
    tar -xJf $f -C "$tools\openwatcom"
}

# mTCP binaries + source
if (-not (Test-Path "$tools\mtcp\htget.exe")) {
    $f = Get-File "https://www.brutman.com/mTCP/download/mTCP_$mtcpVer.zip" "mTCP_$mtcpVer.zip"
    Expand-Archive -Force $f "$tools\mtcp"
}
if (-not (Test-Path "$tools\mtcp-src")) {
    $f = Get-File "https://www.brutman.com/mTCP/download/mTCP-src_$mtcpVer.zip" "mTCP-src_$mtcpVer.zip"
    Expand-Archive -Force $f "$tools\mtcp-src"
}

# FreeDOS
if (-not (Test-Path "$tools\freedos\floppy")) {
    $f = Get-File "https://download.freedos.org/$fdVer/FD14-FloppyEdition.zip" 'FD14-FloppyEdition.zip'
    Expand-Archive -Force $f "$tools\freedos\floppy"
}
if (-not (Test-Path "$tools\freedos\liteusb")) {
    $f = Get-File "https://download.freedos.org/$fdVer/FD14-LiteUSB.zip" 'FD14-LiteUSB.zip'
    Expand-Archive -Force $f "$tools\freedos\liteusb"
}

# FreeDOS packages (zips), pulled out of the LiteUSB image
if (-not (Test-Path "$tools\freedos\packages\BASE\KERNEL.ZIP")) {
    python "$PSScriptRoot\fatimg.py" get "$tools\freedos\liteusb\FD14LITE.img" PACKAGES "$tools\freedos\packages"
}

# Crynwr packet drivers (generic NE2000 for emulators), from the FreeDOS net repo
if (-not (Test-Path "$tools\freedos\packages\NET\CRYNWR.ZIP")) {
    New-Item -ItemType Directory -Force "$tools\freedos\packages\NET" | Out-Null
    $f = Get-File "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/$fdVer/net/crynwr.zip" 'crynwr.zip'
    Copy-Item $f "$tools\freedos\packages\NET\CRYNWR.ZIP"
}

# PicoMEM DOS drivers
$pmDrivers = 'ASTCLOCK.COM', 'NE2000.COM', 'PICOMEM.EXE', 'PM2000.COM', 'PMDFS.EXE',
             'PMDFS3.EXE', 'PMEMM.EXE', 'PMINIT.EXE', 'PMMOUSE.EXE', 'README.md'
New-Item -ItemType Directory -Force "$tools\picomem" | Out-Null
foreach ($d in $pmDrivers) {
    if (-not (Test-Path "$tools\picomem\$d")) {
        curl.exe -sSfL -o "$tools\picomem\$d" "https://raw.githubusercontent.com/FreddyVRetro/ISA-PicoMEM/main/drivers/$d"
    }
}

# 86Box ROM set (goes next to the winget-installed 86Box.exe)
$box = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Directory -Filter '86Box.86Box_*' | Select-Object -First 1
if ($box -and -not (Test-Path "$($box.FullName)\roms\machines")) {
    $f = Get-File "https://github.com/86Box/roms/archive/refs/tags/$romsVer.zip" "86box-roms-$romsVer.zip"
    $tmp = Join-Path $dl 'roms-tmp'
    Expand-Archive -Force $f $tmp
    Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName "$($box.FullName)\roms"
    Remove-Item -Recurse $tmp
}

Write-Host 'Toolchain ready. Dot-source scripts\env.ps1 to put it on PATH.'
