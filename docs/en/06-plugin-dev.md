# QLens Plugin Development Guide

QLens's decoder plugin system: **when new image formats appear, support them via plugin DLLs** — no changes to QLens itself. Currently shipped: HEIC/AVIF (libheif), SVG.

## What a Plugin Is

- A **DLL** placed in `plugins/` next to the exe, auto-loaded at startup.
- Each plugin registers **one or more extensions** (e.g. `heic|heif|avif|heifs`).
- Decode order: **plugins first** (extension + file-header signature) → WIC fallback.
- QuickView and Manager (via qlens_core) share the same plugin interface.

## Interface

### Entry point

```c
extern "C" __declspec(dllexport) void qlens_plugin_entry(RegApi *api);
```

The loader finds it via `GetProcAddress(h, "qlens_plugin_entry")` and passes `RegApi`:

```c
typedef struct RegApi {
    void (*regDecoder)(const DecodePlugin *plugin);
} RegApi;
```

### Registration descriptor

```c
typedef struct DecodePlugin {
    const char *ext;        // lowercase extensions, | separated: "heic|heif|avif"
    const char *signature;  // file-header signature (hex bytes), e.g. "3C3F786D6C" (= "<?xm"); empty = extension only
    int  (*query)(const char *path, DecodeInfo *info);   // query: format/size/frames/metadata
    int  (*decode)(const char *path, int frame, int targetW, int targetH, ImageBuffer *out);  // decode
} DecodePlugin;
```

### Query result

```c
typedef struct DecodeInfo {
    int width, height;        // suggested size (EXIF-rotated)
    int frames;               // frame count (GIF etc.)
    int vector;               // is vector (SVG)
    int hasAlpha;             // has alpha channel
    int exifRot;              // EXIF Orientation
    int hasICC;               // has ICC profile
    int peakNit;              // peak luminance (HDR)
    int transfer;             // transfer function: sRGB/PQ/HLG/linear
    int error;                // error code: OK/NOT_SUPPORTED/CORRUPT/IO_ERROR
    // ... (see plugins.h)
} DecodeInfo;
```

### Decode output

```c
typedef struct ImageBuffer {
    void  *pixels;            // pixel data (BGRA8 or RGBA16F)
    int    width, height;
    int    stride;            // bytes per row
    int    format;            // pixel format enum
    // ...
} ImageBuffer;
```

## Complete Example (HEIC plugin skeleton)

```c
#include "plugins.h"

static int heic_query(const char *path, DecodeInfo *info) { /* libheif query */ }
static int heic_decode(const char *path, int frame, int tw, int th, ImageBuffer *out) {
    /* libheif decode → fill out as BGRA8/RGBA16F */
}

static DecodePlugin g_heic = {
    "heic|heif|avif|heifs",   // register 4 formats
    "",                        // no header signature (HEIC has its own container header; extension-based)
    heic_query, heic_decode
};

extern "C" __declspec(dllexport) void qlens_plugin_entry(RegApi *api)
{
    api->regDecoder(&g_heic);
}
```

## Build Requirements

- **x64** Windows DLL (same arch as the QLens exe).
- Exported symbol must **match exactly** (`__declspec(dllexport)` + C linkage `extern "C"`).
- Load third-party libs (e.g. libheif) dynamically with `LoadLibrary` + `GetProcAddress` — **don't statically link** — small release, controlled dependencies.

## Pitfalls (learned the hard way)

1. **Struct returns (x64 ABI)**: functions returning structs use a hidden pointer — the function pointer declaration must match the struct exactly; a wrong declaration (e.g. returning `int`) causes **stack corruption crashes**.
2. **Enums**: third-party enum values (e.g. `heif_colorspace_RGB=1`, `heif_chroma_interleaved_RGBA=11`) must be exact; wrong values return `Unsupported_feature`.
3. **Size APIs**: some libs return -1 for interleaved images on width queries — use the handle-level API instead.
4. **Header signature**: `signature` lets content-based recognition work — renamed files open too.

## Format Icon Convention (plugins)

QLens's format-icon protocol (Manager thumbnail grid):

1. **`icons/<EXT>.ico` first** (folder next to the exe; `EXT` uppercase, e.g. `icons/JPG.ico`) — **plugin authors just drop an icon here; Manager picks it up automatically, no code changes / recompilation**
2. Fallback: icons compiled into the exe via qrc (built-in formats)

- **Built-in formats**: icons compiled into the exe (qrc)
- **Plugin formats**: ship `icons/<EXT>.ico` alongside (under the QLens install `icons/`) — e.g. `.foo` → `icons/FOO.ico` (16/32/48/256 multi-size ICO recommended)

**Optional (more complete)**: embed an ICON resource in the plugin DLL (resource ID 0 = primary format icon) — Windows Explorer `DefaultIcon` can point at the plugin DLL (`"C:\...\plugins\foo.dll",0`), bundling file-type icon with the decoder.

## Testing

- Drop the DLL into `build-qv/Release/plugins/` (next to QuickView), launch QuickView, open a file of that format.
- Manager (qlens_core) loads from the same `plugins/` dir — thumbnails and viewer both pick it up.
