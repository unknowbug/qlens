# QLens QuickView Manual

Native Win32 + D3D11 ultra-fast image viewer. **No Qt — fast start, low memory.**

## Launching

| method | notes |
|---|---|
| Double-click an image | after file association (Manager「Settings → Register as default viewer」or auto-registered on first launch) |
| Drag an image | drag a file from Explorer onto the window |
| Command line | `qlens_quickview.exe <image path>` (first non-`-` arg); `--monitor N` picks the display index |

- Double-clicking an image launches the same-directory `qlens_manager.exe` to open the folder, then closes.
- No args: opens an empty window + registers associations + loads plugins + language init.

## Keyboard Shortcuts

| key | action |
|---|---|
| `Esc` | quit |
| `←` / `↑` | previous image |
| `→` / `↓` / `Space` | next image |
| `F` | 100% original size |
| `S` | fit window |
| `Ctrl+wheel` | zoom (×1.25 / ×0.8, min 0.05) |
| `Ctrl+C` | copy current image (path + file + bitmap, 3-in-1 clipboard) |
| `Q` / `E` | rotate left / right 90° |
| `Del` | delete current image to Recycle Bin |
| `Ctrl+Shift+S` | save as (PNG/JPEG) |
| `F12` | debug overlay (FMT/IMG/WIN/ZOOM/ROT/PAN/MON/HDR/PEAK; also enables window dragging, disabled normally) |

## Mouse

| action | function |
|---|---|
| wheel (no Ctrl) | page: up = previous, down = next |
| `Ctrl+wheel` | zoom |
| drag file in | open new image |
| double-click | open Manager (same dir) |
| left-drag (when zoomed) | pan canvas |
| click bottom thumbnail | jump to that image |
| right-click | menu: rotate left/right, zoom in/out, copy, save as, delete |

## Screen Buttons

- **Top-right**: close button (always visible)
- **Bottom-center**: previous / next / Manager buttons — shown on mouse move, auto-hide after 2s idle

## Supported Formats

- **Built-in filter**: `.jpg .jpeg .png .webp .bmp .gif .tif .tiff .svg .heic .heif .avif .jxr .wdp`
- **Decoder plugins** (`plugins/` next to the exe):
  - HEIC: `heic/heif/avif/heifs` (dynamically loads libheif, 8/16bit)
  - SVG: `svg` (with file-header signature detection)
- **Decode order**: plugins first (extension + header signature) → WIC fallback; high-bit-depth sources → RGBA16F

## HDR Capabilities

| scenario | behavior |
|---|---|
| true HDR (16bit+) on HDR display | **physical passthrough**: scRGB linear, highlights >1.0 (>80 nit real), no adaptive tone map |
| SDR image on HDR display | **SDR→HDR enhancement**: sRGB→linear→×peakNit→PQ; soft-clamp highlights >0.7, bright images lower gain by highlight ratio |
| true HDR on SDR display | **HDR→SDR fallback**: Reinhard tone map (v/(1+v)) to 8bit, avoids black screen |
| UI (thumb strip/buttons) | stays at SDR brightness |

- HDR detection: DXGI enumerates displays — HDR10 color space (G2084/P2020) or MaxLuminance>100 → HDR display.
- Display color space: scRGB (G10) physical linear.

## Thumbnail Strip

- Dark strip at bottom; current image blue-highlighted and brightened, others dimmed
- Auto-center scrolling; re-centers on resize
- Async generation on background thread (below-normal priority), spiral order (current→right→left)
- Only draws complete thumbnails (no half-clipped ones)
- FP16 half-precision buffer in HDR mode

## Misc

- **GIF animation**: auto-plays
- **EXIF Orientation**: auto-rotated
- **Crash log**: `%APPDATA%/QLens/crash.log`
- **Per-Monitor V2 DPI** aware
- **Language**: `qlens_config.ini` `language` → `language/<Lang>.po` (default Chinese)
- **Associations**: writes HKCU `OpenWithProgids` on launch: `.jpg .jpeg .png .webp .bmp .gif .svg`

## System Requirements

- Windows 10 1809+ (Per-Monitor V2 DPI requires 1809+)
- HDR features need an HDR display (Windows HDR toggle on)
- HEIC/AVIF: needs the heic plugin (libheif) in `plugins/` or the system WIC HEIF extension
