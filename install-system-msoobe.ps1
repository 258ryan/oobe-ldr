$ErrorActionPreference = 'Stop'

$root = 'C:\vista-oobe'
$systemOobe = Join-Path $env:windir 'System32\oobe'
$vistaOobe = Join-Path $root 'oobe'
$backup = Join-Path $root 'original-oobe'
$staging = Join-Path $root 'original-oobe.staging'
$aclManifest = Join-Path $root 'original-oobe-acl.xml'

function Save-OobeAclManifest {
    param([string]$Path, [string]$Manifest)

    $items = @((Get-Item -LiteralPath $Path)) + @(Get-ChildItem -LiteralPath $Path -Force -Recurse)
    $entries = foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        $relativePath = if ($item.FullName -eq $Path) {
            '.'
        } else {
            $item.FullName.Substring($Path.Length).TrimStart('\')
        }
        [pscustomobject]@{
            RelativePath = $relativePath
            Sddl = (Get-Acl -LiteralPath $item.FullName).Sddl
        }
    }
    $entries | Export-Clixml -LiteralPath $Manifest
}

function Restore-OobeAclManifest {
    param([string]$Path, [string]$Manifest)

    foreach ($entry in (Import-Clixml -LiteralPath $Manifest | Sort-Object {
        if ($_.RelativePath -eq '.') { 0 } else { ($_.RelativePath -split '\\').Count }
    })) {
        $destination = if ($entry.RelativePath -eq '.') {
            $Path
        } else {
            Join-Path $Path $entry.RelativePath
        }
        if (-not (Test-Path -LiteralPath $destination)) { continue }

        $acl = if (Test-Path -LiteralPath $destination -PathType Container) {
            New-Object System.Security.AccessControl.DirectorySecurity
        } else {
            New-Object System.Security.AccessControl.FileSecurity
        }
        $acl.SetSecurityDescriptorSddlForm([string]$entry.Sddl)
        Set-Acl -LiteralPath $destination -AclObject $acl
    }
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from an elevated PowerShell prompt.'
}
if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
    throw 'Run this installer from 64-bit PowerShell on 64-bit Windows.'
}
foreach ($required in @(
    (Join-Path $vistaOobe 'msoobe.exe'),
    (Join-Path $vistaOobe 'vistamsoobe.exe'),
    (Join-Path $vistaOobe 'setup.exe'),
    (Join-Path $vistaOobe 'vista-setup.exe'),
    (Join-Path $vistaOobe 'winsetup.dll'),
    (Join-Path $root 'oobelaunch.exe')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required staged OOBE file not found: $required"
    }
}
foreach ($processName in @('msoobe', 'setup', 'setuphost', 'oobe', 'oobeldr')) {
    if (Get-Process -Name $processName -ErrorAction SilentlyContinue) {
        throw "$processName.exe is running. Run this before entering OOBE, after shutting down setup."
    }
}

if (-not (Test-Path -LiteralPath $backup)) {
    if (Test-Path -LiteralPath $staging) {
        throw "An incomplete staging backup exists: $staging. Inspect it before removing or renaming it."
    }
    New-Item -ItemType Directory -Path $staging | Out-Null
    & robocopy.exe $systemOobe $staging /E /XJ /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) {
        throw "Could not back up the original OOBE folder (robocopy exit code $LASTEXITCODE)."
    }
    foreach ($file in @('msoobe.exe', 'setup.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $staging $file) -PathType Leaf)) {
            throw "The staged Windows backup does not contain ${file}: $staging"
        }
    }
    Move-Item -LiteralPath $staging -Destination $backup
    Write-Host "Backed up the original OOBE folder to $backup"
}
foreach ($file in @('msoobe.exe', 'setup.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $backup $file) -PathType Leaf)) {
        throw "The original OOBE backup does not contain ${file}: $backup"
    }
}

if (-not (Test-Path -LiteralPath $aclManifest -PathType Leaf)) {
    Save-OobeAclManifest -Path $systemOobe -Manifest $aclManifest
}

& takeown.exe /A /R /D Y /F $systemOobe | Out-Null
if ($LASTEXITCODE -ne 0) { throw "takeown could not grant temporary OOBE folder access (exit code $LASTEXITCODE)." }
& icacls.exe $systemOobe /grant '*S-1-5-32-544:F' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls could not grant temporary OOBE folder access (exit code $LASTEXITCODE)." }

& robocopy.exe $vistaOobe $systemOobe /MIR /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) {
    throw "Could not replace the complete Windows OOBE folder (robocopy exit code $LASTEXITCODE)."
}

& icacls.exe $systemOobe /reset /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls could not reset the replacement OOBE permissions (exit code $LASTEXITCODE)." }
& icacls.exe $systemOobe /setowner 'NT SERVICE\TrustedInstaller' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not set TrustedInstaller ownership on the replacement OOBE folder (exit code $LASTEXITCODE)." }
Restore-OobeAclManifest -Path $systemOobe -Manifest $aclManifest

$ifeoPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\msoobe.exe'
if (Test-Path -LiteralPath $ifeoPath) {
    $ifeo = Get-ItemProperty -LiteralPath $ifeoPath -Name Debugger -ErrorAction SilentlyContinue
    if ($ifeo.Debugger -and $ifeo.Debugger.Trim('"') -ieq (Join-Path $root 'oobelaunch.exe')) {
        Remove-ItemProperty -LiteralPath $ifeoPath -Name Debugger
        Write-Host 'Removed the old IFEO debugger value for this launcher.'
    }
}

Write-Host "Replaced the complete System32 OOBE folder with the Vista OOBE folder. Original files and ACLs are saved under $backup and $aclManifest."
Write-Host 'Keep the VM snapshot until setup, device configuration, and rollback have all been tested.'
