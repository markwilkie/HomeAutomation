# CameraDetector.ps1
# Monitors for camera/USB device insertion and triggers file copying

param(
    [string]$ConfigPath = ".\Config.json"
)

# Import configuration
if (Test-Path $ConfigPath) {
    $Config = Get-Content $ConfigPath | ConvertFrom-Json
} else {
    Write-Host "Configuration file not found: $ConfigPath" -ForegroundColor Red
    exit 1
}

# Function to log messages
function Write-Log {
    param(
        [string]$Message,
        [string]$Level = "INFO"
    )
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] [$Level] $Message"
    Write-Host $logMessage
    
    # Log to file if specified in config
    if ($Config.LogFile) {
        Add-Content -Path $Config.LogFile -Value $logMessage
    }
}

# Function to detect camera drives
function Get-CameraDrives {
    $cameraDrives = @()
    
    # Check for MTP devices like X-T5 (prioritize this since it's more reliable)
    try {
        $shell = New-Object -ComObject Shell.Application
        $myComputer = $shell.NameSpace(17)
        foreach ($item in $myComputer.Items()) {
            $name = $item.Name
            # Look for camera-like names (exclude known system drives)
            if ($name -notmatch "Local Disk|Network|Removable Disk|\(C:\)|\(P:\)" -and $name -ne "System Reserved") {
                Write-Log "Found MTP device: $name"
                $cameraDrives += @{
                    DriveLetter = $name
                    Label = $name
                    Slot1Path = "MTP"
                    Slot2Path = "MTP"
                    RAFCount = "MTP"
                    JPGCount = "MTP" 
                    Type = "MTP"
                }
            }
        }
    }
    catch {
        # Ignore COM errors
    }
    
    return $cameraDrives
}

# Function to process cameras once
function Invoke-CameraProcessing {
    Write-Log "Processing cameras once (no monitoring)..."
    
    $currentCameras = Get-CameraDrives
    
    if ($currentCameras.Count -eq 0) {
        Write-Log "No cameras detected"
        return
    }
    
    # Process all detected cameras
    foreach ($camera in $currentCameras) {
        Write-Log "Processing camera: $($camera.DriveLetter) - Type: $($camera.Type)"
        
        # Trigger appropriate file copying based on camera type
        if ($camera.Type -eq "MTP") {
            Write-Log "Using MTP copier for $($camera.DriveLetter)"
            & ".\MTPCopier.ps1" -CameraName $camera.DriveLetter -ConfigPath $ConfigPath
        } else {
            Write-Log "Using standard copier for $($camera.DriveLetter)"
            & ".\FileCopier.ps1" -CameraDrive $camera.DriveLetter -ConfigPath $ConfigPath
        }
    }
    
    Write-Log "Camera processing completed"
}

# Main execution
try {
    Write-Log "Camera Detector starting up..."
    Write-Log "Configuration loaded from: $ConfigPath"
    
    # Process cameras once and exit
    Invoke-CameraProcessing
}
catch {
    Write-Log "Error in camera detector: $($_.Exception.Message)" -Level "ERROR"
    exit 1
}