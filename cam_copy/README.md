# Camera Copy PowerShell Project

Automatically detect camera insertion and copy files to network shares based on file type.

## Overview

This PowerShell project provides an automated solution for photographers who need to quickly and reliably copy files from their camera's memory cards to different network locations:

- **RAF files** (RAW images) from Slot1 → RAW network share
- **JPG files** (JPEG images) from Slot2 → JPG network share

## Features

- **Manual Camera Processing**: Detect and process connected cameras when run
- **MTP Camera Support**: Direct access to cameras like Fujifilm X-T5 via MTP protocol
- **Smart File Organization**: Separates RAW and JPEG files to different destinations
- **Direct Network Copy**: Files copied directly from camera to network shares (no local temp storage)
- **Date-based Folders**: Automatically creates dated subfolders for organization
- **Duplicate Handling**: Skips identical files, renames conflicts
- **Copy Verification**: Verifies all files were successfully copied after completion
- **Safe Eject**: Automatically ejects camera after successful copy (for removable drives)
- **Notifications**: Windows toast notifications when copying completes
- **Logging**: Comprehensive logging to file and console
- **Multiple Modes**: GUI, command-line, test mode, and single-run options

## Project Structure

```
cam_copy/
├── CameraCopyMain.ps1      # Main orchestrator script - start here
├── CameraDetector.ps1      # Detects and processes cameras
├── FileCopier.ps1          # Handles file copying operations
├── Config.json             # Configuration file
├── README.md               # This documentation
└── .github/
    └── copilot-instructions.md
```

## Quick Start

### 1. Configuration Setup

Edit `Config.json` and update the network share paths:

```json
{
    "RAFNetworkShare": "\\\\your-nas\\photos\\RAW",
    "JPGNetworkShare": "\\\\your-nas\\photos\\JPG",
    "LogFile": ".\\CameraCopy.log",
    "AutoEject": true,
    "NotificationEnabled": true
}
```

### 2. Run the System

**Option A: GUI Mode (Recommended for beginners)**
```powershell
.\CameraCopyMain.ps1 -ShowGUI
```

**Option B: Command Line Mode**
```powershell
.\CameraCopyMain.ps1
```

**Option C: Test Mode (Verify setup without copying)**
```powershell
.\CameraCopyMain.ps1 -TestMode
```

### 3. Use Your Camera

1. Plug in your camera or memory card reader
2. The system automatically detects the camera
3. Files are copied to the configured network shares
4. Camera is safely ejected (if enabled)
5. You receive a notification when complete

## Detailed Configuration

### Network Shares

Update these paths in `Config.json`:

```json
{
    "RAFNetworkShare": "\\\\server\\share\\RAW",
    "JPGNetworkShare": "\\\\server\\share\\JPG"
}
```

**Examples:**
- Windows Share: `\\\\MyNAS\\Photos\\RAW`
- Mapped Drive: `Z:\\Photos\\RAW` (maps to network share)
- Local Path: `C:\\Photos\\RAW` (local storage - not recommended for MTP cameras)

### Advanced Settings

```json
{
    "Settings": {
        "CreateDateFolders": true,           // Create YYYY-MM-DD subfolders
        "DateFolderFormat": "yyyy-MM-dd",    // Date format for folders
        "SkipDuplicates": true,              // Skip files that already exist
        "RenameConflicts": true,             // Rename files with same name but different content
        "MaxRetries": 3,                     // Retry failed copies
        "RetryDelay": 2000                   // Delay between retries (ms)
    },
    "CameraSettings": {
        "Slot1FileTypes": ["*.RAF", "*.ARW", "*.CR2", "*.NEF"],  // RAW file extensions
        "Slot2FileTypes": ["*.JPG", "*.JPEG"],                   // JPEG file extensions
        "DCIMFolderPattern": "DCIM",                             // Camera folder name
        "MinimumFreeSpace": 1073741824                           // 1GB minimum free space
    }
}
```

## Usage Modes

### 1. Default Mode (Single Run)
```powershell
.\CameraCopyMain.ps1
```
Checks for cameras and processes any found, then exits.

### 2. GUI Mode
```powershell
.\CameraCopyMain.ps1 -ShowGUI
```
Shows a simple status window with a stop button.

