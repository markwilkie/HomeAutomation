# MTPHelper.ps1
# Helper script for processing files from MTP devices like X-T5

param(
    [string]$SourceFolder = "C:\Temp\Camera",
    [string]$ConfigPath = ".\Config.json"
)

Write-Host "=== MTP Camera Helper ===" -ForegroundColor Cyan
Write-Host "This script processes files that you've manually copied from your MTP camera" -ForegroundColor Yellow
Write-Host ""
Write-Host "Instructions:" -ForegroundColor Green
Write-Host "1. Copy files from your X-T5 camera to: $SourceFolder" -ForegroundColor White
Write-Host "2. Run this script to organize them into RAF/JPG network shares" -ForegroundColor White
Write-Host ""

# Check if source folder exists
if (-not (Test-Path $SourceFolder)) {
    Write-Host "Creating source folder: $SourceFolder" -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $SourceFolder -Force | Out-Null
}

# Check if there are files to process
$rafFiles = Get-ChildItem -Path $SourceFolder -Filter "*.RAF" -Recurse -ErrorAction SilentlyContinue
$jpgFiles = Get-ChildItem -Path $SourceFolder -Filter "*.JPG" -Recurse -ErrorAction SilentlyContinue

if ($rafFiles.Count -eq 0 -and $jpgFiles.Count -eq 0) {
    Write-Host "No RAF or JPG files found in $SourceFolder" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please copy your camera files to this folder first, then run this script again." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 0
}

Write-Host "Found files to process:" -ForegroundColor Green
Write-Host "  RAF files: $($rafFiles.Count)" -ForegroundColor White
Write-Host "  JPG files: $($jpgFiles.Count)" -ForegroundColor White
Write-Host ""

$response = Read-Host "Process these files? (y/n)"
if ($response -notmatch "^[Yy]") {
    Write-Host "Operation cancelled" -ForegroundColor Yellow
    exit 0
}

# Use the FileCopier script to process the files
Write-Host "Processing files..." -ForegroundColor Green
& ".\FileCopier.ps1" -CameraDrive $SourceFolder -ConfigPath $ConfigPath

Write-Host ""
Write-Host "Processing complete!" -ForegroundColor Green
Write-Host "You can now delete the files from $SourceFolder if desired" -ForegroundColor Yellow

Read-Host "Press Enter to exit"