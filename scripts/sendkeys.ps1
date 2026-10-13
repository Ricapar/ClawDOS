# Type into an emulator window (default: 86Box) by injecting hardware scan codes,
# which emulators reading raw keyboard input will see (SendKeys is not enough).
#   .\scripts\sendkeys.ps1 'CLAUDE{ENTER}'
#   .\scripts\sendkeys.ps1 'hello{ENTER}' -Process 86Box -DelayMs 60
# Special keys: {ENTER} {ESC} {BS} {TAB} {F1}..{F10} {PGUP} {PGDN} {UP} {DOWN} {LEFT} {RIGHT}
param(
    [Parameter(Mandatory, Position = 0)][string]$Keys,
    [string]$Process = '86Box',
    [int]$DelayMs = 60
)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KeyInject {
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit, Size = 40)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] static extern short VkKeyScan(char c);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    // Windows only lets a process take focus right after input; a lone Alt tap counts.
    public static bool Focus(IntPtr h) {
        for (int i = 0; i < 5 && GetForegroundWindow() != h; i++) {
            keybd_event(0x12, 0, 0, IntPtr.Zero);
            keybd_event(0x12, 0, KEYUP, IntPtr.Zero);
            SetForegroundWindow(h);
            System.Threading.Thread.Sleep(100);
        }
        return GetForegroundWindow() == h;
    }
    const uint SCANCODE = 8, KEYUP = 2, EXTENDED = 1;
    static void Send(ushort scan, bool up, bool ext) {
        var i = new INPUT[1];
        i[0].type = 1;
        i[0].ki.scan = scan;
        i[0].ki.flags = SCANCODE | (up ? KEYUP : 0) | (ext ? EXTENDED : 0);
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Tap(ushort scan, bool shift, bool ext) {
        if (shift) Send(0x2A, false, false);
        Send(scan, false, ext);
        System.Threading.Thread.Sleep(15);
        Send(scan, true, ext);
        if (shift) Send(0x2A, true, false);
    }
    public static void Char(char c) {
        short v = VkKeyScan(c);
        if (v == -1) return;
        ushort scan = (ushort)MapVirtualKey((uint)(v & 0xFF), 0);
        Tap(scan, (v & 0x100) != 0, false);
    }
}
'@
$special = @{
    'ENTER' = @(0x1C, $false); 'ESC' = @(0x01, $false); 'BS' = @(0x0E, $false); 'TAB' = @(0x0F, $false)
    'F1' = @(0x3B, $false); 'F2' = @(0x3C, $false); 'F3' = @(0x3D, $false); 'F4' = @(0x3E, $false)
    'F5' = @(0x3F, $false); 'F6' = @(0x40, $false); 'F7' = @(0x41, $false); 'F8' = @(0x42, $false)
    'F9' = @(0x43, $false); 'F10' = @(0x44, $false)
    'PGUP' = @(0x49, $true); 'PGDN' = @(0x51, $true); 'UP' = @(0x48, $true); 'DOWN' = @(0x50, $true)
    'LEFT' = @(0x4B, $true); 'RIGHT' = @(0x4D, $true)
}
$p = Get-Process -Name $Process -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not [KeyInject]::Focus($p.MainWindowHandle)) { throw "could not focus $Process" }
Start-Sleep -Milliseconds 300
foreach ($t in [regex]::Matches($Keys, '\{[^}]+\}|.')) {
    $v = $t.Value
    if ($v.Length -gt 2 -and $v.StartsWith('{')) {
        $k = $special[$v.Trim('{}').ToUpper()]
        if (-not $k) { throw "unknown key $v" }
        [KeyInject]::Tap([uint16]$k[0], $false, $k[1])
    } else {
        [KeyInject]::Char($v[0])
    }
    Start-Sleep -Milliseconds $DelayMs
}
