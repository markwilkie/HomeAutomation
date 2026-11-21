# Photo Workflow Plan: camera → NAS → OneDrive

Status: agreed 2026-11-21, not yet implemented.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Folder naming | **Date taken**, same shape as today: `Xt5\{raw,jpg}\YYYY\MM-DD` | Photos file under the day you shot them; the `lightroom` tool keeps working with one setting change (§3) |
| Backup method | **rclone on wyse, nightly** | wyse is already signed in to the work OneDrive (no new sign-in); verifies uploads; keeps deleted photos 90 days; shows on the dashboard; all in git (§7) |
| Backup scope | **`Xt5` only** (15 GB) | Your call; the job can take more folders later |
| Import sources | **Both the camera over USB and an SD card reader**, detected automatically | Your call (§4) |
| NAS account for wyse | **New DSM user `photo-sync`: read-only on `photo`, write access only to `Xt5\Edited`** | wyse needs a NAS login for the backup (read) and the edit saver (write). This way a bug or compromise on wyse can't delete or overwrite your originals. One 5-minute visit to DSM |
| Card formatting | **In camera, both cards, just before the next shoot**, and only after ✔ | §5 |
| Runs crossing midnight | Not handled specially | Your call; taken-date folders make it moot anyway |
| Faster Lightroom: adding photos | **Import tool also drops new JPGs in a local Lightroom inbox** (`C:\Photos\Lightroom Inbox\YYYY-MM-DD`); you add from there, not from the NAS | Adding from the NAS runs over Wi-Fi and is very slow (measured 1.2 MB/s, ~20 min for 62 JPGs). From `C:` it takes seconds (§6) |
| Faster Lightroom: editing | **Turn on Lightroom's own "Store a copy of all originals locally"** | Lightroom (cloud) never reads from the NAS after you add photos; it works from its own local copy or downloads from Adobe. This one setting makes every photo local (§6) |
| Getting edits to the NAS | **Automatic: an "edit saver" on wyse checks Lightroom's cloud every night at 01:00 and saves a full-size JPG of every photo you've edited or flagged Pick.** It re-saves a photo when you re-edit it, and alerts you if anything is missing. Manual export stays as a fallback | Exporting by hand gets forgotten. Lightroom (cloud) has no auto-export, but Adobe's Lightroom API can produce full-size renders with your edits, and your `lightroom` tool already holds the needed permissions (§6) |
| Where edited photos live | **Their own tree: `P:\Xt5\Edited\YYYY\YYYY-MM Month\YYYY-MM-DD WJD01451.jpg`** | Easy to find and browse: one folder per month, files sorted by date and self-describing even when copied elsewhere. Originals stay in `raw`/`jpg` by day (§3) |

## 1. What you do

```
After a shoot
1. Plug in the camera over USB (or put a card in a reader)
2. Double-click "Import Photos" on the desktop
3. Wait for the green line:   ✔ 62 RAF + 62 JPG safe on NAS
   The window shows the local inbox folder, e.g. C:\Photos\Lightroom Inbox\2026-11-21
4. In Lightroom: Add Photos → that inbox folder (local disk, takes seconds)

Editing (whenever)
5. Cull and edit in Lightroom as usual; reject the losers
6. Flag the keepers you didn't edit as Pick (P key) if you want them in Edited too
7. Run the lightroom tool's cleanup: rejected photos (JPG + RAF) are removed from the NAS
   Nothing else: by the next morning, every edited or Pick photo appears in
   P:\Xt5\Edited\2026\2026-11 November\

Before the next shoot
8. Format both cards in the camera
   (the import window / tonight's backup tells you it's safe)
```

No reruns, no exporting, no checking logs. Edits reach the NAS overnight, and backup to OneDrive happens every night without you, covering originals and edited photos.

## 2. Design principles

1. **The NAS is the library.** The PC only moves files from camera to NAS. Backup runs from the NAS side, so the PC doesn't need to be on.
2. **Verified or it didn't happen.** A file counts as copied only when its size on the NAS matches the camera. The tool loops until every file is safe, or tells you exactly what isn't.
3. **Route by file type, not by slot.** `.RAF` goes to `raw`, `.JPG` goes to `jpg`, whichever card or slot it's on. Then it doesn't matter which card is in the reader, or if you change the camera's slot settings.
4. **Never delete from the card.** Only you format, in the camera.
5. **One small codebase, every setting used, every run logged.**

## 3. Folder structure

### Recommendation: keep the current shape, but use the date the photo was **taken**

```
P:\Xt5\                          (= \\192.168.15.10\photo\Xt5)
├── Edited\                      ← YOUR FINISHED PHOTOS (§6), filled automatically
│   └── 2026\
│       ├── 2026-11 November\
│       │   ├── 2026-11-15 WJD01451.jpg
│       │   └── 2026-11-21 WJD01503.jpg
│       └── _recipes\            (edit settings, one small file per photo)
├── raw\                         ← originals, by day taken
│   └── 2026\
│       ├── 11-15\   WJD01451.RAF ...     ← shot on 15 Nov, whenever you import
│       └── 11-21\   WJD01499.RAF ...
└── jpg\
    └── 2026\
        ├── 11-15\   WJD01451.JPG ...
        └── 11-21\   ...
```

