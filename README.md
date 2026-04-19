# Wallpaper Engine for KDE Plasma 6 (cachywp fork)

A wallpaper plugin that integrates [Wallpaper Engine](https://store.steampowered.com/app/431960/Wallpaper_Engine) wallpapers into KDE Plasma 6. Scene (2D Vulkan), Video (mpv or QtMultimedia), and Web backends.

> **Fork lineage:** this is a performance + hardening fork of [RainyPixel/wallpaper-engine-kde-plugin](https://github.com/RainyPixel/wallpaper-engine-kde-plugin), which itself forks [catsout/wallpaper-engine-kde-plugin](https://github.com/catsout/wallpaper-engine-kde-plugin).

## What this fork adds

### Performance

- **GPU zero-copy mpv path** — mpv renders directly into the QRhi-owned OpenGL FBO. No per-frame CPU alpha fix, no CPU texture upload.
- **SW fallback auto-detection** — falls back to the CPU render path only when the RHI backend is not OpenGL.
- **Frame pacing** — `fpsLimit` throttle in the mpv redraw callback; scene backend honours a user FPS slider.
- **Resolution scaling (both backends)** — render at 0.25–1.0× output size and bilinear-upscale. Slashes GPU load on integrated GPUs at minimal visual cost.
- **Quality tier presets** — `Low Power / Balanced / High / Native` auto-set fps + both render scales; touching any individual slider drops the tier back to `Custom`.
- **Per-backend metrics** — `debugMetrics()` QML invokable + atomic counters (`frameCount`, `droppedFrames`, `dirtyUpdates`, `lastRenderMs`, etc). Toggle-able debug HUD.
- **Structured init logging** — `wekde.mpv` and `wekde.scene` logging categories report render path, driver, GPU name, target size on startup.
- **Optional backend builds** — `-DBUILD_MPV=OFF` / `-DBUILD_SCENE=OFF` CMake flags. Build just what you need.

### Safety / crash hardening

- Try/catch safety net around scene parse, draw, set-scene, and Vulkan-init handlers.
- `nlohmann::json` `.at()` calls replaced with `.contains()` + `.find()` fallbacks throughout the scene parser.
- Bounds-checked `SpriteAnimation`, camera lookups, shader alias substrings, `CustomShaderPass` render-target lookups, particle vector indexing, render-graph null-pass filtering.
- Pre-render package validation: `MainHandler::loadScene` rejects packages missing `sceneGraph` or required `global` / `global_perspective` cameras, with a clear log line, instead of crashing during draw.
- Schema/version probe in `WPSceneParser::Parse` that logs `scene.json` version and warns on unknown top-level keys (format-drift early warning).

### Benchmarking

- `bench/` — scenario runner (`bench.sh` + `scenarios.json`) with thresholds per wallpaper type.

---

## Install

You have two reasonable install paths:

| Path                            | Sudo needed? | Good for                                                   |
| ------------------------------- | ------------ | ---------------------------------------------------------- |
| **User-local** (`$HOME/.local`) | No           | Daily use on a single-user machine, avoids touching `/usr` |
| **System-wide** (`/usr`)        | Yes          | Multi-user machines, packaging                             |

Both are documented below. User-local is usually the right call.

---

## Build from source

### 1. Install dependencies

**Arch / CachyOS:**

```sh
sudo pacman -S --needed extra-cmake-modules libplasma gst-libav ninja \
    base-devel mpv qt6-declarative qt6-websockets qt6-webchannel \
    vulkan-headers cmake lz4
```

**Fedora:**

```sh
# RPM Fusion (required for ffmpeg/mpv)
sudo dnf install -y \
    https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
    https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
sudo dnf swap -y ffmpeg-free ffmpeg --allowerasing
sudo dnf install -y ffmpeg-devel --allowerasing

sudo dnf install -y vulkan-headers plasma-workspace-devel kf6-plasma-devel \
    kf6-kcoreaddons-devel kf6-kpackage-devel gstreamer1-libav \
    lz4-devel mpv-libs-devel qt6-qtbase-private-devel libplasma-devel \
    qt6-qtwebchannel-devel qt6-qtwebsockets-devel cmake extra-cmake-modules
```

You also need a working [Vulkan driver](https://wiki.archlinux.org/title/Vulkan#Installation). AMD users: prefer RADV. NVIDIA users: the proprietary driver is fine.

### 2. Clone and fetch submodules

```sh
git clone <your-fork-url> wallpaper-engine-kde-plugin
cd wallpaper-engine-kde-plugin
git submodule update --init --force --recursive
```

### 3. Configure and build

**User-local (no sudo):**

```sh
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j$(nproc)
```

**System-wide:**

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

#### Optional backend flags

Disable a backend at build time if you don't need it:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_MPV=OFF      # scene+web only
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_SCENE=OFF    # mpv+web only
```

### 4. Install

**User-local:**

```sh
cmake --install build
```

This drops the QML wallpaper package in `~/.local/share/plasma/wallpapers/` and the native QML plugin in `~/.local/lib/qml/`. **One extra step** is needed so Qt finds the user-local QML plugin:

```sh
# Tell the systemd user session where to find the QML plugin
systemctl --user set-environment \
    QML2_IMPORT_PATH=$HOME/.local/lib/qml \
    QML_IMPORT_PATH=$HOME/.local/lib/qml

# Persist it across reboots via plasma-workspace env hook
mkdir -p ~/.config/plasma-workspace/env
cat > ~/.config/plasma-workspace/env/qml-import-path.sh <<'EOF'
#!/bin/sh
export QML2_IMPORT_PATH="$HOME/.local/lib/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
export QML_IMPORT_PATH="$HOME/.local/lib/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}"
EOF
chmod +x ~/.config/plasma-workspace/env/qml-import-path.sh
```

**System-wide:**

```sh
sudo cmake --install build
```

### 5. Restart plasmashell

```sh
systemctl --user restart plasma-plasmashell.service
```

Verify the package is registered:

```sh
kpackagetool6 --list --type Plasma/Wallpaper | grep wallpaperEngineKde
```

---

## Activate in Plasma

1. Right-click the desktop → **Configure Desktop and Wallpaper…**
2. **Wallpaper Type** dropdown → **Wallpaper Engine for KDE**
3. **Steam Library** → folder containing `steamapps` (usually `~/.local/share/Steam`). Wallpaper Engine must be installed inside that library.
4. Subscribed Workshop wallpapers appear in the list. Pick one → **Apply**.

---

## Settings you'll care about (new in this fork)

Open the plugin config page (Configure Desktop and Wallpaper → the plugin's settings tab). In addition to the upstream controls:

| Setting                               | What it does                                                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| **Quality Preset**                    | `Low Power / Balanced / High / Native` — one-shot preset of fps + render scale. `Custom` is re-selected automatically when you move a slider. |
| **Fps** (Scene)                       | Frame cap for the Vulkan scene renderer.                                                                                                      |
| **Render Scale** (Mpv)                | 0.25–1.0. Renders mpv at a fraction of output size and upscales. Big win on integrated GPUs.                                                  |
| **Render Scale** (Scene)              | Same idea for the Vulkan scene backend. Drops + rebuilds the swapchain on change.                                                             |
| **Video Backend**                     | `QtMultimedia` or `Mpv`. Mpv is faster and more reliable; QtMultimedia is pure Qt.                                                            |
| **Hardware Decode**                   | `Auto` (VAAPI/NVDEC) or `Software` for mpv.                                                                                                   |
| **Show Mpv Stats / Show Scene Stats** | Toggles an on-demand debug HUD overlay showing `renderPath`, frame time, counts, etc.                                                         |

---

## Tuning guide

- **Integrated GPU or laptop on battery** → Quality Preset = **Low Power** (10 fps, 0.50× scale).
- **Mid-range desktop** → **Balanced** (15 fps, 0.75× scale).
- **Gaming rig** → **High** or **Native**.
- If CPU is high on Mpv path, check the Mpv stats HUD. If `renderPath` shows `sw-fallback` instead of `gpu-gl`, your Qt is using a non-OpenGL RHI backend. Force it with:
  ```sh
  systemctl --user set-environment QSG_RHI_BACKEND=opengl
  systemctl --user restart plasma-plasmashell.service
  ```
- If the scene backend is the bottleneck (complex wallpapers), drop **Fps** and/or **Render Scale** (Scene).

---

## Requirements

- KDE Plasma 6
- Qt 6.7+ (requires `QQuickRhiItem`)
- Vulkan 1.1+
- C++20 (GCC 10+ / Clang 12+)
- mpv 0.35+ with `libmpv` dev headers
- A working Vulkan driver

---

## Troubleshooting

### `module "com.github.catsout.wallpaperEngineKde" is not installed`

Qt can't find the native plugin. Either:

- User-local install: make sure `QML2_IMPORT_PATH` is set in the **systemd user session**, not just your shell — see the install step above. Verify with:
  ```sh
  cat /proc/$(pgrep -f plasmashell | head -1)/environ | tr '\0' '\n' | grep QML
  ```
- System-wide install: check `/usr/lib/qt6/qml/com/github/catsout/wallpaperEngineKde/libWallpaperEngineKde.so` exists.

### Wallpaper list is empty

Check the **Steam Library** path points to the directory containing `steamapps/`, not `steamapps/` itself. Wallpaper Engine must actually be installed (`steamapps/common/wallpaper_engine/`).

### Scene wallpaper crashes plasmashell

The hardening in this fork should prevent most of these, but if one still crashes:

1. `journalctl --user -u plasma-plasmashell.service -b | grep -iE "wekde|vulkan"` — find the failing wallpaper + reason.
2. Remove `WallpaperSource=` from `~/.config/plasma-org.kde.plasma.desktop-appletsrc` to break the boot loop.
3. Restart plasmashell.

### Mpv path shows `sw-fallback` instead of `gpu-gl`

Your RHI backend isn't OpenGL. Force it (see Tuning guide above) or accept the CPU cost and drop `Render Scale` to compensate.

### Per-frame logs flooding journal

Only debug-level logs (`_Q_DEBUG()`) are noisy. Filter with:

```sh
journalctl --user -u plasma-plasmashell.service -f | grep -v DEBUG
```

---

## Debugging

```sh
# Tail plasmashell logs
journalctl --user -u plasma-plasmashell.service -f

# Replace running plasmashell in a terminal (stdout visible)
plasmashell --replace

# Force Vulkan validation layers for the scene backend
systemctl --user set-environment VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
systemctl --user restart plasma-plasmashell.service
```

There is also a standalone scene viewer for isolated debugging:

```sh
cd src/backend_scene/standalone_view/
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/sceneviewer --valid-layer \
    <steamlibrary>/steamapps/common/wallpaper_engine/assets \
    <steamlibrary>/steamapps/workshop/content/431960/<id>/scene.pkg
```

---

## Benchmarking

```sh
cd bench/
./bench.sh                    # runs all scenarios in scenarios.json
./bench.sh video-1080p30      # runs a single scenario
```

Each scenario has CPU / frame-time / drop-rate thresholds. See `bench/README.md`.

---

## Uninstall

**User-local:**

```sh
# Files installed by cmake --install
xargs rm -f < build/install_manifest.txt
kpackagetool6 -t Plasma/Wallpaper -r com.github.catsout.wallpaperEngineKde
rm -f ~/.config/plasma-workspace/env/qml-import-path.sh
systemctl --user unset-environment QML2_IMPORT_PATH QML_IMPORT_PATH
systemctl --user restart plasma-plasmashell.service
```

**System-wide:**

```sh
sudo xargs rm -f < build/install_manifest.txt
kpackagetool6 -t Plasma/Wallpaper -r com.github.catsout.wallpaperEngineKde
```

---

## Known issues (upstream)

- Mouse long-press (desktop panel edit mode) is broken while a live wallpaper is active.
- Screen Locking is not supported.
- WebGL inside web wallpapers may be buggy.

---

## Architecture (short version)

- `src/backend_mpv/` — `QQuickRhiItem`-based mpv backend. Dual render paths:
  - **GL path** (`renderGL`): mpv renders into a QRhi-owned FBO using `MPV_RENDER_API_TYPE_OPENGL`. Zero CPU copy.
  - **SW path** (`renderSW`): mpv renders into an aligned CPU buffer, alpha-fixed, uploaded via `QRhiResourceUpdateBatch`. Used only when RHI backend is not OpenGL.
- `src/backend_scene/` — Vulkan 2D scene renderer.
  - Renders into a Vulkan image, exports as dmabuf, imports as a GL texture, wraps in `QSGSimpleTextureNode`. Zero CPU copy.
  - Auto render graph with pass dependency resolution.
- `plugin/contents/ui/` — QML settings UI (`SettingPage.qml`, `main.qml`, backend wrappers).
- `src/plugin.cpp` — QML plugin entry point, gated by `BUILD_MPV` / `BUILD_SCENE`.

---

## Acknowledgments

- RainyPixel fork — [RainyPixel/wallpaper-engine-kde-plugin](https://github.com/RainyPixel/wallpaper-engine-kde-plugin)
- Original project — [catsout/wallpaper-engine-kde-plugin](https://github.com/catsout/wallpaper-engine-kde-plugin)
- [RePKG](https://github.com/notscuffed/repkg) — Wallpaper Engine package format reference
- All open-source libraries bundled under `src/backend_scene/third_party/`
