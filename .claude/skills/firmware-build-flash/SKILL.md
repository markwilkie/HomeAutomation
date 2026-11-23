---
name: firmware-build-flash
description: Build and/or flash an ESP-IDF firmware project in this repo (MiniSplit bridge, SpaV2 spa monitor and its test apps, ThreadRouter, DormerWindowState, MiniSplitIR) so it compiles only what changed and lands on the right board. Use for any request to build, compile, flash, upload or update firmware, or when a build is unexpectedly slow / recompiling everything.
---
Background and the reasons behind every rule: `tools/ESP_BUILDS.md`. Run all of
this from the repo root in PowerShell (the scripts are .ps1; Git Bash can't
activate ESP-IDF).

## Boards (all ESP32-C6, identical USB VID:PID -- the MAC is the only identity)
| Project | flash.ps1 | MAC |
|---|---|---|
| MiniSplit (bridge) | `MiniSplit\flash.ps1` | `58:8C:81:5D:89:04` |
| SpaV2/firmware, test_leds, test_phorp | `SpaV2\<dir>\flash.ps1` | `58:8C:81:5D:89:1C` |
| ThreadRouter | `ThreadRouter\flash.ps1` | `10:51:DB:0D:27:88` |
| DormerWindowState, MiniSplitIR | none -- build only; flashing is the owner's call | -- |

## Rules
- Only use the project's `build.ps1` / `flash.ps1` (they call `tools\esp-build.ps1` /
  `tools\flash-esp32.ps1`). Never run `idf.py set-target`, `idf.py fullclean`,
  `idf.py build` or `idf.py flash` on an existing project, never activate ESP-IDF
  by hand, and never edit `PROJECT_VER` or add a git hash to the version -- each
  of those forces a full rebuild or a wrong-install build.
- Never pipe a build through `Select-Object -First N` / `head`: it kills the build
  mid-run. Send output to a log file and read the tail.
- Matter builds can take minutes: run them with `run_in_background` and wait for
  the notification. Use `-Jobs 2` when the owner is away or it's unattended
  (memory: a full-parallel esp-matter build runs this PC out of memory).

## Steps
1. **Behavior change?** Bump the project's `version.txt` first -- it's the only
   version HA shows. (A bump re-runs CMake: ~5-8 min for Matter projects.)
2. **Build** (when asked to compile, or to check it compiles):
   `.\<project>\build.ps1 *> "$env:TEMP\<project>-build.log"` (add `-Jobs 2` if unattended).
   The last line says either `Up to date: <bin> <version> -- nothing compiled.` or
   `Built <bin> <version> from <commit>[ + uncommitted changes].`
3. **Flash** (only when asked, with the board on USB): `.\<project>\flash.ps1`.
   It refuses a board whose MAC doesn't match, builds only if something changed
   (seconds if not), then writes `build\flash_args` with esptool. It never erases
   flash, so Matter commissioning and NVS survive.
   - Owner says "don't recompile" / "flash what's built": `.\<project>\flash.ps1 -NoBuild`.
     If it warns `build/ is out of date`, tell the owner what's stale
     (step 5) instead of silently rebuilding.
   - No board found / MAC mismatch: stop and tell the owner which ports and MACs
     were seen. Don't retry on another port.
4. **Verify**: the flash output names the version it wrote. After the device
   rejoins, check HA shows that version as the device's Firmware (Matter
   SoftwareVersionString) or read it from the boot log. `build\build-info.txt`
   has the commit and any uncommitted changes the image was built from -- mention
   it if the build included uncommitted changes.
5. **Unexpected rebuild?** Expected costs: nothing changed = seconds; a source
   edit = 1-2 min; CMakeLists / sdkconfig / version.txt / component change =
   ~800 Matter+OpenThread recompiles (Espressif rewrites generated headers on
   every CMake run). If a build recompiles hundreds of files without one of those
   changes, find out why before accepting it:
   `.\tools\esp-build.ps1 -ProjectDir <project> -ActivateOnly; ninja -C <project>\build -n -d explain *> $env:TEMP\explain.log`
   and read the first `ninja explain:` lines. A `.git/...` input means something
   is tracking the repo's git state again -- fix it in
   `tools/cmake/idf_project_defaults.cmake`, don't work around it.

## New ESP-IDF project
In its `CMakeLists.txt`, before `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`:
`include(${CMAKE_CURRENT_LIST_DIR}/<..>/tools/cmake/idf_project_defaults.cmake)`.
Add `version.txt`, a `build.ps1` like `ThreadRouter\build.ps1`, and -- once it has
a dedicated board -- a `flash.ps1` with its MAC (add the MAC to ThreadRouter's
`$taken` list and the table above).
