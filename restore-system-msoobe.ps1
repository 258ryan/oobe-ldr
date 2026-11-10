$ErrorActionPreference = 'Stop'

$root = 'C:\vista-oobe'
$systemOobe = Join-Path $env:windir 'System32\oobe'
$backup = Join-Path $root 'original-oobe'
$aclManifest = Join-Path $root 'original-oobe-acl.xml'

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
    throw 'Run this restore script from an elevated PowerShell prompt.'
}
if (-not [Environment]::Is64BitProcess) {
    throw 'Run this script from 64-bit PowerShell so System32 resolves to the native system directory.'
}
foreach ($processName in @('msoobe', 'setup', 'setuphost', 'oobe', 'oobeldr')) {
    if (Get-Process -Name $processName -ErrorAction SilentlyContinue) {
        throw "$processName.exe is running. Shut down setup before restoring the OOBE folder."
    }
}
foreach ($file in @('msoobe.exe', 'setup.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $backup $file) -PathType Leaf)) {
        throw "The preserved Windows OOBE file was not found: $(Join-Path $backup $file)"
    }
}

& takeown.exe /A /R /D Y /F $systemOobe | Out-Null
if ($LASTEXITCODE -ne 0) { throw "takeown could not grant temporary OOBE folder access (exit code $LASTEXITCODE)." }
& icacls.exe $systemOobe /grant '*S-1-5-32-544:F' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls could not grant temporary OOBE folder access (exit code $LASTEXITCODE)." }

& robocopy.exe $backup $systemOobe /MIR /COPY:DAT /XF msoobe.acl.sddl setup.exe.acl.sddl /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) {
    throw "Could not restore the original Windows OOBE folder (robocopy exit code $LASTEXITCODE)."
}

& icacls.exe $systemOobe /reset /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls could not reset the restored OOBE permissions (exit code $LASTEXITCODE)." }
& icacls.exe $systemOobe /setowner 'NT SERVICE\TrustedInstaller' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not set TrustedInstaller ownership on the restored OOBE folder (exit code $LASTEXITCODE)." }

if (Test-Path -LiteralPath $aclManifest -PathType Leaf) {
    Restore-OobeAclManifest -Path $systemOobe -Manifest $aclManifest
} else {
    Write-Warning 'The full ACL manifest is missing; restoring saved executable ACLs and inherited defaults.'
    foreach ($file in @('msoobe.exe', 'setup.exe')) {
        $savedAclPath = Join-Path $backup "$file.acl.sddl"
        $destination = Join-Path $systemOobe $file
        if (Test-Path -LiteralPath $savedAclPath -PathType Leaf) {
            $savedAcl = New-Object System.Security.AccessControl.FileSecurity
            $savedAcl.SetSecurityDescriptorSddlForm((Get-Content -LiteralPath $savedAclPath -Raw))
            Set-Acl -LiteralPath $destination -AclObject $savedAcl
        }
    }
}

Remove-Item -LiteralPath (Join-Path $root 'msoobe-launcher.sha256') -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $root 'setup-host.sha256') -ErrorAction SilentlyContinue
Write-Host "Restored the original Windows OOBE folder from $backup"
