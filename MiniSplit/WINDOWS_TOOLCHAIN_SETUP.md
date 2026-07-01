# Windows Toolchain Setup

This project pulls in Matter support as a managed component
(`espressif/esp_matter` in `main/idf_component.yml`), which builds fine on
native Windows — you do **not** need WSL2 for this project. WSL2 is only
required for the full esp-matter repo clone (which also builds the
host-side `chip-tool` / `chip-cert` / ZAP tools, and those do need a
Linux/macOS host). The managed component just gets cross-compiled for the
ESP32-C6 like any other ESP-IDF component, through the normal Windows
toolchain. (Note: `MATTER_SDK_SETUP.md` in this repo describes the
full-repo-clone option as well — that doc predates the project settling on
the managed-component approach actually declared in
`main/idf_component.yml`. Follow this doc for the path that matches what's
actually in this repo.)

## 1. Install ESP-IDF (Espressif Installation Manager)

Easiest: run `setup-toolchain.ps1` from this folder. It installs EIM,
installs ESP-IDF v5.4.1, enables Windows long-path support, and verifies
(and auto-repairs, if needed) the riscv32-esp-elf toolchain extraction —
see the note below on why that last step matters.

```powershell
.\setup-toolchain.ps1
```

Or do it by hand:

```powershell
winget install Espressif.EIM-CLI
eim install -i v5.4.1
```

(Or use the GUI installer instead: `winget install Espressif.EIM`, then
`New Installation → Custom Installation`, pick version `v5.4.1` to match
this project's recorded `sdkconfig` (ESP-IDF 5.4.1).)

This installs Python, the RISC-V/Xtensa toolchains, CMake, Ninja, and
creates a desktop shortcut / Start Menu entry — typically "ESP-IDF 5.4
CMD" — that opens a terminal with the IDF environment already sourced.

> **Known issue:** EIM's built-in zip extractor has been observed to fail
> silently on some Windows setups. The log shows
> `ZipArchive decompression failed: ... os error 3` followed by
> `Falling back to PowerShell approach... Decompression completed
> successfully` — but files like `cc1.exe` deep inside
> `riscv32-esp-elf\...\libexec\gcc\riscv32-esp-elf\<ver>\` never actually
> get written, so the first build fails with
> `riscv32-esp-elf-gcc.exe: fatal error: cannot execute 'cc1': CreateProcess:
> No such file or directory`. `setup-toolchain.ps1` checks for this and
> repairs it automatically by re-extracting the cached zip
> (`C:\Espressif\dist\riscv32-esp-elf-*.zip`) with `tar` instead, which
> doesn't have the same problem. If you installed by hand and hit this,
> the manual fix is:
> ```powershell
> Remove-Item -Recurse -Force "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119"
> New-Item -ItemType Directory -Force -Path "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119" | Out-Null
> tar -xf "C:\Espressif\dist\riscv32-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip" -C "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119"
> ```
> (adjust the version-specific paths to match what `eim install` printed).
> Enabling Windows long-path support first is also worth doing regardless:
> ```powershell
> New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
> ```

## 2. Build

Open the **ESP-IDF 5.4 CMD** shortcut EIM created (Start Menu / Desktop) —
it sources the right environment for you. `cd` into this project folder:

```powershell
cd C:\Users\Administrator\Documents\GitHub\HomeAutomation\MiniSplit
idf.py set-target esp32c6
idf.py build
```

> **Activating from a plain PowerShell window:** don't source
> `esp-idf\export.ps1` directly — an EIM-managed install's Python venv
> lives under `C:\Espressif\tools\python\<version>\venv`, not the path
> `export.ps1` expects (`%USERPROFILE%\.espressif\python_env\...`), so it
> fails with `ESP-IDF Python virtual environment "..." not found`. Instead
> dot-source the activation script EIM generated, whose path is recorded in
> `eim_idf.json`'s `activationScript` field (typically
> `C:\Espressif\tools\Microsoft.v<version>.PowerShell_profile.ps1`):
> ```powershell
> . C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1
> ```

The first build takes a while: the IDF Component Manager fetches
`espressif/esp_matter` and its dependencies from the component registry
into `managed_components/` automatically — no separate SDK clone needed.

Or use the helper script, which also prompts for menuconfig and flashing:

```powershell
.\build.ps1 -Target esp32c6 -Port COM6
```

## 3. Flash

```powershell
idf.py -p COM6 erase_flash   # first flash only, recommended
idf.py -p COM6 flash monitor
```

Swap `COM6` for whatever port Device Manager shows for the ESP32-C6's
USB-UART bridge. `Ctrl+]` exits the monitor.

## Troubleshooting

- **Build can't reach components.espressif.com** — check your network/VPN;
  the Component Manager needs outbound HTTPS on first build (results are
  cached locally afterward).
- **"called from a virtual environment, can not create a virtual
  environment again"** — run `pip install -r %IDF_PATH%\requirements.txt`
  inside the ESP-IDF terminal.
- **`cc1.exe`/`cc1plus.exe` missing, "CreateProcess: No such file or
  directory"** — see the known-issue box above.
- **Compile errors in `closure-control-server`/`closure-dimension-server`**
  (`error: no match for 'operator==' ... GenericOverallCurrentState` /
  `...TargetState` in `connectedhomeip/src/app/data-model/Nullable.h`) —
  a bug in the Matter 1.5 "Closure Control" cluster code bundled inside
  `espressif/esp_matter`, present in both 1.5.0 and 1.4.2 (pinning to an
  older esp_matter release does **not** avoid it — the vendored
  connectedhomeip copy has it either way; see
  [espressif/esp-matter#1604](https://github.com/espressif/esp-matter/issues/1604)).
  This project works around it by excluding those two clusters from the
  build entirely (`CONFIG_SUPPORT_CLOSURE_CONTROL_CLUSTER`/
  `CONFIG_SUPPORT_CLOSURE_DIMENSION_CLUSTER` set to `n` in the committed
  `sdkconfig`), which is safe since MiniSplit doesn't use them. If you see
  this error, those two options got re-enabled (e.g. via `idf.py
  menuconfig` + save, or a `sdkconfig` regenerated from scratch) — disable
  them again under the cluster-select menu.
- **Path too long / build fails oddly** — Espressif recommends keeping the
  ESP-IDF and project install paths under ~90 characters with no spaces or
  parentheses; if your project path is deeply nested, move it closer to
  the drive root, and make sure Windows long-path support is enabled (see
  above).
- **Build fails with errors that point into `connectedhomeip`/Matter host
  tooling** — that's a sign you've hit code that genuinely needs the
  Linux-only host build (rare for the managed-component path, but
  possible for advanced features like generating a Certification
  Declaration or running `chip-tool`). Fall back to WSL2 for just that
  step.

## References

- [ESP-IDF: Installation on Windows (EIM)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/windows-setup.html)
- [ESP-Matter: ESP Matter Component (experimental)](https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/developing.html#esp-matter-component-experimental)
