# QLens Build & Release

## System Requirements (runtime)

- **Windows 10 1809+** (QuickView uses Per-Monitor V2 DPI; pre-1809 unsupported)
- HDR features: HDR display + Windows HDR toggle (`Settings → System → Display → HDR`)
- HEIC/AVIF: heic plugin (libheif) in `plugins/` or the system WIC HEIF extension
- RAW (CR2/CR3/NEF/ARW/DNG/RW2): built into Manager (LibRaw)

## Dependencies

| dependency | purpose | source |
|---|---|---|
| Qt 6 (Core/Gui/Widgets/Sql/Concurrent) | Manager + qlens_core | system install (CMake find_package) |
| LibRaw | RAW decode | `find_package(libraw)` |
| lcms2 | color management | `find_package(lcms)` |
| Windows SDK | WIC / D3D11 / DXGI / shlwapi | system |
| Python 3 | MCP Server + import/export CLI | optional (MCP/import-export only) |

## Building

### Manager + qlens_core (Qt part)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:\Qt\6.11.1\msvc2022_64
cmake --build build --config Debug --target qlens_manager
```

Targets:
- `qlens_core` (SHARED) — decode/plugin/i18n shared library
- `qlens_manager` (WIN32 exe) — the manager

### QuickView (native Win32, standalone build)

```bat
cmake -S src/quickview -B build-qv -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:\Qt\6.11.1\msvc2022_64
cmake --build build-qv --config Release
```

(QuickView uses WIC/D3D11 — no real Qt dependency; separate dir keeps the lightweight release small.)

## Release Layout

```
qlens/
├── qlens_quickview.exe      # fast viewer (native)
├── qlens_manager.exe        # manager (Qt)
├── qlens_core.dll           # shared lib (Manager depends)
├── Qt6*.dll                 # Qt runtime (Manager; deploy with windeployqt)
├── opengl32sw.dll           # Qt software-rendering fallback (optional)
├── plugins/                 # decoder plugins (heic/svg...)
├── mcp/                     # MCP Server (server.py + qlens_lib.py + qc_detect.py)
├── docs/                    # documentation (incl. QLENS_TAG_PROTOCOL.md)
├── language/zh/qlens_quickview.po
├── language/en/qlens_quickview.po
├── language/zh/qlens_manager.po
├── language/en/qlens_manager.po
├── qlens_config.ini         # optional: language=zh/en (portable mode; else %APPDATA%/QLens/)
└── docs/                    # documentation
```

- **Qt DLL deploy**: `windeployqt qlens_manager.exe` (auto-copies Qt6Core/Gui/Widgets/Sql/Concurrent + platform plugins).
- **QuickView needs no Qt** — release can ship just qlens_quickview.exe + plugins/ + language/.
- **Portable mode**: config writes next to the exe if writable, else `%APPDATA%/QLens/`.
- **Crash log**: `%APPDATA%/QLens/crash.log` (QuickView).

## Language (i18n)

- Files: `language/<Lang>/<app>.po` (`qlens_quickview.po` / `qlens_manager.po`)
- Config: `qlens_config.ini` `[General] language=zh|en` (Manager「Settings → Language」writes it)
- Defaults to system language, falls back to Chinese
- New language: copy a po template, translate, drop into `language/<Lang>/`

## Uninstall Cleanup (file associations)

The 「Settings → Register as default viewer」writes **HKCU user-level** keys; delete on uninstall:

\\nHKCU\\Software\\Classes\\QLensQuickView                    (whole ProgID tree)
HKCU\\Software\\Classes\\Applications\\qlens_quickview.exe  (whole tree)
HKCU\\Software\\Classes\\.jpg\\OpenWithProgids\\QLensQuickView  (per-extension value)
  ...(.jpeg .png .gif .bmp .webp .heic .heif .avif .jxr .wdp .svg .tif .tiff likewise)
\\n
Note: **delete only the QLensQuickView value** — not the OpenWithProgids key itself (other programs may have entries). Call SHChangeNotify(SHCNE_ASSOCCHANGED) after removal.

## Plugins

- Decoder plugins go in `plugins/` (DLLs) — see [Plugin Development](06-plugin-dev.md)
- Shipped: HEIC/AVIF (libheif), SVG
