# CameraCopyMain.ps1
# Main orchestrator script for camera file copying system

param(
    [string]$ConfigPath = ".\Config.json",
    [switch]$RunOnce,
    [switch]$TestMode,
    [switch]$ShowGUI
)

# Set execution policy for current session if needed
try {
    Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force -ErrorAction SilentlyContinue
} catch {
    Write-Warning "Could not set execution policy. You may need to run as administrator."
}

# Import configuration
if (Test-Path $ConfigPath) {
    $Config = Get-Content $ConfigPath | ConvertFrom-Json
} else {
    Write-Host "Configuration file not found: $ConfigPath" -ForegroundColor Red
    Write-Host "Creating default configuration file..." -ForegroundColor Yellow
    
    # Create default config if it doesn't exist
    $defaultConfig = @{
        RAFNetworkShare = "\\your-server\photos\RAW"
        JPGNetworkShare = "\\your-server\photos\JPG"
        LogFile = ".\CameraCopy.log"
        AutoEject = $true
        NotificationEnabled = $true
        MonitoringInterval = 5
    } | ConvertTo-Json -Depth 3
    
    Set-Content -Path $ConfigPath -Value $defaultConfig
    Write-Host "Default configuration created at: $ConfigPath" -ForegroundColor Green
    Write-Host "Please edit the network share paths before running again." -ForegroundColor Yellow
    exit 0
}

# Function to log messages
function Write-Log {
    param(
        [string]$Message,
        [string]$Level = "INFO"
    )
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] [$Level] $Message"
    
    # Console output with colors
    switch ($Level) {
        "ERROR" { Write-Host $logMessage -ForegroundColor Red }
        "WARNING" { Write-Host $logMessage -ForegroundColor Yellow }
        "SUCCESS" { Write-Host $logMessage -ForegroundColor Green }
        default { Write-Host $logMessage -ForegroundColor White }
    }
    
    # Log to file if specified in config
    if ($Config.LogFile) {
        Add-Content -Path $Config.LogFile -Value $logMessage
    }
}

# Function to validate configuration
function Test-Configuration {
    $isValid = $true
    
    Write-Log "Validating configuration..."
    
    # Check network shares
    if (-not $Config.RAFNetworkShare -or $Config.RAFNetworkShare -eq "\\your-server\photos\RAW") {
        Write-Log "RAF network share not configured properly" -Level "ERROR"
        $isValid = $false
    }
    
    if (-not $Config.JPGNetworkShare -or $Config.JPGNetworkShare -eq "\\your-server\photos\JPG") {
        Write-Log "JPG network share not configured properly" -Level "ERROR"
        $isValid = $false
    }
    
    # Test network connectivity if shares are configured
    if ($Config.RAFNetworkShare -and $Config.RAFNetworkShare -ne "\\your-server\photos\RAW") {
        if (Test-Path $Config.RAFNetworkShare) {
            Write-Log "RAF network share accessible: $($Config.RAFNetworkShare)" -Level "SUCCESS"
        } else {
            Write-Log "RAF network share not accessible: $($Config.RAFNetworkShare)" -Level "WARNING"
        }
    }
    
    if ($Config.JPGNetworkShare -and $Config.JPGNetworkShare -ne "\\your-server\photos\JPG") {
        if (Test-Path $Config.JPGNetworkShare) {
            Write-Log "JPG network share accessible: $($Config.JPGNetworkShare)" -Level "SUCCESS"
        } else {
            Write-Log "JPG network share not accessible: $($Config.JPGNetworkShare)" -Level "WARNING"
        }
    }
    
    return $isValid
}