Two schemas on purpose:
- **Originals** (`raw`, `jpg`): by day, matching what the `lightroom` tool expects. Many files, rarely browsed.
- **Edited**: by month, date in every file name. A few dozen photos a month, the folder you actually open, share from, and print from. A month folder holds a manageable number; `2026-11-21 WJD01503.jpg` still says when it was taken after you email or copy it. The camera number links it back to its original.

Why this one:
- **Taken date beats copy date.** Today, photos from a trip land in whatever day you happened to plug in. Using the date taken puts them under the day you shot them, and a RAF and its JPG always share a folder.
- **Same shape as today.** Your `lightroom` tool builds `p:\xt5\{raw,jpg}\YYYY\MM-DD` paths itself. Keeping the shape means its only change is one setting: `default_date_type: "import"` → `"capture"` in `lightroom/config.yaml`. That needs a test (§9).
- **Existing folders stay as they are.** Older imports are filed by copy date. That's usually within a few days of the shoot, and moving them could confuse Lightroom's matching. Not worth the risk.

Alternatives considered:
- **RAF and JPG together** (`Photos\2026\2026-11-15\*.RAF + *.JPG`). Common for photographers, but it breaks the `lightroom` tool's raw/jpg split and needs a migration. Not recommended unless you retire that tool.
- **Event names** (`11-15 Rattlesnake Ledge`). Nice to browse, but needs typing at import time and breaks the tool's date lookup. You can still add these later as a Lightroom album instead.

Where the date taken comes from: the camera file's date (the X-T5 stamps it at capture), read from the camera over USB or from the file on a card reader. EXIF `DateTimeOriginal` is the fallback.

## 4. Import tool (Windows PC)

### Works with the camera's USB connection or a card reader, picked automatically

| Source | How it's found | How it copies | Speed (44 MB RAF) |
|---|---|---|---|
| **Camera over USB** (what you do today) | Device named `X-T5` under This PC; walks `SLOT 1` *and* `SLOT 2` | Windows Shell copy into a hidden `.incoming` folder on the NAS; checked for full size; then moved into place | ~15 s each (~3 MB/s) |
| **SD card reader** (optional, faster) | Any drive with `DCIM\*_FUJI` | `robocopy` (built-in retries, restartable) into `.incoming`, size check, move into place | ~1 s each |

