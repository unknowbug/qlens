# QLens

<img src="assets/QLens.png" width="64" align="left" style="margin-right:12px" />

> **QLens is an image viewer — and more importantly, a universal image-tagging protocol designed for the AI era.**

English · [中文](README.md)

See [CHANGELOG.md](CHANGELOG.md) for version history.

QLens is built around an open image-tagging protocol (`qltag.db`) that any
software, script, or AGENT can read and write:

| Component | Description |
|---|---|
| **QLens QuickView** | Native Win32 + D3D11 ultra-fast viewer — instant start, **HDR rendering**, WIC full-format + decoder plugins |
| **QLens Manager** | Qt file-manager style — thumbnail browsing, **tag management** (assign/color/combo filter), **QC inspection** (auto-detect overexposure/blur/color-cast) |
| **QLens MCP Server** | Opens your image library to AI clients (Claude / Cursor, etc.) — search/tag/statistics/batch analysis |

## Why QLens?

**Starting point 1: there's no "Picasa-like" viewer anymore.** Picasa was light, fast, clean — double-click and it shows, no distracting UI. But it's long discontinued. ACDSee and the like are feature-complete but heavy and slow to start; other viewers are either light with poor browsing experience, or look good but aren't light. QLens aims to bring back that light, clean viewing experience.

**Starting point 2: every viewer is missing something.** All viewers can open, zoom, and page through images — fast and pretty — but **none of them can answer**:

> "Of the 200 photos I took at the beach last year, which ones have closed eyes?"

QLens = viewing + an open tagging protocol, so your library can be searched and understood by AI.

```
┌─────────────────────────────────────────────────────────┐
│                  QLens Tag Protocol (qltag.db)          │
│   One SQLite DB per folder — readable/writable by any   │
│   software or AGENT                                     │
└───────────┬──────────────────────────┬──────────────────┘
            │                          │
   ┌────────▼────────┐       ┌─────────▼─────────┐
   │  QLens Manager  │       │ QLens QuickView   │
   │  (Qt file mgr)  │       │ (Win32 + D3D11)   │
   │  thumbs/tags/QC │       │ fast viewing + HDR │
   └────────┬────────┘       └─────────┬─────────┘
            │                          │
   ┌────────▼──────────────────────────▼─────────┐
   │            QLens MCP Server                  │
   │  opens the library to AI clients             │
   │  (Claude / Cursor …)                         │
   └──────────────────────────────────────────────┘
```

## Core: Tag Protocol

Everything revolves around `qltag.db` — one SQLite database per folder, keyed by pure filenames, naturally tree-shaped. Tags are classified by **how they are detected**:

- **QC tags (qc)**: reliably detectable without AI (overexposure / blur / color-cast) — one-click local CV detection
- **AI tags (ai)**: require AI detection (red-eye / closed eyes) — assigned by external AGENTs via MCP
- **Regular tags**: manual or arbitrary

See the [Tag Protocol Spec](docs/en/QLENS_TAG_PROTOCOL.md) ★

## Quick Start

```
bin/qlens_quickview.exe  double-click or drag an image to view (F=100%, S=fit window, wheel=paging)
bin/qlens_manager.exe    browse folders, tag images, run QC detection (double-click image to view)
```

**Typical workflow**: view in QuickView → double-click to open Manager → select images and tag them → hit "QC detection" to batch-apply QC tags → use combo/QC filters to find images → (optional) let AI read the library via MCP to add AI tags.

**System requirements**: Windows 10 1809+ (HDR features need an HDR display; HEIC/AVIF etc. need WIC extension or decoder plugin).

## HDR Test: Your Monitor's Real Brightness

Displays labeled HDR400/HDR600 usually deliver far less than the label (e.g. HDR400 often measures only 200–350 nits). Use QLens to find the real value:

1. Open [`testdata/hdr/hdr_range_test.jxr`](testdata/hdr/hdr_range_test.jxr) and press **F** for 100% view
2. The image shows 9 brightness blocks: **SDR 100 / 200 / HDR400 / HDR600 / HDR1000 / HDR1400 / HDR2000 / 4000 / 10000 nits**, each with a linear gradient and labeled peak
3. **The block where gray levels stop getting brighter (clamped)** is your monitor's actual HDR peak

QLens renders true HDR images (16-bit+) with **16F physical passthrough** (scRGB 1.0 = 80 nits) — no adaptive brightening/dimming, so what you see is the pixel's true physical luminance. This makes the image both a brightness meter and the most honest test of a monitor's HDR capability.

**Before testing**: enable HDR in Windows, set monitor brightness to max, disable eye-care/power-saving modes, and confirm HDR is active in Windows display settings.

## Documentation

- [Overview (detailed)](docs/en/01-overview.md) — design philosophy & the three components
- [QuickView Manual](docs/en/02-quickview.md) — shortcuts / HDR / plugins
- [Manager Manual](docs/en/03-manager.md) — file management / tags / QC / batch
- [Tag Protocol Spec](docs/en/QLENS_TAG_PROTOCOL.md) — `qltag.db` schema & classification ★
- [MCP Server](docs/en/05-mcp.md) — tools / setup / examples
- [Plugin Development](docs/en/06-plugin-dev.md) — decoder plugin API
- [Build & Release](docs/en/07-build.md) — dependencies / build / system requirements

## License

[LICENSE](LICENSE)
