# Photo Workflow (X-T5 → NAS → Lightroom → OneDrive)

How photos get from the camera to safe storage, what runs by itself, and what to do when something's wrong.
Why it's built this way: [IMPROVEMENT_PLAN.md](https://github.com/markwilkie/HomeAutomation/blob/master/cam_copy/IMPROVEMENT_PLAN.md). Set up and tested 2026-11-21.

## The flow

```
X-T5 over USB, or SD card reader
   │
   │  Import Photos  (desktop shortcut on this PC)
   ▼
NAS  \\192.168.15.10\photo\Xt5
   ├─ raw\YYYY\MM-DD\       originals
   ├─ jpg\YYYY\MM-DD\       originals
   │     └─ new JPGs also → C:\Photos\Lightroom Inbox\  → Lightroom: Add Photos
   └─ Edited\YYYY\YYYY-MM Month\
         ▲  1:00 am  edit saver (wyse): full-size JPEG of every
         │           edited or Pick photo, from Lightroom's cloud
   │
   │  2:00 am  backup (wyse)
   ▼
OneDrive (work)  Backups/Photos/Xt5
                 Backups/Photos/_deleted/   (90 days)
```

## What you do

### After a shoot

1. Plug the X-T5 in over USB and switch it on (or put a card in a reader).
2. Double-click **Import Photos** on the desktop.
3. Wait for the last line: **✔ All N RAF + N JPG safe on NAS**. It also says where the new JPGs are for Lightroom.
   If it says **✘ INCOMPLETE**, run it again (files already copied are skipped). Don't format the cards.
4. In Lightroom: **Add Photos** → `C:\Photos\Lightroom Inbox\<date>` (local disk: seconds, not 20 minutes over Wi-Fi).

### Editing (whenever)

1. Cull and edit in Lightroom; reject the losers (X); press **P** (Pick) on keepers you didn't edit if you want them in `Edited`.
2. Optional: run the `lightroom` tool's cleanup to remove rejected photos (JPG + RAF) from the NAS.
3. Let Lightroom finish syncing (cloud icon, top right) before shutting down.
   That night, every edited or Pick photo appears in `P:\Xt5\Edited\YYYY\YYYY-MM Month\YYYY-MM-DD NAME.jpg`.

### Photoshop (when Lightroom isn't enough)

