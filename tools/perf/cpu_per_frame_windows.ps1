# cpu_per_frame_windows.ps1: while ben10_poe.exe runs, every 10 s writes the
# frame rate (from the game's [fps] lines) and, for each busy thread, the CPU
# time it used per second and per game frame: the Windows counterpart of
# tools/perf/cpu_per_frame.sh. A thread at about 1000 ms per second is using a
# whole core. Threads are named by their Windows thread description
# (GameThread), "main" for the process's first thread, or tNNNN.
#
# Use: start the game with BEN10_FPS_LOG=1 and its output redirected to a log,
# then, alongside it (e.g. with `start` in the same .bat):
#   powershell -ExecutionPolicy Bypass -File tools\perf\cpu_per_frame_windows.ps1 -GameLog <log> -Out <table>
# It waits up to 2 minutes for the process and stops when the game exits.
param([string]$GameLog, [string]$Out, [int]$Interval = 10)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ThreadNames {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int id);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    public static string Name(int id) {
        IntPtr h = OpenThread(0x0800, false, id);   // THREAD_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return "";
        string s = "";
        IntPtr p;
        if (GetThreadDescription(h, out p) >= 0 && p != IntPtr.Zero) { s = Marshal.PtrToStringUni(p); LocalFree(p); }
        CloseHandle(h);
        return s;
    }
}
"@
Set-Content -Path $Out -Value "cpu-monitor $(Get-Date -Format s), interval $Interval s"
$proc = $null
for ($i = 0; $i -lt 120 -and -not $proc; $i++) { Start-Sleep 1; $proc = Get-Process -Name ben10_poe -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $proc) { Add-Content $Out 'game process not found'; exit 1 }
$mainId = ($proc.Threads | Sort-Object StartTime | Select-Object -First 1).Id
function Frames { $n = 0; foreach ($l in (Get-Content -Path $GameLog -ErrorAction SilentlyContinue)) { if ($l -match '^\[fps\] game frames (\d+)') { $n += [int]$Matches[1] } }; $n }
function Snap { $h = @{}; $proc.Refresh(); foreach ($t in $proc.Threads) { try { $h[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }; $h }
$a = Snap; $fa = Frames; $ta = Get-Date
while (-not $proc.HasExited) {
  Start-Sleep $Interval
  if ($proc.HasExited) { break }
  $b = Snap; $fb = Frames; $tb = Get-Date
  $frames = $fb - $fa; $wall = ($tb - $ta).TotalSeconds
  $line = "{0:HH:mm:ss}  {1,4} game frames in {2:N1} s = {3,5:N1} fps |" -f $tb, $frames, $wall, ($frames / $wall)
  $rows = foreach ($id in $b.Keys) {
    if ($a.ContainsKey($id)) {
      $d = $b[$id] - $a[$id]
      if ($d -gt 200 * $Interval / 10) {
        $n = [ThreadNames]::Name($id); if ($id -eq $mainId) { $n = 'main' }; if (-not $n) { $n = "t$id" }
        [pscustomobject]@{ Name = $n; MsPerSec = $d / $wall; MsPerFrame = $(if ($frames -gt 0) { $d / $frames } else { 0 }) }
      }
    }
  }
  foreach ($r in ($rows | Sort-Object MsPerSec -Descending | Select-Object -First 10)) {
    $line += " {0} {1:N0}ms/s {2:N0}ms/f;" -f $r.Name, $r.MsPerSec, $r.MsPerFrame
  }
  Add-Content -Path $Out -Value $line
  $a = $b; $fa = $fb; $ta = $tb
}
Add-Content -Path $Out -Value 'game exited'
