# MTPCopier.ps1
# Direct MTP device file copier for cameras like X-T5

param(
    [string]$CameraName = "X-T5",
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
    
    switch ($Level) {
        "ERROR" { Write-Host $logMessage -ForegroundColor Red }
        "WARNING" { Write-Host $logMessage -ForegroundColor Yellow }
        "SUCCESS" { Write-Host $logMessage -ForegroundColor Green }
        default { Write-Host $logMessage -ForegroundColor White }
    }
    
    if ($Config.LogFile) {
        Add-Content -Path $Config.LogFile -Value $logMessage
    }
}

# Function to create dated folder structure
function New-DatedFolder {
    param(
        [string]$BasePath,
        [string]$YearFormat = "yyyy",
        [string]$MonthFormat = "MM-dd"
    )

    $yearFolder = Get-Date -Format $YearFormat
    $monthFolder = Get-Date -Format $MonthFormat
    $datePath = Join-Path $yearFolder $monthFolder
    $fullPath = Join-Path $BasePath $datePath
    
    if (-not (Test-Path $fullPath)) {
        New-Item -ItemType Directory -Path $fullPath -Force | Out-Null
        Write-Log "Created folder: $fullPath"
    }
    
    return $fullPath
}

# Function to copy files from MTP device using Shell.Application
function Copy-MTPFiles {
    param(
        [object]$SourceFolder,
        [string]$DestinationPath,
        [string]$FileFilter,
        [string]$FileType
    )
    
    $copiedFiles = 0
    $skippedFiles = 0
    
    try {
        $shell = New-Object -ComObject Shell.Application
        $destFolder = $shell.NameSpace($DestinationPath)
        
        # Get all items in the source folder
        $items = $SourceFolder.Items()
        
        foreach ($item in $items) {
            $fileName = $item.Name
            
            # Check if file matches our filter
            if ($fileName -like $FileFilter) {
                $destFile = Join-Path $DestinationPath $fileName
                
                # Check if file already exists
                if (Test-Path $destFile) {
                    Write-Log "Skipping existing file: $fileName"
                    $skippedFiles++
                    continue
                }
                
                Write-Log "Copying $FileType file: $fileName"
                
                # Copy using Shell.Application (handles MTP properly)
                $destFolder.CopyHere($item, 4 + 16) # 4 = No progress dialog, 16 = Yes to all
                
                # Wait for copy to complete
                $timeout = 0
                while (-not (Test-Path $destFile) -and $timeout -lt 30) {
                    Start-Sleep -Milliseconds 500
                    $timeout++
                }
                
                if (Test-Path $destFile) {
                    $copiedFiles++
                    Write-Log "Successfully copied: $fileName" -Level "SUCCESS"
                } else {
                    Write-Log "Failed to copy: $fileName" -Level "ERROR"
                }
            }
            # If it's a folder, recurse into it
            elseif ($item.IsFolder) {
                Write-Log "Checking subfolder: $fileName"
                $subFolder = $item.GetFolder
                $subCopied = Copy-MTPFiles -SourceFolder $subFolder -DestinationPath $DestinationPath -FileFilter $FileFilter -FileType $FileType
                $copiedFiles += $subCopied
            }
        }
    }
    catch {
        Write-Log "Error during MTP copy: $($_.Exception.Message)" -Level "ERROR"
    }
    
    Write-Log "$FileType copy completed: $copiedFiles copied, $skippedFiles skipped"
    return $copiedFiles
}

# Function to find and access camera MTP device
function Get-CameraMTPDevice {
    param([string]$CameraName)
    
    try {
        $shell = New-Object -ComObject Shell.Application
        $myComputer = $shell.NameSpace(17)
        
        $camera = $myComputer.Items() | Where-Object { $_.Name -eq $CameraName }
        if (-not $camera) {
            Write-Log "Camera '$CameraName' not found in This PC" -Level "ERROR"
            return $null
        }
        
        Write-Log "Found camera: $CameraName" -Level "SUCCESS"
        return $camera.GetFolder
    }
    catch {
        Write-Log "Error accessing MTP device: $($_.Exception.Message)" -Level "ERROR"
        return $null
    }
}