# Function to show system status
function Show-SystemStatus {
    Write-Host "`n=== Camera Copy System Status ===" -ForegroundColor Cyan
    Write-Host "Configuration: $ConfigPath"
    Write-Host "RAF Share: $($Config.RAFNetworkShare)"
    Write-Host "JPG Share: $($Config.JPGNetworkShare)"
    Write-Host "Auto Eject: $($Config.AutoEject)"
    Write-Host "Notifications: $($Config.NotificationEnabled)"
    Write-Host "Log File: $($Config.LogFile)"
    
    # Check for existing camera drives (both regular drives and MTP devices)
    $cameraDrives = & { 
        $foundCameras = @()
        
        # Check for MTP devices (like cameras that don't appear as drive letters)
        try {
            $shell = New-Object -ComObject Shell.Application
            $myComputer = $shell.NameSpace(17)
            foreach ($item in $myComputer.Items()) {
                $name = $item.Name
                # Look for camera-like names (exclude known system drives)
                if ($name -notmatch "Local Disk|Network|Removable Disk|\(C:\)|\(P:\)" -and $name -ne "System Reserved") {
                    # For X-T5, try to count actual files
                    $rafCount = "Unknown"
                    $jpgCount = "Unknown"
                    
                    if ($name -eq "X-T5") {
                        try {
                            $cameraFolder = $item.GetFolder
                            $extMem = $cameraFolder.Items() | Where-Object { $_.Name -eq "External Memory" }
                            if ($extMem) {
                                $rafCount = "Available"
                                $jpgCount = "Available"
                            }
                        }
                        catch {
                            # Ignore errors when checking MTP content
                        }
                    }
                    
                    $foundCameras += @{
                        Drive = $name
                        Label = $name
                        RAFCount = $rafCount
                        JPGCount = $jpgCount
                        Type = "MTP"
                    }
                }
            }
        }
        catch {
            # Ignore COM errors
        }
        
        return $foundCameras
    }
    
    if ($cameraDrives) {
        Write-Host "`nDetected Camera Drives:" -ForegroundColor Green
        foreach ($camera in $cameraDrives) {
            if ($camera.Type -eq "MTP") {
                Write-Host "  $($camera.Drive) [MTP Device] - Ready for automatic processing" -ForegroundColor Cyan
            } else {
                Write-Host "  $($camera.Drive) [$($camera.Label)] - RAF: $($camera.RAFCount), JPG: $($camera.JPGCount)"
            }
        }
    } else {
        Write-Host "`nNo cameras detected" -ForegroundColor Yellow
        Write-Host "Note: MTP cameras (like X-T5) may require manual file copying" -ForegroundColor Yellow
    }
    
    Write-Host "================================`n" -ForegroundColor Cyan
}

# Function to run in test mode
function Invoke-TestMode {
    Write-Log "Running in test mode..." -Level "WARNING"
    Show-SystemStatus
    
    Write-Host "Test mode focuses on MTP cameras like X-T5" -ForegroundColor Yellow
    Write-Host "Regular removable drive detection has been simplified" -ForegroundColor Yellow
}

