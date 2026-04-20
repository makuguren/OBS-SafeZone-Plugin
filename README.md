# SafeZone Overlay for OBS Studio

A lightweight OBS Studio plugin that overlays a safe-zone guide image on top of the main preview, so you can keep important on-screen content inside the action-safe / title-safe region while you compose your scene.

The overlay is drawn **only on the preview** inside OBS Studio. It never appears in recordings, streams, projectors, or any output — it lives purely in the editor as a compositional aid.

- **Name:** SafeZone Overlay
- **Version:** 1.0.0
- **Author:** Dcoderz Philippines
- **Website:** <https://dcoderz.site>
- **Contact:** mail@dcoderz.site

## Features

- Draws a safe-zone PNG (bundled `safezone-overlay.png`) on top of the OBS preview.
- Aligns pixel-perfectly with the canvas — tracks preview resizing, aspect ratio, **Ctrl+Scroll zoom**, and **Spacebar pan** automatically.
- Adds a dockable **SafeZone Overlay** panel with a single Enable/Disable toggle. Pin it, float it, tab it with other docks, or hide it — Qt remembers the layout between sessions.
- **Persists across restarts.** If you close OBS with the overlay turned on, it comes back on the next launch (waits for the preview to be ready before re-enabling).
- Output-safe: renders via a preview-only draw callback, never through a source or filter, so it can't leak into your stream/recording.
- Crash-safe shutdown: disables cleanly whether OBS is closed via the window chrome, File → Exit, or while the dock is floating.

## Requirements

- OBS Studio **31.x** (this release targets 31.1.1).
- Windows x64 (the build script and installer target Windows; the source builds on macOS too if you rebuild with the macOS preset).

> The plugin reads a couple of private members out of OBS's `OBSBasicPreview` (preview display pointer, scroll offset, zoom amount) by matching its C++ layout. That layout is locked to OBS 31.x. If you try to load this DLL against a different major OBS version, the overlay will still be safe (it sanity-checks the values and falls back to a centered fit), but you should rebuild against the matching OBS sources.

## Installation

### Option A: Prebuilt DLL (recommended)

1. Close OBS Studio.
2. Copy `makuguren-obs-safezone-overlay.dll` into:

   ```text
   C:\Program Files\obs-studio\obs-plugins\64bit\
   ```

