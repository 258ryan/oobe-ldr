$ErrorActionPreference = 'Stop'

$root = 'C:\vista-oobe'
$systemOobe = Join-Path $env:windir 'System32\oobe'
$target = Join-Path $systemOobe 'msoobe.exe'
$launcher = Join-Path $root 'oobelaunch.exe'
$backup = Join-Path $root 'original-oobe'
$staging = Join-Path $root 'original-oobe.staging'
$aclBackup = Join-Path $backup 'msoobe.acl.sddl'
$installedHashFile = Join-Path $root 'msoobe-launcher.sha256'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from an elevated PowerShell prompt.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The bundled launcher is 64-bit and requires 64-bit Windows.'
}
if (-not [Environment]::Is64BitProcess) {
    throw 'Run this script from 64-bit PowerShell so System32 resolves to the native system directory.'
}
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Launcher not found: $launcher"
}
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
    throw "Windows OOBE executable not found: $target"
}
if (Get-Process -Name msoobe -ErrorAction SilentlyContinue) {
    throw 'msoobe.exe is running. Run this before entering OOBE, after shutting down setup.'
}

$launcherHash = (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash
$targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
$backupExe = Join-Path $backup 'msoobe.exe'

if (-not (Test-Path -LiteralPath $backupExe -PathType Leaf)) {
    if (Test-Path -LiteralPath $backup) {
        throw "The backup folder exists but has no msoobe.exe: $backup. Preserve it and resolve the incomplete backup before continuing."
    }

    if (Test-Path -LiteralPath $staging) {
        throw "An incomplete staging backup exists: $staging. Inspect it before removing or renaming it."
    }

    New-Item -ItemType Directory -Path $staging | Out-Null
    & robocopy.exe $systemOobe $staging /E /XJ /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) {
        throw "Could not back up the original OOBE folder (robocopy exit code $LASTEXITCODE)."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $staging 'msoobe.exe') -PathType Leaf)) {
        throw "The staged backup does not contain msoobe.exe: $staging"
    }

    (Get-Acl -LiteralPath $target).Sddl | Set-Content -LiteralPath (Join-Path $staging 'msoobe.acl.sddl')
    Move-Item -LiteralPath $staging -Destination $backup
    Write-Host "Backed up the original OOBE folder to $backup"
}
elseif ($targetHash -ne $launcherHash) {
    $backupHash = (Get-FileHash -LiteralPath $backupExe -Algorithm SHA256).Hash
    $knownInstalledHash = if (Test-Path -LiteralPath $installedHashFile) {
        (Get-Content -LiteralPath $installedHashFile -Raw).Trim()
    } else {
        ''
    }
    if ($targetHash -ne $backupHash -and $targetHash -ne $knownInstalledHash) {
        throw 'System msoobe.exe differs from both the saved original and the previously installed launcher. It may have been serviced; take a snapshot and refresh the backup before replacing it.'
    }
}

$newHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($newHash -eq $launcherHash) {
    Write-Host 'The replacement launcher is already installed.'
} else {
    $replacement = "$target.new"
    if (Test-Path -LiteralPath $replacement) {
        throw "A staged replacement already exists: $replacement. Inspect it before continuing."
    }

    $originalAcl = Get-Acl -LiteralPath $target
    try {
        & takeown.exe /A /F $target | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "takeown failed with exit code $LASTEXITCODE." }
        & icacls.exe $target /grant '*S-1-5-32-544:F' /C | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "icacls could not grant temporary access (exit code $LASTEXITCODE)." }

        Copy-Item -LiteralPath $launcher -Destination $replacement
        [IO.File]::Replace($replacement, $target, $null)

        try {
            if (Test-Path -LiteralPath $aclBackup) {
                $savedAcl = New-Object System.Security.AccessControl.FileSecurity
                $savedAcl.SetSecurityDescriptorSddlForm((Get-Content -LiteralPath $aclBackup -Raw))
                Set-Acl -LiteralPath $target -AclObject $savedAcl
            } else {
                Set-Acl -LiteralPath $target -AclObject $originalAcl
            }
        } catch {
            Write-Warning "Could not restore the saved file ACL: $($_.Exception.Message)"
            & icacls.exe $target /reset /C | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "icacls could not reset the file ACL (exit code $LASTEXITCODE)." }
        }
        & icacls.exe $target /setowner 'NT SERVICE\TrustedInstaller' /C | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not restore TrustedInstaller ownership (exit code $LASTEXITCODE)." }
        Set-Content -LiteralPath $installedHashFile -Value $launcherHash -NoNewline
        Write-Host "Installed the replacement at $target"
    } catch {
        if (Test-Path -LiteralPath $replacement) { Remove-Item -LiteralPath $replacement -Force }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $launcherHash) {
            Copy-Item -LiteralPath $backupExe -Destination $replacement
            [IO.File]::Replace($replacement, $target, $null)
            try { Set-Acl -LiteralPath $target -AclObject $originalAcl } catch { }
            & icacls.exe $target /setowner 'NT SERVICE\TrustedInstaller' /C | Out-Null
        }
        throw
    }
}

$ifeoPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\msoobe.exe'
if (Test-Path -LiteralPath $ifeoPath) {
    $ifeo = Get-ItemProperty -LiteralPath $ifeoPath -Name Debugger -ErrorAction SilentlyContinue
    if ($ifeo.Debugger -and $ifeo.Debugger.Trim('"') -ieq $launcher) {
        Remove-ItemProperty -LiteralPath $ifeoPath -Name Debugger
        Write-Host 'Removed the old IFEO debugger value for this launcher.'
    }
}

Write-Host 'System binary replacement is ready. Take a VM snapshot before running Sysprep /oobe.'