### 3. Explicit Single Run Mode
```powershell
.\CameraCopyMain.ps1 -RunOnce
```
Same as default mode - processes any currently connected cameras then exits.

### 4. Test Mode
```powershell
.\CameraCopyMain.ps1 -TestMode
```
Shows what would be copied without actually copying files.

### 5. Custom Configuration
```powershell
.\CameraCopyMain.ps1 -ConfigPath "C:\\MyConfig.json"
```
Uses a different configuration file.

## Copy Behavior

### Direct Network Transfer
The system copies files **directly** from your camera to the network shares:
- **No local temp storage** used during transfer
- **MTP Cameras** (like X-T5): Uses Windows Shell.Application COM interface
- **Drive-based cameras**: Uses standard PowerShell Copy-Item
- **Network efficiency**: Single copy operation (camera → network share)

### File Organization

The system creates the following folder structure:

```
Network Share/
├── 2024-03-15/          # Today's files
│   ├── IMG001.RAF
│   ├── IMG002.RAF
│   └── ...
├── 2024-03-14/          # Yesterday's files
│   └── ...
└── 2024-03-13/          # Older files
    └── ...
```

## Troubleshooting

### Camera Not Detected

1. **Check USB Connection**: Ensure camera is properly connected
2. **Camera Mode**: Set camera to "PC" or "Mass Storage" mode
3. **Driver Issues**: Install camera manufacturer's drivers
4. **Test Detection**: Run with `-TestMode` to see detected drives

### Network Share Issues

1. **Path Format**: Use UNC format: `\\\\server\\share`
2. **Permissions**: Ensure write access to network shares
3. **Connectivity**: Test by accessing shares in File Explorer
4. **Credentials**: Configure username/domain in config if needed

### No Files Copying

1. **File Types**: Check if camera uses different RAW formats (ARW, CR2, NEF)
2. **Folder Structure**: Verify camera uses DCIM folder structure
3. **Permissions**: Check write permissions on destination folders
4. **Free Space**: Ensure adequate free space on destination

### Script Execution Errors

1. **Execution Policy**: Run PowerShell as Administrator and execute:
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```

2. **MTP Cameras**: If X-T5 or similar cameras aren't detected, check USB connection

3. **Antivirus**: Add script folder to antivirus exclusions

## Logging

All activities are logged to `CameraCopy.log` (configurable):

```
[2025-09-23 14:30:15] [INFO] Camera Detector starting up...
[2025-09-23 14:30:16] [INFO] Processing cameras once (no monitoring)...
[2025-09-23 14:32:45] [INFO] Found MTP device: X-T5
[2025-09-23 14:32:46] [SUCCESS] MTP copy operation completed. Total files copied: 129
[2025-09-23 14:32:47] [SUCCESS] All files verified successfully copied!
```

## Security Considerations

- **Network Credentials**: Store credentials securely, avoid plain text
- **Execution Policy**: Use RemoteSigned, not Unrestricted
- **File Permissions**: Grant minimum necessary network share permissions
- **Antivirus**: Ensure scripts are scanned and trusted

## Customization

### Adding New File Types

Edit `Config.json` to support additional RAW formats:

```json
"CameraSettings": {
    "Slot1FileTypes": ["*.RAF", "*.ARW", "*.CR2", "*.NEF", "*.DNG"],
    "Slot2FileTypes": ["*.JPG", "*.JPEG", "*.HEIC"]
}
```

### Custom Folder Naming

Change the date format for folders:

```json
"Settings": {
    "DateFolderFormat": "yyyy\\MM\\dd"  // Creates 2024\03\15 structure
}
```

### Multiple Camera Support

The system automatically handles multiple cameras connected simultaneously.

## Performance

- **Typical Speed**: 50-100 MB/s over Gigabit network
- **Memory Usage**: ~50MB RAM during operation
- **CPU Usage**: Minimal usage, higher during copying operations

## License

This project is provided as-is for educational and personal use.

## Support

For issues or questions:

1. Check the troubleshooting section above
2. Review log files for error details
3. Test in TestMode to isolate issues
4. Verify network connectivity and permissions

## Version History

- **v1.0**: Initial release with basic camera detection and file copying
- Features: MTP camera support, network shares, duplicate handling, copy verification, logging, notifications