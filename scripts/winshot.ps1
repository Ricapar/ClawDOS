# Capture a screenshot of a process's main window (default: 86Box) to a PNG.
#   .\scripts\winshot.ps1 [-Process 86Box] [-Out build\shot.png] [-Delay 0]
param(
    [string]$Process = '86Box',
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\shot.png'),
    [int]$Delay = 0
)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WinShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[WinShot]::SetProcessDPIAware() | Out-Null
if ($Delay) { Start-Sleep -Seconds $Delay }
$p = Get-Process -Name $Process -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { throw "no window for $Process" }
$r = New-Object WinShot+RECT
[WinShot]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WinShot]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Output $Out
