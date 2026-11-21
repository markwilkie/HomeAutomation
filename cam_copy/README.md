# Camera Copy PowerShell Project

Automatically detect camera insertion and copy files to network shares based on file type.

## Overview

This PowerShell project provides an automated solution for photographers who need to quickly and reliably copy files from their camera's memory cards to different network locations:

- **RAF files** (RAW images) from Slot 1 → RAW network share (`P:\Xt5\raw`)
- **JPG files** (JPEG images) from Slot 2 → JPG network share (`P:\Xt5\jpg`)

The camera must be set to write RAW to Slot 1 and JPEG to Slot 2 (X-T5: *Save Data Set-up → Card Slot Setting (Still) → RAW / JPEG*). The slot → file type mapping is hardcoded in `MTPCopier.ps1`, not read from `Config.json`.

## Features

- **Manual Camera Processing**: Detect and process connected cameras when run
- **MTP Camera Support**: Direct access to cameras like Fujifilm X-T5 via MTP protocol
- **Smart File Organization**: Separates RAW and JPEG files to different destinations
- **Direct Network Copy**: Files copied directly from camera to network shares (no local temp storage)
- **Date-based Folders**: Creates a subfolder named for the day you copy
- **Duplicate Handling**: Skips files whose name already exists in today's folder
- **Copy Verification**: Compares file counts on the camera and the NAS after copying and logs a mismatch
- **Safe Eject**: Automatically ejects camera after successful copy (for removable drives)
- **Notifications**: Windows toast notifications when copying completes
- **Logging**: Comprehensive logging to file and console
- **Multiple Modes**: GUI, command-line, test mode, and single-run options

## Project Structure

```
cam_copy/
├── CameraCopyMain.ps1      # Main orchestrator script - start here
├── CameraDetector.ps1      # Finds the camera (MTP device or drive letter) and picks a copier
├── MTPCopier.ps1           # Copies from the X-T5 over USB (MTP) - the normal path
├── FileCopier.ps1          # Copies from a card reader (drive letter), then ejects
├── MTPHelper.ps1           # Manual fallback: sorts files you dragged into C:\Temp\Camera
├── Config.json             # Configuration file
├── CameraCopy.log          # Log of every run (gitignored)
├── README.md               # This documentation
└── .github/
    └── copilot-instructions.md
```

## Quick Start

### 1. Configuration Setup

Edit `Config.json` and update the network share paths:

```json
{
    "RAFNetworkShare": "P:\\Xt5\\raw",
    "JPGNetworkShare": "P:\\Xt5\\jpg",
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

1. Plug in the camera over USB (or put the card in a reader) and turn it on
2. Run `.\CameraCopyMain.ps1` from the `cam_copy` folder
3. Files are copied to the network shares, into a folder named for **today's date**
4. Card-reader copies are safely ejected (if enabled)
5. You receive a notification when complete; "(Check logs)" means some files were missed
6. **Run `.\CameraCopyMain.ps1` again, and keep rerunning until the log shows no `file count mismatch` errors.** A single run usually stops partway (e.g. 14 of 62 RAF, 3 of 62 JPG) without any error. Files already copied are skipped, so reruns are quick.
   - **Do all reruns on the same day.** The "already copied" check only looks in today's date folder, so a rerun tomorrow copies everything again into a new folder.

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

> **Note:** the scripts currently only read `RAFNetworkShare`, `JPGNetworkShare`, `LogFile`, `AutoEject` and `NotificationEnabled`. The `Settings`, `CameraSettings` and `NetworkSettings` blocks below are in `Config.json` but **not used by the code**: slots, file types (`*.RAF` / `*.JPG` only) and date folders are hardcoded.

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
        "Slot1FileTypes": ["*.RAF", "*.ARW", "*.CR2", "*.NEF"],  // Slot 1 = RAW
        "Slot2FileTypes": ["*.JPG", "*.JPEG"],                   // Slot 2 = JPEG
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

Folders are named for the date you **copy** the files, not the date the photos were taken.

**Camera over USB (MTPCopier, the normal path)**: `yyyy\MM-dd`

```
P:\Xt5\raw\
├── 2026\
│   ├── 11-21\           # Copied on 21 Nov 2026
│   │   ├── WJD01451.RAF
│   │   └── ...
│   └── 03-09\
└── 2025\
    └── ...