# Function to get slot folder
function Get-SlotFolder {
    param(
        [object]$CameraFolder,
        [string]$SlotName
    )
    
    try {
        # Navigate to External Memory
        $extMem = $CameraFolder.Items() | Where-Object { $_.Name -eq "External Memory" }
        if (-not $extMem) {
            Write-Log "External Memory not found" -Level "ERROR"
            return $null
        }
        
        $extMemFolder = $extMem.GetFolder
        
        # Get the specified slot
        $slot = $extMemFolder.Items() | Where-Object { $_.Name -eq $SlotName }
        if (-not $slot) {
            Write-Log "$SlotName not found" -Level "ERROR"
            return $null
        }
        
        $slotFolder = $slot.GetFolder
        
        # Navigate to DCIM folder if it exists
        $dcim = $slotFolder.Items() | Where-Object { $_.Name -eq "DCIM" }
        if ($dcim) {
            Write-Log "Found DCIM folder in $SlotName" -Level "SUCCESS"
            return $dcim.GetFolder
        } else {
            Write-Log "No DCIM folder in $SlotName, using slot root" -Level "WARNING"
            return $slotFolder
        }
    }
    catch {
        Write-Log "Error accessing $SlotName : $($_.Exception.Message)" -Level "ERROR"
        return $null
    }
}

# Function to count files in MTP folder
function Count-MTPFiles {
    param(
        [object]$SourceFolder,
        [string]$FileFilter
    )
    
    $fileCount = 0
    
    try {
        $items = $SourceFolder.Items()
        
        foreach ($item in $items) {
            $fileName = $item.Name
            
            # Check if file matches our filter
            if ($fileName -like $FileFilter) {
                $fileCount++
            }
            # If it's a folder, recurse into it
            elseif ($item.IsFolder) {
                $subFolder = $item.GetFolder
                $subCount = Count-MTPFiles -SourceFolder $subFolder -FileFilter $FileFilter
                $fileCount += $subCount
            }
        }
    }
    catch {
        Write-Log "Error counting files: $($_.Exception.Message)" -Level "ERROR"
    }
    
    return $fileCount
}

# Function to verify all files were copied successfully
function Test-CopyCompletion {
    param(
        [object]$CameraFolder,
        [hashtable]$CopyResults
    )
    
    Write-Log "Verifying copy completion..." -Level "INFO"
    $verificationPassed = $true
    
    # Check RAF files from SLOT 1
    if ($Config.RAFNetworkShare -and $CopyResults.ContainsKey("RAF")) {
        $slot1Folder = Get-SlotFolder -CameraFolder $CameraFolder -SlotName "SLOT 1"
        if ($slot1Folder) {
            $sourceRAFCount = Count-MTPFiles -SourceFolder $slot1Folder -FileFilter "*.RAF"
            $destinationRAFCount = (Get-ChildItem -Path $CopyResults["RAF_Destination"] -Filter "*.RAF" -ErrorAction SilentlyContinue).Count
            
            Write-Log "RAF files - Source: $sourceRAFCount, Destination: $destinationRAFCount"
            
            if ($sourceRAFCount -ne $destinationRAFCount) {
                Write-Log "RAF file count mismatch! Expected: $sourceRAFCount, Found: $destinationRAFCount" -Level "ERROR"
                $verificationPassed = $false
            } else {
                Write-Log "RAF file verification passed" -Level "SUCCESS"
            }
        }
    }
    
    # Check JPG files from SLOT 2
    if ($Config.JPGNetworkShare -and $CopyResults.ContainsKey("JPG")) {
        $slot2Folder = Get-SlotFolder -CameraFolder $CameraFolder -SlotName "SLOT 2"
        if ($slot2Folder) {
            $sourceJPGCount = Count-MTPFiles -SourceFolder $slot2Folder -FileFilter "*.JPG"
            $destinationJPGCount = (Get-ChildItem -Path $CopyResults["JPG_Destination"] -Filter "*.JPG" -ErrorAction SilentlyContinue).Count
            
            Write-Log "JPG files - Source: $sourceJPGCount, Destination: $destinationJPGCount"
            
            if ($sourceJPGCount -ne $destinationJPGCount) {
                Write-Log "JPG file count mismatch! Expected: $sourceJPGCount, Found: $destinationJPGCount" -Level "ERROR"
                $verificationPassed = $false
            } else {
                Write-Log "JPG file verification passed" -Level "SUCCESS"
            }
        }
    }
    
    if ($verificationPassed) {
        Write-Log "All files verified successfully copied!" -Level "SUCCESS"
    } else {
        Write-Log "File verification failed - some files may be missing!" -Level "ERROR"
    }
    
    return $verificationPassed
}

