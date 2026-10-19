# Capture the Vita3K game window's client area (the game's image, no title bar) to a PNG.
#   powershell -NoProfile -File tools/vita3k/grab.ps1 <out.png>
#
# PrintWindow with PW_RENDERFULLCONTENT asks the window for its own content, so a window
# covered by another one, or a refused SetForegroundWindow (which Windows does to a
# background process), still grabs the game. Copying the screen instead grabs whatever
# lies on top. A grab that comes back flat black (the window minimised, or its content not
# composed) is reported as FAILED, never saved as a frame.
#
# The game runs in a window of its own, "<name> (<TITLE ID>) | <backend> | ...": the
# process's main window is the library (and an "Update Available" dialog can sit on top),
# so the window is found by the title ID in its title, not taken as the main one.
param([Parameter(Mandatory = $true)][string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class VitaWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    public struct RECT { public int L, T, R, B; }
    public struct POINT { public int X, Y; }
}
"@
$PW_RENDERFULLCONTENT = 2
$p = Get-Process Vita3K -ErrorAction Stop | Select-Object -First 1
$script:game = [IntPtr]::Zero
$script:title = ""
[VitaWin]::EnumWindows({ param($w, $l)
    $procId = 0
    [VitaWin]::GetWindowThreadProcessId($w, [ref]$procId) | Out-Null
    if ($procId -eq $p.Id -and [VitaWin]::IsWindowVisible($w)) {
        $t = New-Object System.Text.StringBuilder 512
        [VitaWin]::GetWindowText($w, $t, 512) | Out-Null
        if ($t.ToString() -match '\([A-Z]{4}[0-9]{5}\)') { $script:game = $w; $script:title = $t.ToString() }
    }
    $true
}, [IntPtr]::Zero) | Out-Null
if ($script:game -eq [IntPtr]::Zero) { "vita3k: grab FAILED (no game window) for $Out"; exit 1 }
$h = $script:game
$w = New-Object VitaWin+RECT
[VitaWin]::GetWindowRect($h, [ref]$w) | Out-Null
$c = New-Object VitaWin+RECT
[VitaWin]::GetClientRect($h, [ref]$c) | Out-Null
$o = New-Object VitaWin+POINT
[VitaWin]::ClientToScreen($h, [ref]$o) | Out-Null

$whole = New-Object System.Drawing.Bitmap ($w.R - $w.L), ($w.B - $w.T)
$g = [System.Drawing.Graphics]::FromImage($whole)
$hdc = $g.GetHdc()
$ok = [VitaWin]::PrintWindow($h, $hdc, $PW_RENDERFULLCONTENT)
$g.ReleaseHdc($hdc)
$g.Dispose()
if (-not $ok) { "vita3k: grab FAILED (PrintWindow refused) for $Out"; exit 1 }

# The client area inside the whole-window image.
$client = New-Object System.Drawing.Rectangle ($o.X - $w.L), ($o.Y - $w.T), ($c.R - $c.L), ($c.B - $c.T)
$bmp = $whole.Clone($client, $whole.PixelFormat)
$whole.Dispose()

# Flat black = nothing captured: say so rather than hand back an empty frame.
$lit = $false
for ($y = 0; $y -lt $bmp.Height -and -not $lit; $y += 17) {
    for ($x = 0; $x -lt $bmp.Width; $x += 17) {
        $px = $bmp.GetPixel($x, $y)
        if ($px.R + $px.G + $px.B -gt 24) { $lit = $true; break }
    }
}
if (-not $lit) { "vita3k: grab FAILED (flat black) for $Out"; exit 1 }
$bmp.Save($Out)
"vita3k: grabbed $Out ($($bmp.Width)x$($bmp.Height)) from '$script:title'"
