# ESP-IDF firmware builds: compile only what changed

Every ESP-IDF project here (MiniSplit, SpaV2/firmware, SpaV2/test_leds,
SpaV2/test_phorp, ThreadRouter, DormerWindowState, MiniSplitIR,
MiniSplit/test_apps/*) builds and flashes the same way.

## Workflow (Windows PowerShell)

```powershell
.\MiniSplit\build.ps1              # build if anything changed; seconds ("no work to do") if not
.\MiniSplit\flash.ps1              # same build check, then write the board (MAC-checked)
.\MiniSplit\flash.ps1 -NoBuild     # write build/ exactly as it is; warns if it's out of date
.\MiniSplit\flash.ps1 -Jobs 2      # unattended: fewer parallel compiles
```

Projects without a flash.ps1 (DormerWindowState, MiniSplitIR) have no
MAC-checked board yet: build with build.ps1, then flash by hand.

- **Building** runs `ninja` with a job cap (default 4) — a full-parallel
  esp-matter build runs this PC out of memory. Nothing changed means nothing
  is compiled, so building before every flash costs seconds. (ninja never
  prints "no work to do" for these projects: ESP-IDF's bootloader sub-build
  and partition size check run every time. The scripts look at whether the
  app image changed instead.)
- **Flashing** writes `build/flash_args` (bootloader, partition table, app —
  the same files idf.py flash writes) with esptool. It never erases flash, so
  Matter commissioning and NVS survive.
- **Version** is the project's `version.txt` and nothing else; that's what HA
  shows as the device's Firmware. Bump it on any behavior change. Each build
  that compiled something writes `build/build-info.txt`: version, time, commit,
  and any uncommitted changes in the project.

A change to `CMakeLists.txt`, `sdkconfig`, `version.txt` or the components
still re-runs CMake, and for the Matter projects that costs ~800 recompiles
(5-8 min) — see cause 2 below. That's expected and rare.

## Why full rebuilds used to happen "for no reason" (found 2026-11-23)

1. **Any git commit re-ran CMake.** Two ways:
   - `espressif__esp_insights` (pulled in by esp-matter) calls `git_describe`
     on its own folder "just to automatically trigger re-runs of CMake".
     `managed_components/` sits inside this repo, so that tracked
     HomeAutomation's `.git/HEAD` and branch ref.
   - Projects that didn't set `PROJECT_VER` got it from ESP-IDF's own
     `git describe` of the repo, also tracking `.git`.
   MiniSplit's and SpaV2's version strings came from `git describe --dirty` as
   well, so the version changed on every commit too.
2. **A CMake run is expensive.** esp-matter writes ~16 generated headers
   (`CHIPBuildConfig.h`, `FirmwareBuildTime.h`, ...) with `file(WRITE)` on
   every run — new timestamps, same content — so ~490 Matter files recompile.
   ESP-IDF's OpenThread component puts the configure time in a compile flag
   (`OPENTHREAD_BUILD_DATETIME`), so ~290 OpenThread files recompile.
   Nov 21's MiniSplit build: 806 steps, 7.6 min, for a one-file change.
3. **`set-target` on every build.** The old interactive MiniSplit and Dormer
   build.ps1/build.sh ran `idf.py set-target` each time, which wipes `build/`
   and regenerates `sdkconfig`.
4. **Two ESP-IDF tool installs.** The EIM install (`C:\Espressif\tools`) and
   the plain-upstream one (`~\.espressif`) both use the v5.4.1 source at
   `C:\esp\v5.4.1\esp-idf` but have different compilers and Python. A `build/`
   made with one rebuilds from scratch (or fails) under the other.
   ThreadRouter, DormerWindowState, MiniSplitIR and ir_loopback were built with
   EIM; the rest with plain upstream.
5. **`idf.py flash` builds first.** The old flash script called it, so a
   flash after any commit meant cause 1 + 2.

## Fixes

- `tools/cmake/idf_project_defaults.cmake`, included by every project's
  CMakeLists.txt before ESP-IDF's: sets `PROJECT_VER` from `version.txt` and
  adds `version.txt` as a CMake dependency, and sets `GIT_CEILING_DIRECTORIES`
  to `managed_components/` so esp_insights' git lookup finds no repo (causes
  1). It's in CMake, so it holds for plain `idf.py build` too.
- `tools/esp-build.ps1`: activates whichever ESP-IDF install made this
  project's `build/` (cause 4; new projects get plain upstream); runs
  `set-target` / `reconfigure` only when `sdkconfig` / `build/` don't exist
  yet (cause 3); builds with `ninja -j`; writes `build/build-info.txt`.
- `tools/flash-esp32.ps1`: MAC check, then esp-build.ps1 (unless `-NoBuild`),
  then esptool `write_flash @flash_args` (cause 5).

Cause 2 is in Espressif's code (a managed component and ESP-IDF itself), so it
isn't patched — it only bites when CMake legitimately re-runs.

One-time cost: the first build of each project after this change re-runs CMake
(its CMakeLists.txt changed), so it's a long one. After that, commits elsewhere
cost nothing.

Check what a build would do without doing it:
`ninja -C <project>\build -n -d explain` (after `tools\esp-build.ps1 -ProjectDir <project> -ActivateOnly`).
