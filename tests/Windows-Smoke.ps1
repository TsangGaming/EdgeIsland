[CmdletBinding()]
param([Parameter(Mandatory)][string]$Setup, [Parameter(Mandatory)][string]$PayloadDir)
$ErrorActionPreference = 'Stop'
$setupPath = (Resolve-Path -LiteralPath $Setup).Path
$payload = (Resolve-Path -LiteralPath $PayloadDir).Path
$installed = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\EdgeIsland'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Edge Island.lnk'
$arp = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\EdgeIsland'
$pref = 'HKCU:\Software\EdgeIsland'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
function Startup-Entry {
    if (-not (Test-Path -LiteralPath $runKey)) { return $null }
    $key = Get-Item -LiteralPath $runKey -ErrorAction Stop
    try {
        return $key.GetValue('EdgeIsland', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    } finally {
        $key.Dispose()
    }
}
if ($null -ne (Startup-Entry)) { throw 'Use a test account without an existing Edge Island startup entry.' }
foreach ($path in @($installed, $shortcut, $arp, $pref)) {
    if (Test-Path -LiteralPath $path) { throw "Use a clean test account. An existing Edge Island item is present at $path." }
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class IslandSmoke {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string text);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w, out Rect r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr w, out Rect r);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint msg,IntPtr wp,IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint msg,IntPtr wp,IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
    private static extern IntPtr SendMessageW(IntPtr w,uint msg,IntPtr size,StringBuilder text);
    public static string ControlText(IntPtr w,int id) {
        StringBuilder s = new StringBuilder(1024);
        SendMessageW(GetDlgItem(w,id),0x000D,(IntPtr)s.Capacity,s);
        return s.ToString();
    }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
}
'@
[void][IslandSmoke]::SetProcessDPIAware()
function Wait-Until([scriptblock]$Condition, [string]$Message) {
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 150
    }
    throw $Message
}
function Run-Checked([string]$File, [string[]]$Arguments) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(25000)) { throw "Timed out waiting for $File" }
    if ($process.ExitCode -ne 0) { throw "$File exited with $($process.ExitCode)" }
}
function Wizard-Window([System.Diagnostics.Process]$Process) {
    $Process.Refresh()
    if ($Process.HasExited -or $Process.MainWindowHandle -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    $Process.MainWindowHandle
}
function Wizard-Command([IntPtr]$Window, [int]$Id, [int]$Notification = 0) {
    [void][IslandSmoke]::SendMessage($Window,0x111,[IntPtr](($Notification -shl 16) -bor $Id),[IslandSmoke]::GetDlgItem($Window,$Id))
}
function Choose-SetupLanguage([IntPtr]$Window, [int]$Language) {
    $combo = [IslandSmoke]::GetDlgItem($Window,204)
    if ([IslandSmoke]::SendMessage($combo,0x14e,[IntPtr]$Language,[IntPtr]::Zero).ToInt32() -ne $Language) {
        throw 'Could not select a setup language'
    }
    Wizard-Command $Window 204 1  # CBN_SELCHANGE
}
function Find-Island { [IslandSmoke]::FindWindow('EdgeIsland.Overlay.1',$null) }
function Read-Width {
    $r = [IslandSmoke+Rect]::new()
    if (-not [IslandSmoke]::GetWindowRect((Find-Island),[ref]$r)) { return 0 }
    $r.R - $r.L
}
function Move-Outside {
    $y = [IslandSmoke]::GetSystemMetrics(1) - 40
    if (-not [IslandSmoke]::SetCursorPos(24, $y)) { throw 'Cannot move pointer on test desktop' }
}
function Move-Over {
    $r = [IslandSmoke+Rect]::new()
    if (-not [IslandSmoke]::GetWindowRect((Find-Island),[ref]$r)) { throw 'Overlay is missing' }
    if (-not [IslandSmoke]::SetCursorPos([int](($r.L + $r.R) / 2),[int](($r.T + $r.B) / 2))) { throw 'Cannot enter overlay' }
}
function Send-Key([int]$Code) {
    if (-not [IslandSmoke]::PostMessage((Find-Island),0x100,[IntPtr]$Code,[IntPtr]::Zero)) { throw 'Sending keyboard input failed' }
    Start-Sleep -Milliseconds 320
}
function Click-Panel([int]$X,[int]$Y) {
    Start-Sleep -Milliseconds 40  # Allow the final animation frame to install its hitboxes.
    $h = Find-Island
    $r = [IslandSmoke+Rect]::new()
    if (-not [IslandSmoke]::GetClientRect($h,[ref]$r)) { throw 'No overlay client area' }
    $xPixel = [int][Math]::Round($X * $r.R / 396.0)
    $yPixel = [int][Math]::Round($Y * $r.B / 334.0)
    $location = [IntPtr](($yPixel -shl 16) -bor $xPixel)
    [void][IslandSmoke]::PostMessage($h,0x201,[IntPtr]1,$location)
    [void][IslandSmoke]::PostMessage($h,0x202,[IntPtr]0,$location)
    Start-Sleep -Milliseconds 300
}
function Setting([int]$Offset) {
    $bytes = (Get-ItemProperty -LiteralPath $pref -Name Settings).Settings
    [BitConverter]::ToUInt32($bytes,$Offset)
}
function Pull-Capsule([int]$Distance) {
    $h = Find-Island
    $r = [IslandSmoke+Rect]::new()
    if (-not [IslandSmoke]::GetWindowRect($h,[ref]$r)) { throw 'Missing capsule' }
    $scale = ($r.B - $r.T) / 36.0
    $startX = [int](($r.L + $r.R) / 2)
    $startY = [int](($r.T + $r.B) / 2)
    $steps = @(@(0x201, 0), @(0x200, 8), @(0x200, $Distance), @(0x202, $Distance))
    foreach ($step in $steps) {
        [void][IslandSmoke]::GetWindowRect($h,[ref]$r)
        $x = $startX - $r.L
        $y = $startY + [int][Math]::Round($step[1] * $scale) - $r.T
        $xy = [IntPtr](($y -shl 16) -bor ($x -band 0xffff))
        $buttons = if ($step[0] -eq 0x202) { 0 } else { 1 }
        [void][IslandSmoke]::SendMessage($h,[uint32]$step[0],[IntPtr]$buttons,$xy)
    }
    Start-Sleep -Milliseconds 320
}
$originalPointer = [IslandSmoke+Point]::new()
$havePointer = [IslandSmoke]::GetCursorPos([ref]$originalPointer)
$wizardProcess = $null
try {
    Move-Outside
    $wizardProcess = Start-Process -FilePath $setupPath -PassThru
    Wait-Until { (Wizard-Window $wizardProcess) -ne [IntPtr]::Zero } 'Setup Wizard failed to open'
    $wizard = Wizard-Window $wizardProcess
    if ([IslandSmoke]::ControlText($wizard,201) -ne 'Welcome to Edge Island Setup') { throw 'New setup must default to English' }
    Choose-SetupLanguage $wizard 1
    if ([IslandSmoke]::ControlText($wizard,201) -notmatch '[\u4e00-\u9fff]') { throw 'Setup language did not switch to Chinese' }
    Wizard-Command $wizard 2
    if (-not $wizardProcess.WaitForExit(5000) -or $wizardProcess.ExitCode -ne 2) { throw 'Cancelling setup failed' }
    if ((Test-Path $pref) -or (Test-Path $installed) -or (Test-Path $arp)) { throw 'Cancelled setup changed user data' }

    $wizardProcess = Start-Process -FilePath $setupPath -PassThru
    Wait-Until { (Wizard-Window $wizardProcess) -ne [IntPtr]::Zero } 'Could not reopen the Setup Wizard'
    $wizard = Wizard-Window $wizardProcess
    Choose-SetupLanguage $wizard 1
    Wizard-Command $wizard 1
    if ([IslandSmoke]::ControlText($wizard,205) -ne $installed) { throw 'Folder page did not show the installation path' }
    Wizard-Command $wizard 208
    if (-not [IslandSmoke]::IsWindowVisible([IslandSmoke]::GetDlgItem($wizard,204))) { throw 'Back did not return to the Welcome page' }
    Wizard-Command $wizard 1
    Wizard-Command $wizard 1
    if ((Test-Path $installed) -or (Test-Path $arp)) { throw 'Setup wrote files before the Install button' }
    Wizard-Command $wizard 1
    Wait-Until { [IslandSmoke]::IsWindowVisible([IslandSmoke]::GetDlgItem($wizard,207)) } 'No setup completion page'
    $launchBox = [IslandSmoke]::GetDlgItem($wizard,207)
    [void][IslandSmoke]::SendMessage($launchBox,0xF1,[IntPtr]::Zero,[IntPtr]::Zero)  # Uncheck Open Edge Island.
    Wizard-Command $wizard 1
    if (-not $wizardProcess.WaitForExit(5000) -or $wizardProcess.ExitCode -ne 0) { throw 'Interactive setup failed' }
    if ((Get-ItemPropertyValue $pref Language) -ne 1 -or (Find-Island) -ne [IntPtr]::Zero) {
        throw 'Setup did not preserve the selected language or the launch checkbox'
    }
    Run-Checked $setupPath @('/quiet','/nolaunch')  # Reinstall over the same build.
    foreach ($name in 'EdgeIsland.exe','Uninstall.exe') {
        if ((Get-FileHash (Join-Path $installed $name)).Hash -ne (Get-FileHash (Join-Path $payload $name)).Hash) { throw "$name payload mismatch" }
    }
    if (-not (Test-Path $shortcut) -or (Get-ItemProperty -LiteralPath $arp).DisplayName -ne 'Edge Island') { throw 'Missing installation registration' }
    $app = Start-Process -FilePath (Join-Path $installed 'EdgeIsland.exe') -PassThru
    Wait-Until { (Find-Island) -ne [IntPtr]::Zero -and (Test-Path $pref) } 'Overlay did not start'
    Start-Sleep -Milliseconds 900
    $closed = Read-Width
    if ($null -ne (Startup-Entry)) { throw 'Login startup must default to off' }
    $foreground = [IslandSmoke]::GetForegroundWindow()
    Move-Over
    Wait-Until { (Read-Width) -gt $closed * 1.15 } 'Default hover failed to open the capsule'
    Start-Sleep -Milliseconds 260
    $expanded = Read-Width
    if ([IslandSmoke]::GetForegroundWindow() -ne $foreground) { throw 'Hover stole foreground focus' }
    Move-Over  # Move onto panel content, outside the original capsule.
    Start-Sleep -Milliseconds 250
    if ((Read-Width) -ne $expanded) { throw 'Hover collapsed while the pointer was inside the panel' }
    Move-Outside
    Wait-Until { (Read-Width) -eq $closed } 'Leaving the panel did not collapse it'
    Move-Over
    Start-Sleep -Milliseconds 35
    Move-Outside
    Wait-Until { (Read-Width) -eq $closed } 'Fast pointer exit left a stuck panel'
    Move-Over
    Wait-Until { (Read-Width) -eq $expanded } 'Hover did not reopen'
    Send-Key 27
    if ((Read-Width) -ne $closed) { throw 'Escape reopened immediately under a stationary pointer' }
    Move-Outside
    Start-Sleep -Milliseconds 100
    Move-Over
    Wait-Until { (Read-Width) -eq $expanded } 'Re-entering after Escape did not reopen'
    Click-Panel 272 28
    Click-Panel 305 124
    Wait-Until { (Get-ItemPropertyValue $pref HoverExpand) -eq 0 } 'Manual mode was not saved'
    Click-Panel 165 151
    Wait-Until { (Get-ItemPropertyValue $pref Language) -eq 0 } 'English preference was not saved'
    Click-Panel 305 151
    Wait-Until { (Get-ItemPropertyValue $pref Language) -eq 1 } 'Chinese preference was not saved'
    Move-Outside
    Start-Sleep -Milliseconds 280
    if ((Read-Width) -ne $expanded) { throw 'Manual mode collapsed on pointer leave' }
    Send-Key 27
    Move-Over
    Start-Sleep -Milliseconds 280
    if ((Read-Width) -ne $closed) { throw 'Manual mode opened on pointer entry' }
    Move-Outside
    Pull-Capsule 20
    Wait-Until { (Read-Width) -eq $closed } 'A short pull must retract'
    Pull-Capsule 64
    Wait-Until { (Read-Width) -ge $expanded * 0.98 } 'Mouse pull failed to expand the fixed capsule'
    Send-Key 27
    [void][IslandSmoke]::SendMessage((Find-Island),0x20a,[IntPtr](-120 -shl 16),[IntPtr]::Zero)
    Wait-Until { (Read-Width) -ge $expanded * 0.98 } 'Wheel down did not open the capsule'
    Start-Sleep -Milliseconds 250
    Click-Panel 160 30   # Timer tab.
    Click-Panel 60 218   # 5 minutes.
    Click-Panel 140 282
    Wait-Until { (Setting 32) -eq 1 -and (Setting 28) -eq 5 } 'Timer did not start'
    Click-Panel 140 282
    Wait-Until { (Setting 32) -eq 2 } 'Timer did not pause'
    Click-Panel 300 283
    Wait-Until { (Setting 32) -eq 0 } 'Timer did not reset'
    Click-Panel 272 28   # Settings tab.
    Click-Panel 345 252  # Music clock (on by default).
    Wait-Until { (Get-ItemPropertyValue $pref MediaClock) -eq 0 } 'Clock preference did not switch off'
    $expectedRun = '"' + (Join-Path $installed 'EdgeIsland.exe') + '"'
    Click-Panel 344 276
    Wait-Until { (Startup-Entry) -eq $expectedRun } 'Startup did not register the quoted installed path'
    Click-Panel 344 276
    Wait-Until { $null -eq (Startup-Entry) } 'Switching off left a startup value'
    Click-Panel 344 276  # Leave on to exercise restart, upgrade and uninstall.
    Click-Panel 265 91   # Edge docking.
    Wait-Until { (Setting 4) -eq 0 } 'Docking preference did not change'
    Run-Checked $setupPath @('/quiet','/nolaunch')
    Wait-Until { (Find-Island) -eq [IntPtr]::Zero } 'Upgrade did not close the previous instance'
    if ((Startup-Entry) -ne $expectedRun -or (Get-ItemPropertyValue $pref MediaClock) -ne 0 -or
        (Get-ItemPropertyValue $pref HoverExpand) -ne 0 -or (Get-ItemPropertyValue $pref Language) -ne 1 -or (Setting 4) -ne 0) {
        throw 'Upgrade discarded user preferences'
    }
    $app = Start-Process -FilePath (Join-Path $installed 'EdgeIsland.exe') -PassThru
    Wait-Until { (Find-Island) -ne [IntPtr]::Zero } 'Relaunch failed'
    Move-Over
    Start-Sleep -Milliseconds 300
    if ((Read-Width) -eq $expanded) { throw 'Relaunch lost the manual interaction setting' }
    Send-Key 13
    Click-Panel 272 28
    Click-Panel 165 151
    Wait-Until { (Get-ItemPropertyValue $pref Language) -eq 0 } 'Relaunch did not restore the Chinese preference'
    Click-Panel 345 252
    Wait-Until { (Get-ItemPropertyValue $pref MediaClock) -eq 1 } 'Relaunch lost the disabled clock setting'
    Click-Panel 344 276
    Wait-Until { $null -eq (Startup-Entry) } 'Relaunch did not reflect the registered startup state'
    Click-Panel 344 276
    Click-Panel 165 124
    Wait-Until { (Get-ItemPropertyValue $pref HoverExpand) -eq 1 } 'Hover mode was not saved'
    Move-Outside
    Wait-Until { (Read-Width) -lt $expanded * 0.85 } 'Re-enabled hover did not close on leave'
    Send-Key 27
    $cpuBefore = $app.TotalProcessorTime.TotalMilliseconds
    Start-Sleep -Seconds 4
    $app.Refresh()
    Write-Host ("Runner sample: working set {0:N1} MiB, CPU {1:N0} ms over 4 seconds" -f ($app.WorkingSet64 / 1MB),($app.TotalProcessorTime.TotalMilliseconds - $cpuBefore))
    # Uninstall while the desktop application is running. Wait for native self-cleanup.
    Run-Checked (Join-Path $installed 'Uninstall.exe') @('/quiet')
    Wait-Until { -not (Test-Path $installed) -and -not (Test-Path $arp) -and -not (Test-Path $pref) } 'Installed uninstaller left project-owned files or registration'
    if ((Test-Path $shortcut) -or $null -ne (Startup-Entry)) { throw 'Shortcut or startup entry was left behind' }
    Run-Checked $setupPath @('/quiet','/nolaunch')
    # Exact v0.1.0 Settings layout, with a paused timer and right-edge docking.
    $legacy = [byte[]]::new(112)
    [uint32[]]$fields = @(1, 0, 1, 1, 0, 1, 7000, 15, 2, 693)
    for ($i = 0; $i -lt $fields.Length; $i++) {
        [BitConverter]::GetBytes($fields[$i]).CopyTo($legacy, $i * 4)
    }
    New-Item -Path $pref -Force | Out-Null
    New-ItemProperty -Path $pref -Name Settings -PropertyType Binary -Value $legacy | Out-Null
    $app = Start-Process -FilePath (Join-Path $installed 'EdgeIsland.exe') -PassThru
    Wait-Until { (Find-Island) -ne [IntPtr]::Zero -and (Read-Width) -gt 10 } 'Legacy profile failed to open'
    if ((Read-Width) -ge $expanded * 0.95) { throw 'Legacy profile was treated as first run' }
    Send-Key 13
    Click-Panel 272 28
    Click-Panel 345 228  # Save Settings after changing the legacy sound preference.
    if ((Setting 4) -ne 0 -or (Setting 16) -ne 1 -or (Setting 20) -ne 1 -or (Setting 24) -ne 7000 -or
        (Setting 28) -ne 15 -or (Setting 32) -ne 2 -or (Setting 36) -ne 693) { throw 'Legacy settings were lost' }
    Click-Panel 345 252
    if ((Get-ItemPropertyValue $pref MediaClock) -ne 0 -or $null -ne (Startup-Entry)) { throw 'Legacy profile defaults incorrect' }
    Run-Checked (Join-Path $payload 'Uninstall.exe') @('/quiet')
    Wait-Until { -not (Test-Path $installed) -and -not (Test-Path $arp) } 'Standalone uninstaller did not complete'
    Write-Host 'Setup Wizard, language preferences, upgrade, hover, manual gestures, timer and uninstall passed.'
}
finally {
    if ($havePointer) { [void][IslandSmoke]::SetCursorPos($originalPointer.X, $originalPointer.Y) }
    if ($null -ne $wizardProcess -and -not $wizardProcess.HasExited) { Stop-Process -Id $wizardProcess.Id }
    if (Test-Path -LiteralPath $arp) {
        & { $p = Start-Process -FilePath (Join-Path $payload 'Uninstall.exe') -ArgumentList '/quiet' -PassThru; [void]$p.WaitForExit(12000) }
    }
}
