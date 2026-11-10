# VM ONLY, after a snapshot. Run elevated (via copy-to-default.bat) from the account where you set up the Vista look,
# BEFORE a plain "sysprep /oobe /reboot". Copies whole registry branches from this user's HKCU into C:\Users\Default's hive,
# except keys that hold per-user paths or shell state (those broke the shell: Start/taskbar layout, shell folder paths, file associations...).
$ErrorActionPreference = 'Continue'
if (-not (Test-Path 'C:\vista-oobe')) { New-Item -ItemType Directory 'C:\vista-oobe' -Force | Out-Null }
$log = 'C:\vista-oobe\copy-to-default.log'
function Log($m) { Add-Content -Path $log -Value $m; Write-Host $m }
Set-Content -Path $log -Value ("copy-to-default " + (Get-Date) + " as " + $env:USERNAME)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Log 'Not elevated - run copy-to-default.bat as administrator.'; exit 1 }
$hive = 'C:\Users\Default\NTUSER.DAT'
if (-not (Test-Path $hive)) { Log "$hive not found"; exit 1 }
reg unload HKU\DefUser 2>$null | Out-Null
reg load HKU\DefUser $hive | Out-Null
$dst = [Microsoft.Win32.Registry]::Users.OpenSubKey('DefUser', $true)
if (-not $dst) { Log 'Could not load the default user hive (is it in use?).'; exit 1 }
$src = [Microsoft.Win32.Registry]::CurrentUser
$script:values = 0; $script:keys = 0

function Copy-Key([string]$path, [string[]]$exclude) {
    $s = $src.OpenSubKey($path)
    if (-not $s) { return }
    $d = $dst.CreateSubKey($path)
    $script:keys++
    foreach ($v in $s.GetValueNames()) {
        try {
            $kind = $s.GetValueKind($v)
            $val  = $s.GetValue($v, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $d.SetValue($v, $val, $kind); $script:values++
        } catch { Log ("  value failed: $path\$v : " + $_.Exception.Message) }
    }
    foreach ($sub in $s.GetSubKeyNames()) {
        if ($exclude -contains $sub) { Log "  skipped  $path\$sub"; continue }
        Copy-Key "$path\$sub" @()
    }
    $d.Close(); $s.Close()
}

$explorerSkip = @('StartPage','StartPage2','CloudStore','UserAssist','RecentDocs','RunMRU','TypedPaths','WordWheelQuery','ComDlg32','FeatureUsage',
                  'SessionInfo','Shell Folders','User Shell Folders','MountPoints2','FileExts','Discardable','Taskband','Search','StartupApproved',
                  'Streams','StreamMRU','Desktop','Package Installation','LowRegistry','BamThrottling','OperationStatusManager','HomeFolder')
$roots = @(
  @{ p = 'Control Panel';                                            x = @('International','Input Method') },
  @{ p = 'AppEvents';                                                x = @() },
  @{ p = 'Console';                                                  x = @() },
  @{ p = 'Software\Microsoft\Windows\DWM';                           x = @() },
  @{ p = 'Software\Microsoft\Windows\CurrentVersion\Themes';         x = @() },
  @{ p = 'Software\Microsoft\Windows\CurrentVersion\ThemeManager';   x = @() },
  @{ p = 'Software\Microsoft\Windows\CurrentVersion\Policies';       x = @() },
  @{ p = 'Software\Policies';                                        x = @() },
  @{ p = 'Software\Microsoft\Windows\CurrentVersion\Explorer';       x = $explorerSkip }
)
foreach ($r in $roots) { Log ("copying " + $r.p); Copy-Key $r.p $r.x }

# per-user keys of customisation tools (Windhawk, OpenGlass, theme tools ...): top-level HKCU\Software\<name> whose name looks like one
$skipVendors = @('Microsoft','Classes','Policies','Wow6432Node','Google','Mozilla','Oracle','Valve','Python')
$pattern = '(?i)glass|aero|explorer|classic|vista|win7|7\+|dwm|theme|startisback|start9|open-?shell|taskbar|windhawk|uxtheme|oldnew|shell'
$sw = $src.OpenSubKey('Software')
foreach ($n in $sw.GetSubKeyNames()) {
    if ($skipVendors -contains $n) { continue }
    if ($n -match $pattern) { Log "copying Software\$n (matched by name)"; Copy-Key "Software\$n" @() }
}
$sw.Close()

# theme files
$themes = Join-Path $env:APPDATA 'Microsoft\Windows\Themes'
if (Test-Path $themes) {
    robocopy $themes 'C:\Users\Default\AppData\Roaming\Microsoft\Windows\Themes' /E /NFL /NDL /NJH /NJS | Out-Null
    Log "copied theme files from $themes"
}

$dst.Close(); [GC]::Collect(); [GC]::WaitForPendingFinalizers(); Start-Sleep -Seconds 1
reg unload HKU\DefUser | Out-Null
Log ("done: " + $script:keys + " keys, " + $script:values + " values copied. Log: $log")
Log 'Next: plain  sysprep /oobe /reboot  (no /generalize, no /unattend).'
