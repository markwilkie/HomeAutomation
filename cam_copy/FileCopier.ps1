# FileCopier.ps1
# Handles copying JPG and RAF files from camera to network shares

param(
    [string]$CameraDrive,
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

# Function to ensure network share is accessible
function Test-NetworkShare {
    param([string]$SharePath)
    
    try {
        if (Test-Path $SharePath) {
            return $true
        } else {
            Write-Log "Network share not accessible: $SharePath" -Level "WARNING"
            return $false
        }
    }
    catch {
        Write-Log "Error accessing network share $SharePath : $($_.Exception.Message)" -Level "ERROR"
        return $false
    }
}

# Function to create dated folder structure
function New-DatedFolder {
    param(
        [string]$BasePath,
        [string]$DateFormat = "yyyy-MM-dd"
    )
    
    $dateFolder = Get-Date -Format $DateFormat
    $fullPath = Join-Path $BasePath $dateFolder
    
    if (-not (Test-Path $fullPath)) {
        New-Item -ItemType Directory -Path $fullPath -Force | Out-Null
        Write-Log "Created folder: $fullPath"
    }
    
    return $fullPath
}

# Function to copy files with progress
function Copy-FilesWithProgress {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [string]$FileFilter,
        [string]$FileType
    )
    
    $files = Get-ChildItem -Path $SourcePath -Filter $FileFilter -Recurse
    $totalFiles = $files.Count
    
    if ($totalFiles -eq 0) {
        Write-Log "No $FileType files found in $SourcePath"
        return 0
    }
    
    Write-Log "Found $totalFiles $FileType files to copy"
    
    $copiedFiles = 0
    $skippedFiles = 0
    
    foreach ($file in $files) {
        $destFile = Join-Path $DestinationPath $file.Name
        
        # Check if file already exists
        if (Test-Path $destFile) {
            $sourceHash = (Get-FileHash $file.FullName).Hash
            $destHash = (Get-FileHash $destFile).Hash
            
            if ($sourceHash -eq $destHash) {
                Write-Log "Skipping duplicate file: $($file.Name)"
                $skippedFiles++
                continue
            } else {
                # File exists but is different - rename with timestamp
                $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
                $extension = [System.IO.Path]::GetExtension($file.Name)
                $nameWithoutExt = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
                $destFile = Join-Path $DestinationPath "$nameWithoutExt`_$timestamp$extension"
            }
        }
        
        try {
            Copy-Item -Path $file.FullName -Destination $destFile -Force
            $copiedFiles++
            
            $progress = [math]::Round(($copiedFiles / $totalFiles) * 100, 1)
            Write-Log "Copied $($file.Name) ($copiedFiles/$totalFiles - $progress%)"
        }
        catch {
            Write-Log "Failed to copy $($file.Name): $($_.Exception.Message)" -Level "ERROR"
        }
    }
    
    Write-Log "$FileType copy completed: $copiedFiles copied, $skippedFiles skipped"
    return $copiedFiles
}

# Function to safely eject drive
function Invoke-SafeEject {
    param([string]$DriveLetter)
    
    try {
        $drive = Get-WmiObject -Class Win32_LogicalDisk | Where-Object { $_.DeviceID -eq $DriveLetter }
        if ($drive) {
            $result = (New-Object -comObject Shell.Application).Namespace(17).ParseName($DriveLetter).InvokeVerb("Eject")
            Write-Log "Attempted to safely eject drive $DriveLetter"
        }
    }
    catch {
        Write-Log "Could not eject drive $DriveLetter : $($_.Exception.Message)" -Level "WARNING"
    }
}

# Main file copying logic
try {
    Write-Log "Starting file copy operation for drive: $CameraDrive"
    
    # Validate camera drive
    if (-not (Test-Path $CameraDrive)) {
        Write-Log "Camera drive $CameraDrive not accessible" -Level "ERROR"
        exit 1
    }
    
    $totalCopied = 0
    
    # Copy RAF files from Slot1 to RAF network share
    if ($Config.RAFNetworkShare) {
        Write-Log "Processing RAF files from Slot1..."
        
        if (Test-NetworkShare $Config.RAFNetworkShare) {
            $rafDestination = New-DatedFolder -BasePath $Config.RAFNetworkShare
            $slot1Path = Join-Path $CameraDrive "DCIM"
            
            if (Test-Path $slot1Path) {
                $rafCopied = Copy-FilesWithProgress -SourcePath $slot1Path -DestinationPath $rafDestination -FileFilter "*.RAF" -FileType "RAF"
                $totalCopied += $rafCopied
            } else {
                Write-Log "Slot1 DCIM path not found: $slot1Path" -Level "WARNING"
            }
        }
    }
    
    # Copy JPG files from Slot2 to JPG network share
    if ($Config.JPGNetworkShare) {
        Write-Log "Processing JPG files from Slot2..."
        
        if (Test-NetworkShare $Config.JPGNetworkShare) {
            $jpgDestination = New-DatedFolder -BasePath $Config.JPGNetworkShare
            $slot2Path = Join-Path $CameraDrive "DCIM"
            
            if (Test-Path $slot2Path) {
                $jpgCopied = Copy-FilesWithProgress -SourcePath $slot2Path -DestinationPath $jpgDestination -FileFilter "*.JPG" -FileType "JPG"
                $totalCopied += $jpgCopied
            } else {
                Write-Log "Slot2 DCIM path not found: $slot2Path" -Level "WARNING"
            }
        }
    }
    
    Write-Log "File copy operation completed. Total files copied: $totalCopied"
    
    # Auto-eject if enabled and files were copied
    if ($Config.AutoEject -and $totalCopied -gt 0) {
        Write-Log "Auto-eject enabled, ejecting drive..."
        Invoke-SafeEject -DriveLetter $CameraDrive
    }
    
    # Send notification if configured
    if ($Config.NotificationEnabled -and $totalCopied -gt 0) {
        $notificationMessage = "Camera copy completed: $totalCopied files copied from $CameraDrive"
        
        # Windows toast notification
        Add-Type -AssemblyName System.Windows.Forms
        $notification = New-Object System.Windows.Forms.NotifyIcon
        $notification.Icon = [System.Drawing.SystemIcons]::Information
        $notification.BalloonTipTitle = "Camera Copy"
        $notification.BalloonTipText = $notificationMessage
        $notification.Visible = $true
        $notification.ShowBalloonTip(5000)
        
        Write-Log "Notification sent: $notificationMessage"
    }
}
catch {
    Write-Log "Error in file copier: $($_.Exception.Message)" -Level "ERROR"
    exit 1
}