# QLens Manager Manual

Qt file-manager style — browse folders, manage tags, QC inspection, batch operations.

## Launch & Layout

- Opens the system Pictures folder by default (`%USERPROFILE%\Pictures`, follows registered location).
- Command-line arg with an image path → opens its folder and selects the image.

```
┌────────────────────────────────────────────────────────────┐
│ Menubar: File  Settings  MCP  Help                          │
│ Toolbar: ← → ↑ [path breadcrumb] [Filter:▾][QC:▾][HL:▾][✕][QC Detect] │
├──────────┬─────────────────────────────────┬───────────────┤
│ Browse   │  Thumbnail grid                 │  Tag panel    │
│ (folders)│  (double-click → viewer / menu) │  (tag/color)  │
├──────────┴─────────────────────────────────┴───────────────┤
│ Status: image count / selected count / selected file size   │
└────────────────────────────────────────────────────────────┘
```

## Keyboard Shortcuts

| place | key | action |
|---|---|---|
| main | `Esc` | viewer → back to grid |
| main | `Backspace` | parent directory |
| main | `Alt+←` / `Alt+→` | history back / forward |
| main | `Alt+↑` | parent directory |
| grid | `F2` | rename (single = inline edit; multi = batch rename) |
| grid | `Delete` | move to Recycle Bin |
| grid | `Ctrl+wheel` | thumbnail size 80~320px (step 32) |
| grid | slow double-click filename | inline rename (>500ms, Explorer logic) |
| viewer | `←`/`↑` | previous |
| viewer | `→`/`↓`/`Space` | next |
| viewer | `Home` / `End` | first / last |
| viewer | `+`/`=` / `-` | zoom in ×1.25 / out ÷1.25 |
| viewer | `1`/`F` | 100% original |
| viewer | `0`/`S` | fit window |
| viewer | `Q` / `E` | rotate CCW / CW 90° |
| viewer | `Ctrl+wheel` | zoom; plain wheel = page |
| tag input | Enter / `Tab` | submit / complete first candidate |

## Toolbar

| control | function |
|---|---|
| `←` `→` `↑` | history back / forward / parent |
| breadcrumb | click segment to jump; click `\` separator for subfolder menu; click blank to edit path (Esc exits) |
| Filter ▾ | filter by tag (editable); **comma = multi-tag AND** (e.g. `cat, white`) |
| QC ▾ | fixed-tag filter (category='qc'), stacks with tag filter |
| Highlight ▾ | color-mark grid items by tag |
| `✕` | clear filter & highlight (QC back to "all") |
| `QC Detect` | batch-detect current folder (overexposure/blur/color-cast) and write fixed tags |

## Context Menu (grid)

| item | function |
|---|---|
| Open in viewer | large view |
| Save as... | PNG/JPEG/WebP/BMP |
| Copy | **3-in-1**: file + path text + image bitmap |
| Rename (F2) | inline / batch |
| Delete (Recycle Bin) | `QFile::moveToTrash` |
| Batch convert... | pick format + output dir, convert whole folder |
| Batch resize... | max-edge pixels + output dir (no overwrite) |
| Batch rename... | name template + start index (ACDSee style) |
| Batch add tags... | comma-separated, applies to selection + right-click item |
| Batch remove tags... | comma-separated |

**Drag & drop**: drag out = copy files to Explorer; drag in = same-drive move / cross-drive copy (recursive folders).

## Tag System

- **Right tag panel**: shows tags of the selected image; input `tag1, tag2` + Enter to batch-assign; live autocomplete, `Tab` completes first candidate.
- **Tag color**: right-click a tag → "Set color..." color picker; color dot shown before the tag (protocol `tags.color`).
- **Combo filter**: comma-separated tags in the filter box = **AND**; folders hidden while filtering.
- **QC filter**: only show images hitting a fixed tag (e.g. "blur").
- After tagging/untagging, filter candidates and thumbnail badges refresh automatically.

## QC Detection (Fixed Tags)

The **QC Detect** button batch-detects the current folder in the background (UI stays responsive), writing fixed tags:

| fixed tag | algorithm |
|---|---|
| Overexposure ☀ | pixels brighter than 229 > 15% |
| Color-cast 🎨 | max RGB channel mean deviation > 20 |
| Blur 🌫 | grayscale 3×3 Laplacian variance < 60 |

Hitting images get **emoji corner badges**. Status bar shows the tally (overexposed N | blur M | color-cast K).

> Classification: fixed tags (qc) = locally detectable by CV; standard tags (ai) = need AI (red-eye/closed-eyes, via MCP). See [Tag Protocol](QLENS_TAG_PROTOCOL.md).

## Menubar

| menu | contents |
|---|---|
| File | Open folder / Open image / **Export tags** (CSV/JSON) / **Import tags** / Exit |
| Settings | Language (Chinese/English, restart to apply) / Register as default viewer (13 extensions) |
| MCP | About MCP (tool intro) |
| Help | About Tag Protocol (opens the spec) / About QLens |

## Built-in Viewer

- Double-click an image to enter; top `← Back` button + full path; double-click the viewer to return.
- Supports QImageReader formats + `cr2 cr3 nef arw dng rw2 heic heif avif svg svgz jxr` (qlens fallback).
- Images smaller than the window show at 100% (no upscale); >4096px pre-scaled on decode.
- Status bar shows filename / size / format / file size; right tag panel follows on navigation.

## Status Bar

Shows: total images / selected count / selected file size.
