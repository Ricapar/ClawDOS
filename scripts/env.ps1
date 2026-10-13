# Dot-source to set up the build environment:  . .\scripts\env.ps1
$root = Split-Path -Parent $PSScriptRoot
$env:CLAUDEXT_ROOT = $root
$env:WATCOM  = "$root\tools\openwatcom"
$env:INCLUDE = "$env:WATCOM\h"
$env:EDPATH  = "$env:WATCOM\eddat"
$env:PATH    = "$env:WATCOM\binnt64;$env:WATCOM\binnt;$env:LOCALAPPDATA\bin\NASM;C:\DOSBox-X;$env:PATH"
$box = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Directory -Filter '86Box.86Box_*' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($box) { $env:PATH = "$($box.FullName);$env:PATH" }
