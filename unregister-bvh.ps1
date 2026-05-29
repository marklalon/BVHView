# BVHView File Association Unregistration Script
# Run this script AS ADMINISTRATOR to remove .bvh file association

$ErrorActionPreference = "Stop"

$ProgID = "BVHView.bvh"
$ProtocolName = "bvhview"
$Extension = ".bvh"
$MimeType = "application/bvh"

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    exit 1
}

Write-Host "=== Unregistering .bvh file association ===" -ForegroundColor Cyan

# Remove extension association
$extPath = "Registry::HKEY_CLASSES_ROOT\$Extension"
if (Test-Path $extPath) {
    Write-Host "Removing $Extension association..." -ForegroundColor Yellow
    Remove-Item -Path $extPath -Recurse -Force
}

# Remove ProgID
$progIdPath = "Registry::HKEY_CLASSES_ROOT\$ProgID"
if (Test-Path $progIdPath) {
    Write-Host "Removing $ProgID..." -ForegroundColor Yellow
    Remove-Item -Path $progIdPath -Recurse -Force
}

# Remove MIME type
try {
    Write-Host "Removing MIME type $MimeType..." -ForegroundColor Yellow
    [Microsoft.Win32.Registry]::ClassesRoot.DeleteSubKeyTree("MIME\Database\Content Type\$MimeType", $false)
}
catch {
    Write-Host "WARNING: Could not remove MIME type $MimeType, or it was not registered." -ForegroundColor Yellow
}

# Remove custom URL protocol
$protocolPath = "Registry::HKEY_CLASSES_ROOT\$ProtocolName"
if (Test-Path $protocolPath) {
    Write-Host "Removing URL protocol ${ProtocolName}://..." -ForegroundColor Yellow
    Remove-Item -Path $protocolPath -Recurse -Force
}

# Refresh shell icons
cmd /c "ie4uinit.exe -show 2>nul"

Write-Host "Unregistration complete. .bvh files restored to default handling." -ForegroundColor Green
