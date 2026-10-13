# Downloads and extracts the project toolchain into ./tools (gitignored).
# Idempotent: skips anything already present. System-wide tools (NASM, 86Box,
# DOSBox-X) are installed via winget.

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root 'tools'
$dl    = Join-Path $tools 'downloads'
New-Item -ItemType Directory -Force $dl | Out-Null

$owBuild   = '2026-10-01-Build'
$mtcpVer   = '2026-10-10'
$fdVer     = '1.4'
$romsVer   = 'v6.0'

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
foreach ($id in 'NASM.NASM', '86Box.86Box', 'joncampbell123.DOSBox-X') {
    winget list --id $id -e --accept-source-agreements | Out-Null
    if ($LASTEXITCODE) {
        winget install --id $id -e --silent --accept-source-agreements --accept-package-agreements
    }
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
