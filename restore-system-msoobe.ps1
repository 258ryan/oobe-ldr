$ErrorActionPreference = 'Stop'

$root = 'C:\vista-oobe'
$systemOobe = Join-Path $env:windir 'System32\oobe'
$target = Join-Path $systemOobe 'msoobe.exe'
$backup = Join-Path $root 'original-oobe'
$backupExe = Join-Path $backup 'msoobe.exe'
$aclBackup = Join-Path $backup 'msoobe.acl.sddl'
$replacement = "$target.restore"

function Replace-SystemFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    $directory = [IO.Path]::GetDirectoryName($Destination)
    $temporaryBackup = Join-Path $directory ("msoobe.exe.{0}.bak" -f [Guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::Replace($Source, $Destination, $temporaryBackup)
    } finally {
        if (Test-Path -LiteralPath $temporaryBackup) {
            Remove-Item -LiteralPath $temporaryBackup -Force
        }
    }
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this restore script from an elevated PowerShell prompt.'
}
if (-not [Environment]::Is64BitProcess) {
    throw 'Run this script from 64-bit PowerShell so System32 resolves to the native system directory.'
}
if (Get-Process -Name msoobe -ErrorAction SilentlyContinue) {
    throw 'msoobe.exe is running. Shut down setup before restoring the system binary.'
}
if (-not (Test-Path -LiteralPath $backupExe -PathType Leaf)) {
    throw "The preserved Windows executable was not found: $backupExe"
}
if (Test-Path -LiteralPath $replacement) {
    throw "A staged restore already exists: $replacement. Inspect it before continuing."
}

$originalAcl = if (Test-Path -LiteralPath $aclBackup) {
    $savedAcl = New-Object System.Security.AccessControl.FileSecurity
    $savedAcl.SetSecurityDescriptorSddlForm((Get-Content -LiteralPath $aclBackup -Raw))
    $savedAcl
} else {
    Get-Acl -LiteralPath $target
}

& takeown.exe /A /F $target | Out-Null
if ($LASTEXITCODE -ne 0) { throw "takeown failed with exit code $LASTEXITCODE." }
& icacls.exe $target /grant '*S-1-5-32-544:F' /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls could not grant temporary access (exit code $LASTEXITCODE)." }

Copy-Item -LiteralPath $backupExe -Destination $replacement
Replace-SystemFile -Source $replacement -Destination $target
try {
    Set-Acl -LiteralPath $target -AclObject $originalAcl
} catch {
    & icacls.exe $target /reset /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls could not reset the restored file ACL (exit code $LASTEXITCODE)." }
}
& icacls.exe $target /setowner 'NT SERVICE\TrustedInstaller' /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not restore TrustedInstaller ownership (exit code $LASTEXITCODE)." }

Remove-Item -LiteralPath (Join-Path $root 'msoobe-launcher.sha256') -ErrorAction SilentlyContinue
Write-Host "Restored the original Windows OOBE binary to $target"