# Function to create simple GUI status window
function Show-GUI {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    
    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Camera Copy Monitor"
    $form.Size = New-Object System.Drawing.Size(500, 400)
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    
    # Status text area
    $statusTextBox = New-Object System.Windows.Forms.TextBox
    $statusTextBox.Location = New-Object System.Drawing.Point(10, 10)
    $statusTextBox.Size = New-Object System.Drawing.Size(460, 280)
    $statusTextBox.Multiline = $true
    $statusTextBox.ScrollBars = "Vertical"
    $statusTextBox.ReadOnly = $true
    $statusTextBox.Font = New-Object System.Drawing.Font("Consolas", 9)
    $form.Controls.Add($statusTextBox)
    
    # Check for cameras now button
    $checkButton = New-Object System.Windows.Forms.Button
    $checkButton.Location = New-Object System.Drawing.Point(10, 300)
    $checkButton.Size = New-Object System.Drawing.Size(150, 30)
    $checkButton.Text = "Check for Cameras"
    $form.Controls.Add($checkButton)
    
    # Process cameras button
    $processButton = New-Object System.Windows.Forms.Button
    $processButton.Location = New-Object System.Drawing.Point(170, 300)
    $processButton.Size = New-Object System.Drawing.Size(150, 30)
    $processButton.Text = "Process Cameras"
    $form.Controls.Add($processButton)
    
    # Stop button
    $stopButton = New-Object System.Windows.Forms.Button
    $stopButton.Location = New-Object System.Drawing.Point(330, 300)
    $stopButton.Size = New-Object System.Drawing.Size(100, 30)
    $stopButton.Text = "Stop"
    $stopButton.Add_Click({ $form.Close() })
    $form.Controls.Add($stopButton)
    
    # Clear log button
    $clearButton = New-Object System.Windows.Forms.Button
    $clearButton.Location = New-Object System.Drawing.Point(10, 340)
    $clearButton.Size = New-Object System.Drawing.Size(100, 30)
    $clearButton.Text = "Clear Log"
    $clearButton.Add_Click({ $statusTextBox.Clear() })
    $form.Controls.Add($clearButton)
    
    # Function to update status
    function Update-Status {
        param([string]$Message)
        $timestamp = Get-Date -Format "HH:mm:ss"
        $statusTextBox.AppendText("[$timestamp] $Message`r`n")
        $statusTextBox.SelectionStart = $statusTextBox.Text.Length
        $statusTextBox.ScrollToCaret()
        $form.Refresh()
    }
    
    # Function to get camera status
    function Get-CameraStatus {
        $cameras = @()
        
        # Check for MTP devices
        try {
            $shell = New-Object -ComObject Shell.Application
            $myComputer = $shell.NameSpace(17)
            foreach ($item in $myComputer.Items()) {
                $name = $item.Name
                if ($name -notmatch "Local Disk|Network|Removable Disk|\(C:\)|\(P:\)" -and $name -ne "System Reserved") {
                    $cameras += "MTP: $name"
                }
            }
        }
        catch {
            # Ignore COM errors
        }
        
        return $cameras
    }
    
    # Check for cameras button click
    $checkButton.Add_Click({
        Update-Status "Checking for connected cameras..."
        $cameras = Get-CameraStatus
        
        if ($cameras.Count -gt 0) {
            Update-Status "Found $($cameras.Count) camera(s):"
            foreach ($camera in $cameras) {
                Update-Status "  - $camera"
            }
            $processButton.Enabled = $true
        } else {
            Update-Status "No cameras detected"
            $processButton.Enabled = $false
        }
    })
    
    # Process cameras button click
    $processButton.Add_Click({
        Update-Status "Processing cameras..."
        $processButton.Enabled = $false
        $checkButton.Enabled = $false
        
        # Get current cameras
        $cameras = Get-CameraStatus
        $processedCount = 0
        
        foreach ($camera in $cameras) {
            if ($camera.StartsWith("MTP:")) {
                $cameraName = $camera.Substring(5).Trim()
                Update-Status "Processing MTP camera: $cameraName"
                
                try {
                    # Run MTP copier directly
                    $mtpResult = & ".\MTPCopier.ps1" -CameraName $cameraName -ConfigPath $ConfigPath 2>&1
                    
                    # Parse the output for useful information
                    $mtpOutput = $mtpResult | Out-String
                    $lines = $mtpOutput -split "`n" | Where-Object { $_ -match "copied|skipped|completed" }
                    
                    foreach ($line in $lines) {
                        if ($line.Trim() -ne "") {
                            Update-Status $line.Trim()
                        }
                    }
                    
                    $processedCount++
                    Update-Status "Successfully processed $cameraName"
                }
                catch {
                    Update-Status "Error processing $cameraName : $($_.Exception.Message)"
                }
            }
            elseif ($camera.StartsWith("Drive:")) {
                $driveLetter = ($camera -split " ")[1]
                Update-Status "Processing drive camera: $driveLetter"
                
                try {
                    # Run standard file copier
                    $driveResult = & ".\FileCopier.ps1" -CameraDrive $driveLetter -ConfigPath $ConfigPath 2>&1
                    
                    # Parse the output for useful information
                    $driveOutput = $driveResult | Out-String
                    $lines = $driveOutput -split "`n" | Where-Object { $_ -match "copied|skipped|completed" }
                    
                    foreach ($line in $lines) {
                        if ($line.Trim() -ne "") {
                            Update-Status $line.Trim()
                        }
                    }
                    
                    $processedCount++
                    Update-Status "Successfully processed $driveLetter"
                }
                catch {
                    Update-Status "Error processing $driveLetter : $($_.Exception.Message)"
                }
            }
        }
        
        if ($processedCount -gt 0) {
            Update-Status "All cameras processed successfully! ($processedCount cameras)"
        } else {
            Update-Status "No cameras were processed"
        }
        
        $processButton.Enabled = $true
        $checkButton.Enabled = $true
    })
    
    # Initial status
    Update-Status "Camera Copy Monitor Started"
    Update-Status "Configuration: $ConfigPath"
    Update-Status "RAF Share: $($Config.RAFNetworkShare)"
    Update-Status "JPG Share: $($Config.JPGNetworkShare)"
    Update-Status ""
    Update-Status "Click 'Check for Cameras' to scan for connected devices"
    Update-Status "Click 'Process Cameras' to copy files from detected cameras"
    
    # Auto-check for cameras on startup
    $form.Add_Shown({
        $form.Activate()
        Start-Sleep -Milliseconds 500
        $checkButton.PerformClick()
    })
    
    $form.ShowDialog()
}

# Main execution
try {
    Write-Log "Camera Copy Main starting up..." -Level "SUCCESS"
    Write-Log "Script location: $PSScriptRoot"
    Write-Log "Configuration: $ConfigPath"
    
    # Change to script directory
    Set-Location $PSScriptRoot
    
    # Show system status
    Show-SystemStatus
    
    # Validate configuration
    if (-not (Test-Configuration)) {
        Write-Log "Configuration validation failed. Please check your settings." -Level "ERROR"
        Read-Host "Press Enter to exit"
        exit 1
    }
    
    # Handle different execution modes
    if ($TestMode) {
        Invoke-TestMode
        Read-Host "Press Enter to exit test mode"
        exit 0
    }
    
    if ($RunOnce) {
        Write-Log "Running in single-execution mode..."
        & ".\CameraDetector.ps1" -ConfigPath $ConfigPath
        exit 0
    }
    
    if ($ShowGUI) {
        Write-Log "Starting GUI mode..."
        # Show GUI (no background job needed - GUI handles everything)
        Show-GUI
        exit 0
    }
    
    # Default: run camera detector once
    Write-Log "Running camera detection and processing once..."
    
    & ".\CameraDetector.ps1" -ConfigPath $ConfigPath
}
catch {
    Write-Log "Critical error in main script: $($_.Exception.Message)" -Level "ERROR"
    Read-Host "Press Enter to exit"
    exit 1
}