P:\Xt5\jpg\              # Same layout for JPGs
```

**Card reader (FileCopier)**: a single `yyyy-MM-dd` folder, e.g. `P:\Xt5\raw\2026-11-21\`.

## Troubleshooting

### Camera Not Detected

1. **Check USB Connection**: Ensure camera is properly connected
2. **Camera Mode**: Set camera to "PC" or "Mass Storage" mode
3. **Driver Issues**: Install camera manufacturer's drivers
4. **Test Detection**: Run with `-TestMode` to see detected drives

### Network Share Issues

**If the network share is gone** (e.g. `P:\Xt5` is missing or unreachable):

`P:` should map to **`\\192.168.15.10\photo`** (Synology NAS, share `photo`), with photos landing in `P:\Xt5\raw` and `P:\Xt5\jpg`.

1. Check the mapping: `net use` should list `P:  \\192.168.15.10\photo` with status `OK`.
2. If the NAS itself seems down, log in to the Synology admin page (DSM) at **http://192.168.15.10:5000/**:
   - **Control Panel → Shared Folder**: confirm the `photo` share exists (greyed out = volume problem, check **Storage Manager**).
   - **Control Panel → File Services → SMB**: make sure the SMB service is enabled.
   - **File Station**: confirm `photo/Xt5/raw` and `photo/Xt5/jpg` exist.
3. Remap the drive (it will prompt for your NAS login):
   ```powershell
   net use P: \\192.168.15.10\photo /persistent:yes
   ```

Other checks:

1. **Permissions**: your NAS user needs Read/Write on the `photo` share (DSM: Control Panel → Shared Folder → `photo` → Edit → Permissions)
2. **Connectivity**: open `P:\Xt5` in File Explorer

### Missing Files After a Copy

A single run usually stops partway through without any error. **Rerun `.\CameraCopyMain.ps1` on the same day until the end of the log has no `file count mismatch` errors.** Files already in today's folder are skipped, so reruns are quick.

To check how far it got, look at the end of `CameraCopy.log`:

```
[ERROR] RAF file count mismatch! Expected: 62, Found: 14   <- 48 RAF still to copy, run again
[ERROR] JPG file count mismatch! Expected: 62, Found: 3
```

When every file is copied, the end of the log shows `All files verified successfully copied!`. Keep the camera on and connected until then; if it goes to sleep (auto power off), the copy stops.

**Don't format the cards** until the verified message appears. **After** it appears, format both cards in the camera. The check compares *everything* on the card against *today's* folder only. If older photos are left on the cards, the counts will never match on a later day.

### No Files Copying

1. **Card slots**: RAW must be on Slot 1 and JPEG on Slot 2. If they're swapped on the camera, nothing matches and 0 files copy.
2. **Folder Structure**: Verify camera uses DCIM folder structure (X-T5: `SLOT 1\DCIM\118_FUJI\...`)
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

All activities are logged to `CameraCopy.log` in the `cam_copy` folder. Each run adds to the end of it:

```
[2026-11-21 08:45:27] [INFO] Found MTP device: X-T5
[2026-11-21 08:45:27] [INFO] Processing RAF files from SLOT 1...
[2026-11-21 08:45:27] [INFO] Created folder: P:\Xt5\raw\2026\11-21
[2026-11-21 08:48:56] [INFO] RAF copy completed: 14 copied, 0 skipped
[2026-11-21 08:48:56] [INFO] Processing JPG files from SLOT 2...
[2026-11-21 08:49:25] [INFO] JPG copy completed: 3 copied, 0 skipped
[2026-11-21 08:49:26] [INFO] RAF files - Source: 62, Destination: 14
[2026-11-21 08:49:26] [ERROR] RAF file count mismatch! Expected: 62, Found: 14
```

That run stopped early. See [Missing Files After a Copy](#missing-files-after-a-copy).

## Security Considerations

- **Network Credentials**: Store credentials securely, avoid plain text
- **Execution Policy**: Use RemoteSigned, not Unrestricted
- **File Permissions**: Grant minimum necessary network share permissions
- **Antivirus**: Ensure scripts are scanned and trusted

## Customization

These are code changes; editing `Config.json` alone has no effect.

- **File types**: the `"*.RAF"` / `"*.JPG"` filters in the `Copy-MTPFiles` / `Count-MTPFiles` calls in `MTPCopier.ps1` (and `Copy-FilesWithProgress` in `FileCopier.ps1`)
- **Slots**: the `"SLOT 1"` / `"SLOT 2"` names passed to `Get-SlotFolder` in `MTPCopier.ps1`
- **Date folders**: `New-DatedFolder` in `MTPCopier.ps1` (`yyyy` + `MM-dd`) and `FileCopier.ps1` (`yyyy-MM-dd`)
- **Camera name**: `MTPCopier.ps1` looks for a device named exactly `X-T5` under This PC

## Performance

Over USB, the X-T5 copies at roughly **one RAF every 12-17 seconds** and one JPG every 6-15 seconds. A 60-shot day takes around 20-30 minutes in total once all the reruns are done.

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