# Main execution
try {
    Write-Log "MTP Copier starting for camera: $CameraName" -Level "SUCCESS"
    
    # Find camera MTP device
    $cameraFolder = Get-CameraMTPDevice -CameraName $CameraName
    if (-not $cameraFolder) {
        Write-Log "Cannot access camera MTP device" -Level "ERROR"
        exit 1
    }
    
    $totalCopied = 0
    $copyResults = @{}
    
    # Process RAF files from SLOT 1
    if ($Config.RAFNetworkShare) {
        Write-Log "Processing RAF files from SLOT 1..."
        
        $slot1Folder = Get-SlotFolder -CameraFolder $cameraFolder -SlotName "SLOT 1"
        if ($slot1Folder) {
            $rafDestination = New-DatedFolder -BasePath $Config.RAFNetworkShare
            $rafCopied = Copy-MTPFiles -SourceFolder $slot1Folder -DestinationPath $rafDestination -FileFilter "*.RAF" -FileType "RAF"
            $totalCopied += $rafCopied
            $copyResults["RAF"] = $rafCopied
            $copyResults["RAF_Destination"] = $rafDestination
        }
    }
    
    # Process JPG files from SLOT 2
    if ($Config.JPGNetworkShare) {
        Write-Log "Processing JPG files from SLOT 2..."
        
        $slot2Folder = Get-SlotFolder -CameraFolder $cameraFolder -SlotName "SLOT 2"
        if ($slot2Folder) {
            $jpgDestination = New-DatedFolder -BasePath $Config.JPGNetworkShare
            $jpgCopied = Copy-MTPFiles -SourceFolder $slot2Folder -DestinationPath $jpgDestination -FileFilter "*.JPG" -FileType "JPG"
            $totalCopied += $jpgCopied
            $copyResults["JPG"] = $jpgCopied
            $copyResults["JPG_Destination"] = $jpgDestination
        }
    }
    
    Write-Log "MTP copy operation completed. Total files copied: $totalCopied" -Level "SUCCESS"
    
    # Verify all files were copied successfully
    $verificationPassed = Test-CopyCompletion -CameraFolder $cameraFolder -CopyResults $copyResults
    
    # Send notification if configured
    if ($Config.NotificationEnabled -and $totalCopied -gt 0) {
        $verificationStatus = if ($verificationPassed) { "Verified" } else { "Check logs" }
        $notificationMessage = "X-T5 copy completed: $totalCopied files copied ($verificationStatus)"
        
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $notification = New-Object System.Windows.Forms.NotifyIcon
            $notification.Icon = [System.Drawing.SystemIcons]::Information
            $notification.BalloonTipTitle = "Camera Copy"
            $notification.BalloonTipText = $notificationMessage
            $notification.Visible = $true
            $notification.ShowBalloonTip(5000)
            
            Write-Log "Notification sent: $notificationMessage"
        }
        catch {
            Write-Log "Could not send notification: $($_.Exception.Message)" -Level "WARNING"
        }
    }
    
    # Exit with appropriate code based on verification
    if (-not $verificationPassed) {
        Write-Log "Exiting with error code due to verification failure" -Level "ERROR"
        exit 2
    }
}
catch {
    Write-Log "Critical error in MTP copier: $($_.Exception.Message)" -Level "ERROR"
    exit 1
}