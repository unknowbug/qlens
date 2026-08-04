# QLens Overview

> **QLens is an image viewer — and more importantly, it provides a general-purpose image-tagging protocol designed for the AI era.**

## Why QLens?

### Origin 1: No viewer like Picasa anymore

Anyone who used Picasa remembers the experience: **light, fast, clean**. Double-click to view, non-intrusive UI, pleasant browsing. Picasa is long dead. What replaced it?

- **ACDSee and friends**: full-featured but heavy — slow startup, toolbar-laden UI
- **Others**: either light but with poor browsing, or good-looking but far from light

QLens starts from wanting back that "light, fast, clean viewing experience".

### Origin 2: Viewers all miss one thing

Everyone has piles of photos — weddings, trips, snapshots, work assets. Viewers can open, zoom, page through them beautifully, but **none can answer**:

> "Of those 200 photos I took at the beach last year, which ones have closed eyes?"

## The Three Components

QLens is three components built around one open image-tagging protocol (`qltag.db`):

```
┌─────────────────────────────────────────────────────────┐
│              QLens Tag Protocol (qltag.db)              │
│   one SQLite DB per folder — any software/AGENT can R/W │
└───────────┬──────────────────────────┬──────────────────┘
            │                          │
   ┌────────▼────────┐       ┌─────────▼─────────┐
   │  QLens Manager  │       │ QLens QuickView   │
   │ (Qt file mgr)   │       │ (native Win32+D3D11)│
   │ thumbs/tags/QC  │       │ fast viewing + HDR │
   └────────┬────────┘       └─────────┬─────────┘
            │                          │
   ┌────────▼──────────────────────────▼─────────┐
   │            QLens MCP Server                  │
   │   opens the library to AI clients (Claude…)  │
   └──────────────────────────────────────────────┘
```

### 1. QLens Manager — tagging + file management

A Qt file-manager style UI — QLens's "workbench":

- **Thumbnail grid**: async loading, folder collage previews, format icons, Ctrl+wheel zoom
- **Tag system**: right-panel tagging (with autocomplete), tag colors, batch add/remove, **combo filter** (multi-tag AND), QC filter
- **QC inspection**: one-click batch detection (overexposure/blur/color-cast) — results as emoji corner badges
- **Batch ops**: convert, resize, rename (ACDSee style)
- **Built-in viewer**: double-click to large view, zoom/rotate/page

### 2. QLens QuickView — the fast viewer

Native Win32 + D3D11, no Qt — **fast start, low memory, smooth viewing**:

- Instant open, wheel paging, F/S for 100%/fit
- **HDR rendering**: true HDR (16bit+) physical passthrough; SDR images enhanced on HDR displays
- **WIC full formats** + decoder plugins (HEIC/AVIF/SVG done; extensible)
- Bottom thumbnail strip, drag-to-open, GIF animation, EXIF auto-rotate

### 3. QLens MCP Server — AI ecosystem

Opens your library to external AI clients via MCP (Model Context Protocol):

- 14 tools: search / combo search / stats / tagging / move / batch analysis
- AI reads, tags, and analyzes through it — **QLens itself has zero AI code**
- Auto-precompresses oversized images before analysis to save tokens

## Core: The Tag Protocol

Everything revolves around `qltag.db` — one SQLite database per folder, bare filenames, naturally tree-shaped.

**Classification philosophy**: tags are classified by detection method —

- **Fixed tags (qc)**: reliably detectable without AI (overexposure/blur/color-cast) — local CV
- **Standard tags (ai)**: require AI (red-eye/closed-eyes) — MCP/external AGENT
- **Normal tags**: manual or anything

See [Tag Protocol Spec](QLENS_TAG_PROTOCOL.md).

## Typical Workflow

1. View images with QuickView (fast, HDR)
2. Double-click into Manager — browse folders, double-click to large view
3. Tag selected images (right panel / batch)
4. Click **QC Detect** to batch-tag fixed tags
5. Use combo filter / QC filter to find images
6. (Optional) Let AI read/tag/analyze through MCP
