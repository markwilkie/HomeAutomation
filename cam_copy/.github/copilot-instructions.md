# Camera Copy PowerShell Project

This project contains PowerShell scripts for automated camera file management:

## Project Structure
- `CameraDetector.ps1` - Monitors for camera/USB device insertion
- `FileCopier.ps1` - Handles copying JPG and RAF files to network shares
- `Config.json` - Configuration file for network paths and settings
- `CameraCopyMain.ps1` - Main orchestrator script
- `README.md` - Documentation and setup instructions

## Purpose
Automatically detect when a camera is plugged in and copy:
- JPG files from slot2 to network share
- RAF files from slot1 to different network share

## Key Features
- WMI event monitoring for USB device detection
- Automated file type filtering and organization
- Network share connectivity
- Error handling and logging