1. Do the basic edits in Lightroom first.
2. In Lightroom: right-click the photo → **Edit in Photoshop** (Ctrl+E). Photoshop opens a copy with the Lightroom edits applied.
3. Work in Photoshop, then **File → Save** (Ctrl+S) and close. Lightroom adds the result as a new photo next to the original (usually `NAME-Edit`), layers intact, synced to Adobe's cloud.
4. **Keep the layers on the NAS:** in Photoshop also **File → Save a Copy** (as TIFF or PSD; TIFF with ZIP compression keeps the size down) into `C:\Photos\Export Outbox\`. The next Import Photos files it into `P:\Xt5\Edited\_photoshop\`. (Adobe's API won't hand out original files, so this one step stays manual.)
5. That night the edit saver saves the flattened full-size JPEG of the Photoshop version to `Edited\YYYY\YYYY-MM Month\YYYY-MM-DD NAME-Edit.jpg` by itself (no Pick needed). If a Photoshop photo still has no layered copy in `_photoshop` after 2 nights, the daily brief names it.

For a file that never goes through Lightroom: save the finished JPEG (and the TIFF or PSD if you want the layers) into `C:\Photos\Export Outbox\`; Import Photos files them.

### Before the next shoot

1. Format **both** cards **in the camera** (MENU → User Setting → Format), once the import said ✔ (ideally after that night's backup).

### Every 14 days

1. Double-click **Renew Lightroom sign-in** when the daily brief or dashboard says it's about to expire (Adobe limits it to 14 days).
   Approve in the browser; if the browser ends on httpbin.org, paste that page's address when asked. Next due: **5 Dec 2026**.

## Where everything is

| What | Where |
|---|---|
| Originals | `P:\Xt5\raw\YYYY\MM-DD\*.RAF`, `P:\Xt5\jpg\YYYY\MM-DD\*.JPG` (`P:` = `\\192.168.15.10\photo`). By date taken from 2026-11-21; older folders are by copy date and stay that way |
| Edited photos | `P:\Xt5\Edited\YYYY\YYYY-MM Month\YYYY-MM-DD NAME.jpg` (full size, edits applied); edit settings in `Edited\YYYY\_recipes\NAME.json`; layered Photoshop files in `Edited\_photoshop\` |
| Lightroom inbox | `C:\Photos\Lightroom Inbox\YYYY-MM-DD\` (cleared after 14 days, only once every file is verified on the NAS) |
| Manual export fallback | Lightroom export to `C:\Photos\Export Outbox\`; Import Photos files it into `Edited` |
| Off-site backup | Work OneDrive → `Backups/Photos/Xt5` (mirror), `Backups/Photos/_deleted/YYYY-MM-DD/` (90 days) |
| What's been imported | `P:\Xt5\.import\manifest.csv` (name + size) |
| What's been saved from Lightroom | `P:\Xt5\Edited\.manifest.csv` |

## What runs by itself

| When | Job | Where | Log |
|---|---|---|---|
| 1:00 am daily | **Edit saver**: asks Adobe's Lightroom API what changed; saves full-size JPEGs of edited/Pick photos to `Edited`; never deletes | wyse crontab → `Wyse5070DebSetup/setup-photo-sync.sh --run-edits` | `~/photo-sync.log` on wyse |
| 2:00 am daily | **Backup**: `rclone sync` of `Xt5` to OneDrive, then a size check of every file | wyse crontab → `setup-photo-sync.sh --run-backup` | `~/photo-sync.log` on wyse |
| 6:30 am daily | **Daily brief** photo check: silent when all is well | wyse daily brief (`daily-brief/shared/photo_check.py`) | — |
| Sun 7:00 am | **Weekly health check** "Photos:" line, always present | wyse | — |

## How you know it's working

- **Import Photos**: the ✔ / ✘ last line, a Windows notification, and a log per run in `cam_copy\logs\`.
- **Dashboard** (http://192.168.15.30:8085, card "Photo backup + edit saver"): last backup, files in OneDrive, "Edits on NAS: N of N", days left on the Lightroom sign-in. Amber badge = needs attention.
- **Daily brief**: a 📷 line in 🎯 Needs you only when something's wrong: backup failed or not run in 48 h, photos missing from OneDrive, edited photos not saved after 2 nights, a Photoshop edit with no layered copy after 2 nights, edit saver not run in 48 h, sign-in expiring within 3 days or expired.
- **Sunday health check email**: "Photos: backed up nightly ✔ …; edits N of N ✔; Lightroom sign-in good until …".

## When something's wrong

| Symptom | Fix |
|---|---|
| Import says `P:` isn't reachable | It tries to reconnect by itself. If not: open DSM http://192.168.15.10:5000/ → Control Panel → Shared Folder (`photo` exists) and File Services → SMB (enabled); then `net use P: \\192.168.15.10\photo /persistent:yes` (account `mwilkie`) |
| Import says no camera found | Camera on? USB cable in? It must show as **X-T5** under This PC |
| ✘ INCOMPLETE | Run Import Photos again; the log in `cam_copy\logs\` names what failed. Keep the camera awake (auto power off stops copies) |
| 📷 Lightroom sign-in expiring/expired | Double-click **Renew Lightroom sign-in** (it also saves any missed edits right away) |
| 📷 Edited photos not saved / edit saver hasn't run | On wyse: `~/github/HomeAutomation/Wyse5070DebSetup/setup-photo-sync.sh --run-edits`, then `tail ~/photo-sync.log` |
| 📷 Backup failed / photos not in OneDrive | On wyse: `setup-photo-sync.sh --run-backup`; `tail -30 ~/photo-sync.log`; list of missing files in `/mnt/data/appdata/photo-sync/not-in-onedrive.txt`. If OneDrive sign-in expired: `TripTracker/scripts/setup_onedrive_remote.sh` |
| NAS password changed | ssh to wyse, then: `read -rsp "mwilkie NAS password: " p && printf 'NAS_USER=mwilkie\nNAS_PASS=%s\n' "$p" > /mnt/data/appdata/photo-sync/.env && chmod 600 /mnt/data/appdata/photo-sync/.env && unset p`; then rerun `setup-photo-sync.sh` |

## Cleaning up local copies

| Local copy | What happens | Safe to delete? |
|---|---|---|
| `C:\Photos\Lightroom Inbox\<date>\` | Cleared automatically after 14 days, only if every file is verified on the NAS | Yes, once the import said ✔ and you've done Add Photos |
| Lightroom's own storage (`%LOCALAPPDATA%\Adobe\Lightroom CC\...`) | Managed by Lightroom ("Store a copy of all originals locally" is on) | **Never by hand.** To free space, untick that setting or lower the cache in Lightroom's Preferences |
| `C:\Photos\.staging\` | Temporary; emptied as photos reach the NAS | Should be empty; anything left is reused or cleared by the next run |
| `C:\Photos\Export Outbox\` | Emptied by Import Photos as files are filed into `Edited` | Anything still there isn't filed yet: run Import Photos |
| Any other folder (e.g. on the desktop) | Not tracked | **Right-click the folder → Send to → Check against NAS** (or `.\Import-Photos.ps1 -Check <folder>`) |

**Check against NAS** lists every photo in the folder and deletes nothing:
- **✔ safe**: the same original is on the NAS, or the edited version is in `Edited` (renamed exports are matched by the time they were taken).
- **≈ covered**: a smaller or different copy; the NAS has the full original.
- **✘ ONLY HERE**: not on the NAS. Keep it, or import it (Fujifilm files) / put finished edits in the Export Outbox.

It ends with one line, e.g. "✔ 21 photos checked: all 21 are on the NAS (or a better version is). This folder is safe to delete."

## Getting a photo back

- **From OneDrive**: work OneDrive → `Backups/Photos/Xt5/...` (same folders as the NAS). A photo deleted or replaced on the NAS in the last 90 days is in `Backups/Photos/_deleted/<date it disappeared>/`. OneDrive's own recycle bin (93 days) and version history are behind that.
- **An edit**: `P:\Xt5\Edited\...` (and the same path in OneDrive). The settings that made it are in `_recipes`.
- **Deleted in Lightroom**: Lightroom's Deleted folder (60 days); the saved edit stays on the NAS regardless.

## Things worth knowing

- **Camera clock** decides the folder date. Keep it on local time (it was on UK time until 2026-11-21).
- Over USB the X-T5 reports file size 0 and no modified date; Import Photos reads `System.Size` / `System.Photo.DateTaken` instead. A card reader needs no special handling.
- **Lightroom (cloud)** never reads the NAS after you add photos; "Store a copy of all originals locally" is on, so editing runs from `C:`.
- The Adobe developer project still redirects sign-ins to httpbin.org (the console says localhost; retest later). Renewal handles both.
- wyse reaches the NAS as `mwilkie`: the `photo` share uses Linux permissions, so a separate limited user can't be given `Xt5` alone.
- Wi-Fi to the NAS is slow (802.11n, ~1–2 MB/s); Ethernet on this PC, or ac/ax on the access point, would speed up imports.

## Code and settings (for changes)

| Piece | Code | Settings |
|---|---|---|
| Import Photos | `cam_copy/Import-Photos.ps1`, `cam_copy/PhotoImport/PhotoImport.psm1`; tests `Invoke-Pester cam_copy\tests` | `cam_copy/import-config.json` |
| Edit saver | `lightroom/lightroom_tool/edit_saver.py` (`python -m lightroom_tool save-edits`); tests `lightroom/tests/test_edit_saver.py` | wyse: `/mnt/data/appdata/photo-sync/lightroom.env`, `lightroom-tokens.json` |
| Lightroom sign-in | `lightroom/lightroom_tool/auth/auth_commands.py` (`auth signin`, `auth export-token`), `lightroom/Renew-LightroomSignIn.ps1` | PC: `lightroom/.env` (client ID) |
| Nightly jobs | `Wyse5070DebSetup/setup-photo-sync.sh` (re-run to update; no sudo) | wyse: `/mnt/data/appdata/photo-sync/.env` (NAS login, chmod 600) |
| Brief / weekly checks | `Wyse5070DebSetup/daily-brief/shared/photo_check.py`; deployed by `setup-daily-brief-oai.sh` / `setup-daily-brief-cc.sh` | — |
| Dashboard card | `Wyse5070DebSetup/dashboard/index.html` (reads `photo-status.json`); deployed by `setup-dashboard.sh` | — |