If both are connected, it does both in one run. A card reader is optional: the camera's USB connection is a fully supported path, it's just slower. (The X-T5's "USB card reader" connection mode still shows up as a camera device, not a drive letter, so a real drive letter needs a separate SD reader.)

### What a run does

```
Preflight ─► List every photo (fixed list) ─► Compare with what's already on the NAS
    ─► Copy missing ─► Check sizes ─► Repeat until nothing is missing (max 5 passes) ─► Verdict
```

1. **Preflight**
   - `P:` present? If not, try `net use P: \\192.168.15.10\photo`. If that fails, show the Synology checklist (DSM at http://192.168.15.10:5000/).
   - `P:\Xt5` writable? Test with a probe file.
   - Enough free space for this import?
   - Camera on? Remind you to switch off auto power off, or keep the camera awake.
2. **List**: take a fixed list of every RAF and JPG on all cards *before* copying. This fixes today's "stops after 14 files" bug.
3. **Compare** against a list of every file already copied, stored on the NAS (`P:\Xt5\.import\manifest.csv`), matched by *name + size*.
   - Files imported last week are skipped. Old photos left on the card cost nothing.
   - Fuji file numbers repeat after 9999, which name + size still tells apart.
   - The first run builds the list from your existing `raw` and `jpg` folders.
4. **Copy** in two hops, so the camera is read only once and Lightroom never waits on Wi-Fi:
   1. Camera → **local staging on `C:`** (`C:\Photos\.staging\`), size-checked. Fast, and it frees the camera sooner.
   2. Local → NAS `.incoming`, size-checked, then moved into its taken-date folder. A half-copied file never lands in the library, and the next run cleans up `.incoming`.
   3. New **JPGs** are also moved from staging into the **Lightroom inbox** `C:\Photos\Lightroom Inbox\YYYY-MM-DD\` (by date taken). RAFs are deleted from staging once they're safe on the NAS.
5. **Repeat** until nothing is missing.
6. **Verdict**, as the last line in the window, a Windows notification, and an exit code:
   - `✔ All 62 RAF + 62 JPG safe on NAS (48 new, 76 already imported)`
   - `✘ INCOMPLETE: 3 RAF not copied (camera disconnected?). Don't format. Run again.`
   - Plus the backup status of these photos (§7).

### What's in the code

```
cam_copy/
├── Import-Photos.ps1          # entry point; desktop shortcut points here
├── PhotoImport/               # PowerShell module
│   ├── Sources.Camera.ps1     # camera over USB (Shell COM), hardened
│   ├── Sources.Drive.ps1      # card reader (robocopy)
│   ├── Plan.ps1               # PURE: card list + manifest → what to copy, where
│   ├── Manifest.ps1
│   ├── Preflight.ps1
│   └── Report.ps1             # per-run log in logs\, notification, verdict
├── tests\                     # Pester: Plan, naming, manifest, config
├── config.json
└── README.md                  # one-page runbook
```

About 400 lines, replacing today's 1,150. Removed: GUI, `CameraDetector.ps1`, `MTPHelper.ps1`, `FileCopier.ps1`, the execution-policy change, unused config.

```json
{
  "CameraName": "X-T5",
  "Library":    { "raw": "P:\\Xt5\\raw", "jpg": "P:\\Xt5\\jpg" },
  "Extensions": { "raw": [".RAF"], "jpg": [".JPG"] },
  "FolderFormat": "yyyy\\MM-dd",
  "DateSource":   "Taken",
  "Share":      { "Drive": "P:", "Unc": "\\\\192.168.15.10\\photo" },
  "LocalStaging": "C:\\Photos\\.staging",
  "LightroomInbox": { "Path": "C:\\Photos\\Lightroom Inbox", "Types": ["jpg"], "KeepDays": 14 },
  "MaxPasses": 5
}
```

Switches: `-WhatIf` lists what would be copied and where, then exits. `-Rescan` rebuilds the manifest from the folders.

## 5. Card formatting: recommendation

**Format both cards in the camera right before your next shoot, not right after importing.**

- After import, your photos are on the card and the NAS: two copies, in one house. After that night's backup, they're also in OneDrive: three copies, one off-site. Leaving them on the card until the next shoot keeps a spare copy through the riskiest window, the hours before the first backup.
- The import tool skips photos it already imported. Leaving them on the card costs nothing except card space, and the X-T5's cards hold hundreds of shots.
- **Format in the camera** (MENU → User Setting → Format), never by deleting on the PC. Formatting in the camera resets the card's file system the way the camera expects, avoids leftover fragments, and is the most reliable method Fujifilm supports.
- **Only format when the import tool or the backup status says so.** Since the verdict is based on per-file size checks, "safe" really means every file is there. If it ever says INCOMPLETE, don't format.
- Format **both** cards (RAF and JPG) at the same time, so they always hold the same set of shots.
- In a hurry (card full mid-trip)? Formatting after the green ✔ line is fine. You're just one copy short until that night's backup.

## 6. Lightroom: faster, and edits never lost

### How Lightroom fits (checked on this PC, 2026-11-21)

You run **Lightroom (cloud)**, `C:\Program Files\Adobe\Adobe Lightroom CC\`, not Lightroom Classic. That decides how both speed and safety work:

- **Adding photos copies them in.** Lightroom copies each photo into its own storage (`%LOCALAPPDATA%\Adobe\Lightroom CC\Data\…\originals\`) and uploads it to Adobe's cloud. After that it never reads the NAS file again.
- **Today almost nothing is local.** "Store a copy of all originals locally" is **off**. Only 20 of 216 photos (0.2 GB, all JPG) are on this PC, so most photos load from Adobe's cloud when you open them. That's the slowness.
- **Edits sync to Adobe's cloud**, alongside the local catalog. They are *not* written into the files on the NAS.
- **Today you add JPGs.** The local originals are all JPG, and the `lightroom` tool culls by JPG and removes the matching RAF.

### Where each copy lives, and what it's for

| Copy | Where | Job | Can you lose it? |
|---|---|---|---|
| Card | Camera | Spare until the first backup | Yes, on purpose: formatted before the next shoot |
| **Originals (master)** | NAS `Xt5\raw` + `Xt5\jpg` | The archive of everything you kept | No: backed up nightly to OneDrive |
| Lightroom inbox | `C:\Photos\Lightroom Inbox\` | Fast adding to Lightroom | Yes: auto-deleted after 14 days, once verified on the NAS |
| Lightroom local | `C:` (Lightroom's own folder) | Fast editing | Yes: Lightroom re-downloads it |
| Lightroom cloud | Adobe | Edits + synced originals; phone/web access | Only if sync fails or the subscription lapses, hence the next row |
| **Edited photos** | NAS `Xt5\Edited\YYYY\YYYY-MM Month\` | Your edits, in a form that outlives Lightroom | No: saved automatically each night, then backed up to OneDrive |

Rule of thumb: **the NAS holds what matters (originals and edited photos); Lightroom is the workbench.**

### Faster adding: a local Lightroom inbox

Adding photos from the NAS is slow because Lightroom reads every file over Wi-Fi. Measured on 2026-11-21:

| Link | Speed | What it means |
|---|---|---|
| PC Wi-Fi (Intel AX200 → `WILKIE-DS-LFP`, 5 GHz, signal 89%) | Connected as **802.11n at 144 Mbps**, though the card supports Wi-Fi 6 | Old Wi-Fi mode; the access point or its settings are holding it back |
| PC reading from NAS | **1.2 MB/s** (125 MB in 103 s; Lightroom may have been adding photos at the same time) | ~20 s per 26 MB JPG; ~20 min to add 62 JPGs |
| wyse wired link | **100 Mb/s** (should be 1 Gb/s) | Usually a bad cable or switch port. Fine for the backup's size, but worth a new cable |

The fix in the workflow: the import tool copies from the camera to `C:` first and leaves each new JPG in **`C:\Photos\Lightroom Inbox\YYYY-MM-DD\`**. You add from there, so Lightroom reads from the local SSD and takes seconds. Then Lightroom copies the photos into its own storage and uploads them to Adobe in the background.

- **The inbox is a convenience copy, not the archive.** The NAS copy is the master. Formatting advice is based only on the NAS check.
- **Cleanup:** inbox folders older than 14 days are deleted automatically, but only if every file in them is verified on the NAS. By then Lightroom has its own copy.
- **Space:** a 62-shot day is ~1.6 GB of JPGs; 14 days of inbox stays a few GB.
- **RAFs:** not put in the inbox, because you add JPGs today. If you want a RAF in Lightroom, add it from `Xt5\raw` (one file over Wi-Fi is fine), or set `"Types": ["jpg","raw"]` in the config.

The network fix is separate and helps everything (Phase 3):
- **Best:** an Ethernet cable to this PC. Reads from the NAS would jump to ~100 MB/s if the NAS is on gigabit.
- **Otherwise:** check the access point's 5 GHz settings for ac/ax mode and 80 MHz channels. The AX200 should then connect at 866-1200 Mbps instead of 144.
- Check the NAS's own link speed in DSM (Control Panel → Network → Network Interface), and replace wyse's cable.

### Faster editing: turn on local originals (a setting, no code)

Lightroom → Edit → Preferences → **Local Storage**:
1. Tick **Store a copy of all originals locally**.
2. Leave the location on `C:`. It has 290 GB free; your library is ~6 GB of JPGs today (216 × ~26 MB), and a year of shooting adds ~15-30 GB of JPGs.
3. Leave **Cache size** as it is (currently ~77 GB).

Lightroom then downloads the missing originals once and keeps every new one local. Opening, zooming and editing run from `C:`. Lightroom manages this copy and keeps it in step with the cloud, so it's never edited separately and can't drift. Don't make your own copy of the NAS for Lightroom; it wouldn't be used.

### Edits saved to the NAS automatically ("edit saver")

**The problem:** Lightroom (cloud) keeps edits in Adobe's cloud and never writes them to the NAS. It has no auto-export. Relying on you to export by hand isn't bulletproof: one forgotten session and those edits exist only at Adobe.

**The answer:** a small job on wyse that asks Adobe's Lightroom API, once a night, "what changed?", and saves every edited photo to the NAS itself.

```
Lightroom (cloud) ──01:00 nightly, API──► wyse "edit saver" ──► P:\Xt5\Edited\YYYY\YYYY-MM Month\
                                                   │
                                                   └──► status: "Edits on NAS: 187 of 187 ✔" (dashboard, import tool)
```

**Which photos:** every photo you've **edited** (any develop change) **or flagged Pick**. Pick covers keepers that were good straight out of the camera. Rejected photos are never saved.

**What a run does:**
1. **Ask what changed.** It lists photos updated since the last run (the API's `updated_since`, which your `lightroom` tool already uses). Cheap, even with years of photos.
2. **Decide.** For each edited or Pick photo, compare its edit settings with the last saved version in the edit manifest (`Xt5\Edited\.manifest.csv`: Lightroom photo ID, file, edit fingerprint, saved at, size). It's new, re-edited, or unchanged.
3. **Render.** Ask Adobe for a **full-size render with your edits applied** (the Lightroom API's rendition service, using the `lr_partner_rendition_apis` permission your tool already requests). Download it.
4. **Verify.** The JPEG must open cleanly and be full size (long edge matches the cropped original, not a 2048-px preview). Write it to `Edited\.incoming\` first, then move it into place. The same rule as originals: nothing half-written lands in the library.
5. **Name and file it:** `Edited\2026\2026-11 November\2026-11-21 WJD01503.jpg`, using the date taken and the camera file name. Add `-2`, `-3` only if two different photos would get the same name.
6. **Save the recipe** next to it in `Edited\2026\_recipes\WJD01503.json`: the edit settings as Lightroom reports them. Small, and it records exactly what you did.
7. **Re-edits** replace the file in place. The previous version isn't lost: the nightly backup moves it to OneDrive's `_deleted` folder (kept 90 days), and OneDrive keeps file versions too.
8. **Never deletes.** If you later delete or reject an edited photo in Lightroom, its saved copy stays. The run lists it under "removed in Lightroom" for you to delete by hand if you want.

**The completeness check (what makes it bulletproof):** every run counts *all* edited and Pick photos in Lightroom and compares them with the manifest, not just what changed. The result is a single line:

- `Edits on NAS: 187 of 187 ✔`, or
- `Edits on NAS: 185 of 187 ✘: WJD01503, WJD01510 failed (render timed out); retrying`

It shows on the wyse dashboard, in the Import Photos window, and in the status file. A failed photo is retried on the next night's run. If anything is still missing after 2 nights, or the job hasn't run in 48 hours, **you get an alert** (the same channel as backup failures).

**Where it runs:** wyse, nightly at 01:00 (before the 02:00 backup), systemd timer, checked-in setup script, dashboard card and README row (house rules). It runs on wyse, not the PC, so it works when the PC is off. It writes through the `photo-sync` DSM user, which can only write to `Xt5\Edited`.

**Adobe sign-in lasts 14 days, then needs a 30-second renewal.** Tested 2026-11-21: your Adobe credential (type "OAuth Single-Page App") issues a refresh token that lasts exactly 14 days, and refreshing does *not* extend it. A Web App credential might, but Adobe no longer offers the Lightroom API to add to projects, so we keep the working credential and make renewal painless:
- **Warning:** every run reads the sign-in's expiry. From 3 days before, the dashboard shows "Lightroom sign-in expires in N days" and you get an alert.
- **Renewing:** desktop shortcut **Renew Lightroom sign-in**. It opens Adobe; you approve. It finishes by itself via `localhost:8080`. If Adobe still redirects to httpbin.org, a box asks you to paste the address. Then it copies the new sign-in to wyse (`scp` to the edit saver's token file, outside git).
- **If you miss it:** nothing is lost. The saver pauses, the status turns red, and the night after you renew it catches up on every edit; the completeness check counts what's missing.

**Code:** Python, built on your existing `lightroom` tool (`lightroom/lightroom_tool/`), which already has the Adobe sign-in, the API client and paging. New pieces: renditions, the manifest, the completeness check. About 300 lines plus tests.

**Spike result (2026-11-21): GO.** Tested on your library through the `lightroom` tool's sign-in:
- **Full resolution, edits applied, crop respected:** the RAF `WJD01472` rendered at 5152×7728 (its full size); the JPG `WJD01472` at 5115×7672 and `WJD01486` at 3758×2505, exactly their cropped sizes. The black-and-white conversion, the sky and the crop are all visibly applied, and the photo comes out upright.
- **Quality:** JPEG quality ~91 (the camera's own is ~94), about 4-6 MB per 40 MP photo. Fine for prints.
- **"Edited" marker:** `develop.fromDefaults = false` (present on 231 of 234 photos; `userUpdated` covers the rest). **70 of your 234 photos are edited**, so the first backfill is ~70 files, ~300 MB.
- **Library today:** 233 JPG + 1 RAF; no Pick or Reject flags set.
- **Naming detail found:** Lightroom keeps duplicate-import names like `WJD01486 (1).jpg`. The saver strips the ` (1)` and relies on the `-2` collision rule.
- **Sign-in detail found:** your Adobe developer project's redirect is set to `https://httpbin.org/get`, a public test site, so the one-time sign-in code passes through a third party. PKCE stops anyone else using it, but change the redirect in the Adobe console to `http://localhost:8080/callback` before the wyse setup.

### Fallback: manual export (kept, rarely needed)

If the API path breaks (Adobe changes something, or the sign-in lapses), you can still save edits by hand without losing the schema or the check:

- Lightroom export preset **"To NAS"** → `C:\Photos\Export Outbox\`, full-size JPG, quality 90-100, sRGB. Exporting to `C:` takes seconds; exporting over Wi-Fi to the NAS doesn't.
- Import Photos (run any time, camera or not) and a daily task on the PC move outbox files into `Edited\YYYY\YYYY-MM Month\` with the same names and checks, deleting each local copy only after it's verified on the NAS.
- The completeness check counts these too (matched by camera file name and date taken), so `187 of 187` stays honest either way.
- For big RAW edits you'd want to redo in another editor: **Export → Original + Settings** to the outbox. It writes the original plus an `.xmp` edit recipe that Lightroom Classic and Camera Raw can read. These are filed into `Edited\…\_recipes\` beside the JSON.

### Habits (two)

1. **Let sync finish** before shutting down after editing (cloud icon, top right). The edit saver can only save what has reached Adobe's cloud. An unsynced edit exists only on this PC until it syncs, and the job picks it up the night after that.
2. **Pick (P) the keepers you didn't edit**, if you want them in `Edited` too.

### What protects your edits

| Problem | What saves you |
|---|---|
| You forget to export | Nothing to forget: edits are saved every night |
| A save fails | The completeness check retries and alerts you by name |
| PC disk dies | Lightroom re-downloads from Adobe; originals and edited photos are on the NAS |
| Sync error or conflict loses an edit in Lightroom | The saved JPG and recipe on the NAS / OneDrive |
| Adobe subscription lapses, or Adobe loses data | NAS originals + edited photos + recipes, all in OneDrive |
| You re-edit and regret it | The previous saved version is in OneDrive `_deleted` (90 days) and OneDrive versions |
| You delete a photo by mistake | Lightroom's Deleted folder (60 days); the saved edit stays on the NAS |
| NAS dies | OneDrive has originals and edited photos |
| Edit saver breaks silently | It can't: "hasn't run in 48 h" is itself an alert |

### Should you add RAFs to Lightroom instead of JPGs?

Keep adding JPGs as today, because the `lightroom` cleanup tool is built around culling JPGs. For a keeper that needs serious work (highlights, shadows, white balance), add its RAF from `Xt5\raw\…` and edit that version. RAFs are ~44 MB vs ~26 MB, so do it per photo, not wholesale.

## 7. Backup to OneDrive

### Where it runs: wyse, nightly, with rclone

wyse already has a working, tested rclone remote `onedrive_backup` on the **work (Business Basic) OneDrive**, which TripTracker's backup uses. That drive is 1 TiB with ~1,020 GB free.

```
NAS \\192.168.15.10\photo\Xt5  ──(rclone, SMB, read-only user)──►  OneDrive: Backups/Photos/Xt5/
                                                                     OneDrive: Backups/Photos/_deleted/YYYY-MM-DD/
```

- **What's covered:** `Xt5\raw`, `Xt5\jpg`, `Xt5\Edited` (edited photos and recipes, §6) and both manifests. Not `.incoming` (half-copied files).
- **Order:** the edit saver's 01:00 run finishes before the 02:00 backup, so a day's edits reach OneDrive the same night.
- **Nightly at 02:00**, systemd timer, script checked in under `Wyse5070DebSetup/` (house rule: a script writes the unit; no hand-run commands).
- **Command:** `rclone sync` with `--backup-dir`. New and changed photos are uploaded. Anything deleted or replaced on the NAS (e.g. the `lightroom` tool's reject cleanup, or an accident) is *moved* to `OneDrive: Backups/Photos/_deleted/YYYY-MM-DD/`, not lost. Kept 90 days, then pruned.
- **Checks:** after uploading, run `rclone check --size-only` on new files. A weekly full `rclone check` against OneDrive's hashes.
- **Status file:** writes `P:\Xt5\.import\backup-status.json` (last run, result, newest photo backed up). The import tool reads this to say "✔ also in OneDrive" or "⏳ OneDrive backup tonight", which drives the formatting advice in §5.
- **Visible:** a card on the wyse dashboard (8085) showing last backup time, result and size, plus a row in `Wyse5070DebSetup/README.md` (house rule). An SMS or notification on failure.
- **Limited NAS account** for wyse (DSM user `photo-sync`): read-only on `photo`, write only to `Xt5\Edited`. If wyse were ever compromised, it couldn't delete or encrypt your originals.
- **Size and speed:** `Xt5` is **15 GB** today (the `lightroom` cleanup keeps it lean), so the first upload takes about an hour or two. After that, each session is ~4-5 GB (60 shots × ~70 MB per RAF+JPG pair), minutes per night.

### Why not the alternatives

| Option | Verdict |
|---|---|
| **Synology Cloud Sync** (to OneDrive for Business, one-way upload) | **Fallback if wyse is ever retired.** Would also need a new sign-in to the work account from DSM. Zero code, runs on the NAS. But it's a sync without a deleted-files folder, and its status isn't visible on your dashboard. Use it if you'd rather not run anything on wyse. Set it to *upload only* and *don't remove files in the destination when removed from the source*. |
| Synology Hyper Backup | Versioned, which is better than sync in principle, but stores files in its own backup format (you can't browse photos in OneDrive). Whether it supports OneDrive as a target needs checking in DSM. |
| OneDrive app on the PC (copy into the `OneDrive - Wilkie Family` folder) | No. The PC must be on, and it uses PC disk space or needs Files On-Demand fiddling. Backup belongs next to the library. |
| Personal OneDrive | No. Quota unconfirmed; the work OneDrive is the agreed target. |

### Protecting against OneDrive mistakes too
- OneDrive for Business keeps file versions and a 93-day recycle bin, so ransomware-style overwrites are recoverable.
- **Optional, later:** a monthly **local** copy to a USB drive plugged into the NAS (Synology USB Copy or Hyper Backup). That gives three copies on two kinds of media, one off-site: the classic 3-2-1.

### Photo check in the daily brief

The 6:30 daily brief (wyse, `Wyse5070DebSetup/daily-brief/azure/`) gets a **photos** prefetch, like the existing sober count: a few lines of Python that run before the model writes the brief, so the facts are exact.

- **Reads** the status files the backup and edit saver write locally on wyse (`/mnt/data/appdata/photo-sync/backup-status.json`, `edit-saver-status.json`). It doesn't need the NAS or Adobe, so the check itself can't fail because they're down.
- **Quiet when all is well:** nothing in the brief. It only speaks up when something needs you, as an item in **🎯 Needs you**:

| Condition | Brief says |
|---|---|
| Last night's backup failed, or no successful backup in 48 h | "📷 Photo backup failed last night (NAS unreachable): check wyse dashboard" |
| Edits on NAS fewer than edited in Lightroom after 2 nights | "📷 2 edited photos not saved to NAS: WJD01503, WJD01510" |
| Edit saver hasn't run in 48 h | "📷 Edit saver hasn't run since Mon" |
| Lightroom sign-in expires within 3 days, or has expired | "📷 Lightroom sign-in expires Thu: run Renew Lightroom sign-in (30 s)" |
| Import manifest shows photos on the NAS that aren't in OneDrive after 2 nights | "📷 48 new photos not in OneDrive yet" |

- **Prompt rule** (in `prompts/prompt.md`): put the photo line in 🎯 Needs you exactly as given, and never add a photo line when the prefetch is empty.
- **Weekly reassurance:** the Sunday weekly health check email gets one line whether or not anything's wrong ("Photos: backed up nightly ✔, edits 187 of 187 ✔, Lightroom sign-in good until Dec 5"), so silence in the daily brief is never mistaken for a broken check.

### Scope: `Xt5` only
Decided: back up `Xt5` only. For the record, the `photo` share also holds `Art`, `Community`, `Dad`, `Events`, `Isaac`, `Jason`, `Jennie`, `LightRoomBackup`, `Portraits`, `Published`, `Scanned Images Scratch` and `Sports` (~250 GB on the volume). Adding any of them later means adding a source folder to the job; it would fit in the OneDrive.

## 8. Roadmap

### Phase 0: Decisions ✔, quick settings, and the edit-saver spike
- [ ] You: create the DSM user `photo-sync` (read-only on `photo`, write on `Xt5\Edited`); steps given when Phase 1 starts
- [ ] You, now (2 min): Lightroom → Preferences → Local Storage → tick **Store a copy of all originals locally** (§6). Faster editing from today.
- [ ] You, now (5 min): create the Lightroom export preset **"To NAS"** → `C:\Photos\Export Outbox\`, full-size JPG, quality 90-100, sRGB. A stopgap until the edit saver runs; I'll file anything there by hand until then.
- [x] **Spike: GO** (2026-11-21). `lightroom` tool set up on this PC (`.venv`, `.env`, signed in); full-resolution edited renders confirmed for RAF and JPG; "edited" = `develop.fromDefaults == false` (§6).
- [x] You: changed the Adobe credential's redirect URI and pattern to `http://localhost:8080/callback`. As of 2026-11-21 Adobe still redirects to httpbin.org (likely not applied yet); retest later. The renewal tool handles either.
- [x] Sign-in lifetime tested: 14 days, not extended by refresh. A Web App credential isn't offered for Lightroom any more, so the plan uses a renewal reminder.

### Phase 1: Backup first (~1 session)
Protects what you already have, before touching the import code.
- [ ] wyse: SMB access to `photo` (rclone `smb` remote with the `photo-sync` user, no system mount needed)
- [ ] `Wyse5070DebSetup/photo-backup.sh` + `setup-photo-backup.sh` (writes the systemd service + timer)
- [ ] First full upload of `Xt5` (15 GB, ~1-2 h, in the background)
- [ ] Status file, dashboard card, README row, failure alert
- [ ] Test a restore: pull one RAF back from OneDrive and compare its hash
- [ ] Daily brief: `photos` prefetch in `daily-brief/azure/brief.py` + `jobs.json` + rule in `prompts/prompt.md` (backup checks first; edit-saver checks added in 1b); one-line photo summary in the Sunday weekly health check

**Done when:** the dashboard shows a green nightly backup, and a deleted test file shows up in `_deleted/`.

### Phase 1b: Edit saver (~1-2 sessions, right after the backup)
Edits are the only thing not already safe on the NAS, so this comes before the import tool.
- [ ] Python job in `lightroom/` reusing the existing sign-in and API client: changed-since listing, renditions, verify, name/file, recipes, manifest
- [ ] Completeness check every run; status file `Xt5\Edited\.status.json`
- [ ] Backfill: save every already-edited or Pick photo in your library (first run)
- [ ] wyse: Adobe token stored outside git (`.env` on the host); `setup-edit-saver.sh` writes the nightly (01:00) systemd timer
- [ ] Dashboard card ("Edits on NAS: N of N"), README row, alert on missing after 2 nights or no run in 48 h; a "Save edits now" option for when you don't want to wait
- [ ] Fallback: Export Outbox filing (in the import tool + daily PC task)
- [ ] Daily brief: add edit-saver and sign-in-expiry conditions to the `photos` prefetch
- [ ] Sign-in renewal: expiry warning from 3 days out (dashboard, alert, daily brief); "Renew Lightroom sign-in" shortcut (auto via localhost, paste box if Adobe uses httpbin); copies the token to wyse
- [ ] Remove the `lightroom` tool's debug print of the sign-in exchange (it prints the one-time code and verifier)

**Done when:** an edit made in Lightroom shows up in `Edited\YYYY\YYYY-MM Month\` the next morning, a re-edit replaces it, and the dashboard says `N of N ✔`.

### Phase 2: New import tool (~2 sessions)
- [ ] `Plan` as a pure function, tested first (Pester)
- [ ] Manifest + building it from the existing folders
- [ ] Camera-over-USB source with `.incoming` staging + size check + repeat passes
- [ ] Card-reader source with robocopy
- [ ] Preflight, verdict, notification, per-run logs, backup status line
- [ ] Two-hop copy: camera → local staging → NAS; new JPGs left in the Lightroom inbox; inbox cleanup after 14 days (only if verified on NAS)
- [ ] Desktop shortcut "Import Photos"; the verdict shows the inbox folder to add in Lightroom
- [ ] Show the edit saver's status line ("Edits on NAS: N of N") in the verdict
- [ ] Switch `lightroom/config.yaml` to `capture` dates; test the cleanup dry run
- [ ] Delete old scripts (tag `cam_copy-v1` first); README becomes a one-page runbook

**Done when:** all acceptance tests in §9 pass.

### Phase 3 (optional)
- Network: Ethernet to this PC, or fix the access point's 5 GHz mode (802.11n at 144 Mbps today); check the NAS link speed in DSM; replace wyse's cable (100 Mb/s today)
- Auto-start the import when the camera is plugged in (Task Scheduler on device arrival)
- Share edited photos: mirror `Xt5\Edited` into a OneDrive folder you can share with Jennie or open on your phone
- Local USB copy on the NAS (3-2-1)
- Extend the backup to other `photo` folders or other NAS shares

## 9. Acceptance tests

| # | Scenario | Expected |
|---|---|---|
| 1 | Fresh cards, 60+ shots, camera over USB | One run → ✔ all safe; files in taken-date folders |
| 2 | Same, via card reader, one card at a time | ✔ after each card; RAF/JPG routed correctly regardless of card |
| 3 | Immediate re-import | 0 copied, ✔ all safe |
| 4 | Unplug the camera mid-copy | ✘ INCOMPLETE with a count; nothing half-copied in the library; next run → ✔ |
| 5 | Re-import next week with old shots on the card | Only new shots copied; no duplicate folders |
| 6 | `P:` disconnected | Reconnects itself, or shows the Synology checklist |
| 7 | Nightly backup | New photos in OneDrive; status file updated; dashboard green |
| 8 | Delete a photo on the NAS | Shows up in OneDrive `_deleted/` after the next backup |
| 9 | Restore one RAF from OneDrive | Hash matches the NAS original |
| 10 | `lightroom` cleanup dry run | Still finds new photos (with `capture` dates) |
| 11 | Lightroom with local originals on, Wi-Fi off | Every photo opens and edits at full quality |
| 12 | Edit a photo in Lightroom, wait for the nightly run (or Save edits now) | Full-size JPG with the edit in `Edited\YYYY\YYYY-MM Month\YYYY-MM-DD NAME.jpg`; recipe in `_recipes`; status `N of N ✔` |
| 13 | Re-edit it | File replaced on the next run; old version in OneDrive `_deleted` after the backup |
| 14 | Flag an unedited photo Pick | Saved like an edit |
| 15 | Edit a RAF and a JPG original | Both saved at full resolution |
| 16 | Break the Adobe sign-in on purpose / let it reach 3 days from expiry | Alert with the renew shortcut; status red / amber; nothing deleted; after renewing, the next run catches up |
| 17 | Stop the job for 2 days | "No run in 48 h" alert, and a 📷 line in the next daily brief |
| 21 | Everything healthy | No photo line in the daily brief; Sunday health check shows the ✔ summary |
| 22 | Fail the backup on purpose (wrong NAS password) | Next morning's brief has "📷 Photo backup failed last night" in 🎯 Needs you |
| 18 | Fallback: export to the outbox, run Import Photos | Filed into `Edited` with the same naming; counted by the completeness check |
| 19 | Add a 62-JPG day to Lightroom from the inbox | Takes under a minute (vs ~20 min from the NAS) |
| 20 | Inbox folder 15 days old: all on NAS / one file missing on NAS | Deleted / kept, with a warning |

## 10. Appendix: problems in today's scripts this replaces

| Today | Fixed by |
|---|---|
| Run stops partway with no error (14/62 RAF) | Fixed file list + repeat passes |
| "Copied" means the file exists, so a truncated file is skipped forever | `.incoming` staging + size check |
| Duplicate check only looks in today's folder | Manifest by name + size |
| Verification compares the whole card with today's folder | Per-file verdict for files on the card |
| Failure is hidden (exit code ignored, "Check logs") | Clear verdict line + notification + exit code |
| Any device under This PC can trigger a copy | Matches the camera by name, or a drive by `DCIM\*_FUJI` |
| Depends on the folder you run it from; changes execution policy | Paths from the script's own location; no system changes |
| Slots hardcoded; config mostly ignored | Route by extension; small config, all of it used |
| No backup | Nightly OneDrive backup with a deleted-files folder |
