# BVHView File Association Registration Script
# Run this script AS ADMINISTRATOR to register .bvh files to open with bvhview.exe
# Usage: Right-click -> "Run with PowerShell" (as Administrator), or:
#   powershell -ExecutionPolicy Bypass -File register-bvh.ps1

$ErrorActionPreference = "Stop"

# === CONFIGURATION ===
# Change this to your actual bvhview.exe path
$BvhViewPath = "$PSScriptRoot\bvhview.exe"
$AppName = "BVHView"
$ProgID = "BVHView.bvh"
$ProtocolName = "bvhview"
$Extension = ".bvh"
$Description = "BVH Motion Capture Data"
$MimeType = "application/bvh"
$FriendlyTypeName = "BVH Motion Capture File"
# =====================

# Check if script is running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Please right-click this file and select 'Run with PowerShell' as Administrator, or run:" -ForegroundColor Yellow
    Write-Host "  Start-Process powershell -Verb RunAs -ArgumentList '-File', '$PSCommandPath'" -ForegroundColor Yellow
    exit 1
}

# Verify bvhview.exe exists
if (-not (Test-Path $BvhViewPath)) {
    Write-Host "WARNING: bvhview.exe not found at: $BvhViewPath" -ForegroundColor Yellow
    Write-Host "You can still register the association, but files won't open until the exe is in place." -ForegroundColor Yellow
}

Write-Host "=== Registering .bvh file association ===" -ForegroundColor Cyan
Write-Host "  App:     $AppName" -ForegroundColor White
Write-Host "  ProgID:  $ProgID" -ForegroundColor White
Write-Host "  Exe:     $BvhViewPath" -ForegroundColor White
Write-Host ""

# 1. Register ProgID (HKCR\BVHView.bvh)
Write-Host "[1/5] Creating ProgID: $ProgID" -ForegroundColor Green
$progIdPath = "Registry::HKEY_CLASSES_ROOT\$ProgID"
New-Item -Path $progIdPath -Force | Out-Null
Set-ItemProperty -Path $progIdPath -Name "(Default)" -Value $FriendlyTypeName -Type String

# 2. Default icon
Write-Host "[2/5] Setting default icon" -ForegroundColor Green
$iconPath = "$progIdPath\DefaultIcon"
New-Item -Path $iconPath -Force | Out-Null
Set-ItemProperty -Path $iconPath -Name "(Default)" -Value "`"$BvhViewPath`",0" -Type String

# 3. Open command (with %1 for file path)
Write-Host "[3/5] Setting open command" -ForegroundColor Green
$cmdPath = "$progIdPath\shell\open\command"
New-Item -Path $cmdPath -Force | Out-Null
Set-ItemProperty -Path $cmdPath -Name "(Default)" -Value "`"$BvhViewPath`" `"%1`"" -Type String

# 4. Friendly type name (optional, for Explorer)
Write-Host "[4/5] Setting friendly type name" -ForegroundColor Green
# Already done in step 1 as the default value of ProgID

# 5. Register the .bvh extension to use the ProgID
Write-Host "[5/5] Associating $Extension with $ProgID" -ForegroundColor Green
$extPath = "Registry::HKEY_CLASSES_ROOT\$Extension"
New-Item -Path $extPath -Force | Out-Null
Set-ItemProperty -Path $extPath -Name "(Default)" -Value $ProgID -Type String

# Set Content Type (MIME type) for browser download handling
Set-ItemProperty -Path $extPath -Name "Content Type" -Value $MimeType -Type String -ErrorAction SilentlyContinue

# Set PerceivedType (helps Windows classify the file)
Set-ItemProperty -Path $extPath -Name "PerceivedType" -Value "document" -Type String -ErrorAction SilentlyContinue

# 6. Register MIME type in the system (HKCR\MIME\Database\Content Type)
Write-Host "[Bonus] Registering MIME type: $MimeType" -ForegroundColor Green
try {
    # Use the .NET registry API here because PowerShell treats '/' in
    # application/bvh as a path separator in Registry:: paths.
    $mimeKey = [Microsoft.Win32.Registry]::ClassesRoot.CreateSubKey("MIME\Database\Content Type\$MimeType")
    if ($null -eq $mimeKey) {
        throw "Could not create MIME registry key."
    }
    $mimeKey.SetValue("Extension", $Extension, [Microsoft.Win32.RegistryValueKind]::String)
    $mimeKey.Close()
}
catch {
    Write-Host "WARNING: Could not register MIME type $MimeType. File association was still registered." -ForegroundColor Yellow
    Write-Host "         $($_.Exception.Message)" -ForegroundColor Yellow
}

# 7. Register a custom URL protocol for browser links like:
#    bvhview://open?url=https%3A%2F%2Fexample.com%2Fmotion.bvh
Write-Host "[Bonus] Registering URL protocol: ${ProtocolName}://" -ForegroundColor Green
$protocolPath = "Registry::HKEY_CLASSES_ROOT\$ProtocolName"
New-Item -Path $protocolPath -Force | Out-Null
Set-ItemProperty -Path $protocolPath -Name "(Default)" -Value "URL:BVHView Protocol" -Type String
Set-ItemProperty -Path $protocolPath -Name "URL Protocol" -Value "" -Type String

$protocolIconPath = "$protocolPath\DefaultIcon"
New-Item -Path $protocolIconPath -Force | Out-Null
Set-ItemProperty -Path $protocolIconPath -Name "(Default)" -Value "`"$BvhViewPath`",0" -Type String

$protocolCmdPath = "$protocolPath\shell\open\command"
New-Item -Path $protocolCmdPath -Force | Out-Null
Set-ItemProperty -Path $protocolCmdPath -Name "(Default)" -Value "`"$BvhViewPath`" `"%1`"" -Type String

Write-Host ""
Write-Host "=== Registration Complete! ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Local .bvh files now open with BVHView." -ForegroundColor White
Write-Host "Browser links can use this custom protocol:" -ForegroundColor White
Write-Host "  bvhview://open?url=https%3A%2F%2Fexample.com%2Fmotion.bvh" -ForegroundColor White
Write-Host ""
Write-Host "To UNREGISTER, run: unregister-bvh.ps1" -ForegroundColor Yellow
Write-Host ""
Write-Host "TIP: Build protocol links with encodeURIComponent(url) in JavaScript." -ForegroundColor Gray

# Refresh shell icons
Write-Host "Refreshing shell icon cache..." -ForegroundColor Green
cmd /c "ie4uinit.exe -show 2>nul"
Write-Host "Done!" -ForegroundColor Green