3. Copy the `data\safezone-overlay.png` image into:

   ```text
   C:\Program Files\obs-studio\data\obs-plugins\makuguren-obs-safezone-overlay\
   ```

   (Create the folder if it doesn't exist.)

4. Launch OBS Studio. Open **Docks → SafeZone Overlay**, pin it where you want it, and click **Enable SafeZone Overlay**.

### Option B: Installer

If an `obs-safezone-overlay-1.0.0-windows-x64-Installer.exe` is provided for your release, run it with OBS closed and it will place the files in the correct locations.

## Usage

1. Open the **SafeZone Overlay** dock (View → Docks if it's hidden).
2. Click **Enable SafeZone Overlay**. The safe-zone PNG is drawn on top of the preview, locked to the canvas coordinate system.
3. Zoom the preview with **Ctrl + mouse wheel** or pan with **Space + drag** — the overlay follows the canvas, not the window.
4. Click again to disable, or leave it on; it will auto-restore when you reopen OBS.

### Replacing the overlay image

The plugin looks for `safezone-overlay.{png,jpg,jpeg,bmp}` in its `data/` folder. To use your own guide, replace `data/safezone-overlay.png` (keep the file name) with any image you like — the overlay is drawn stretched to the canvas dimensions, so author your guide at your typical canvas aspect (e.g. 1920×1080 or 1080×1920).

## Building from source

### Prerequisites

- **Visual Studio 2022** with the C++ desktop workload and **Windows SDK 10.0.22621.0** or newer.
- **CMake 3.28+**.
- **Git** (the dependency fetch step downloads OBS sources and prebuilt deps from the `buildspec.json` hashes).
- **PowerShell 7+** (for the `.github/scripts/Build-Windows.ps1` helper).

### Configure & build (Windows x64)

From a PowerShell prompt in the repo root:

```powershell
# One-time: fetch OBS sources, obs-deps, Qt6, into .deps\
.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo

# Subsequent incremental builds:
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Outputs land in:

```text
build_x64\RelWithDebInfo\makuguren-obs-safezone-overlay.dll
build_x64\RelWithDebInfo\makuguren-obs-safezone-overlay.pdb
```

### Install the freshly built DLL into OBS

From an **administrator** PowerShell:

```powershell
Copy-Item build_x64\RelWithDebInfo\makuguren-obs-safezone-overlay.dll `
    "C:\Program Files\obs-studio\obs-plugins\64bit\" -Force

# Only needed the first time you install, or after changing the PNG:
New-Item -ItemType Directory -Force `
    "C:\Program Files\obs-studio\data\obs-plugins\makuguren-obs-safezone-overlay" | Out-Null
Copy-Item data\safezone-overlay.png `
    "C:\Program Files\obs-studio\data\obs-plugins\makuguren-obs-safezone-overlay\" -Force
```

### Package a release ZIP

To produce a redistributable ZIP (`obs-safezone-overlay-<version>-windows-x64.zip`) containing the plugin DLL, PDB, `data/` folder, `README.md`, and `LICENSE`, run the following from the repo root in PowerShell:

```powershell
cmake --build --preset windows-x64 --config RelWithDebInfo --parallel
if (Test-Path release) { Remove-Item -Recurse -Force release }
cmake --install build_x64 --prefix "$PWD\release\RelWithDebInfo" --config RelWithDebInfo
Copy-Item README.md, LICENSE -Destination release\RelWithDebInfo\obs-safezone-overlay -Force
Compress-Archive -Path "$PWD\release\RelWithDebInfo\*" `
    -DestinationPath "$PWD\release\obs-safezone-overlay-1.0.0-windows-x64.zip" `
    -CompressionLevel Optimal -Force
```

This produces the following layout inside the ZIP, ready to drop into an OBS install:

```text
obs-safezone-overlay/
  bin/64bit/obs-safezone-overlay.dll
  bin/64bit/obs-safezone-overlay.pdb
  data/safezone-overlay.png
  README.md
  LICENSE
```

The finished archive lands at `release\obs-safezone-overlay-1.0.0-windows-x64.zip`.

## Project layout

```text
src/
  plugin-main.cpp              Module entry points; dock registration (runs from
                               obs_module_post_load so Qt's restoreState picks
                               up the dock on startup).
  safezone-overlay.hpp/.cpp    Draw callback + preview transform matching
                               (OBSBasicPreview layout shim, pan/zoom tracking).
  safezone-overlay-dock.hpp/.cpp
                               Dock UI (Enable/Disable button), persistence,
                               and startup auto-restore polling.
data/
  safezone-overlay.png         The overlay image the plugin draws.
CMakeLists.txt                 Plugin build definition.
CMakePresets.json              CMake configure presets (windows-x64, macos, ...).
buildspec.json                 Pinned OBS / obs-deps / Qt6 versions + hashes.
```

## Configuration storage

The plugin persists a single flag in OBS Studio's per-user config at:

```text
%APPDATA%\obs-studio\user.ini
```

Under the section:

```ini
[SafeZoneOverlay]
Enabled=true
```

This is the same config file OBS uses for dock layout, confirm-on-exit, etc. The flag is written on every toggle and read on startup; the dock itself (pinned position, floating geometry, tab group) is restored by OBS's own `restoreState()` using the dock's object id `makuguren_safezone_overlay_dock`.

## Troubleshooting

- **The overlay doesn't reappear on restart.**
  Open `%APPDATA%\obs-studio\user.ini` and confirm the `[SafeZoneOverlay] Enabled=true` line is present. If it is and the overlay still doesn't start within ~10 seconds of OBS finishing loading, check the latest file in `%APPDATA%\obs-studio\logs\` for lines starting with `[makuguren-obs-safezone-overlay]`.

- **The dock vanishes from `View → Docks` after a restart.**
  This plugin registers its dock from `obs_module_post_load()`, which runs *before* OBS calls `restoreState()`, so the dock is always eligible for layout restore. If you still see this, make sure only one copy of the DLL is installed (a stale copy in `%APPDATA%\obs-studio\plugins\` can shadow the one in `Program Files`).

- **`Failed to load 'en-US' text for module: 'makuguren-obs-safezone-overlay.dll'`**
  Harmless — the plugin doesn't ship a locale file. OBS logs this for every module that doesn't provide one.

- **"preview widget not found" or "preview display is null" in the log.**
  Expected briefly during startup (the preview's `obs_display_t` is created lazily by Qt paint/expose events). The plugin retries for ~10 seconds before giving up. If it gives up, toggle the dock button off and on once to retry.

## License

This plugin is distributed under the **GNU General Public License v2.0 or later**. See `LICENSE` for the full text.
