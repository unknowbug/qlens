# QLens Tag Protocol Specification

**Version**: v2 (one `qltag.db` per folder)

The QLens tag protocol is designed so that **any software, script, or AGENT can read and write image tags** without depending on QLens itself.

## 1. Core Conventions

- **One hidden `qltag.db` (SQLite 3) per folder** — the database lives in the folder it manages; deleting the folder deletes its tags. Subfolders have independent DBs — naturally tree-shaped.
- **Stores bare filenames only** (e.g. `IMG_0001.jpg`), never absolute paths — tags survive folder moves/renames.
- **WAL mode** (`PRAGMA journal_mode=WAL`) — concurrent access by Manager and external processes/MCP.

## 2. Schema

### `tags`

| column | type | description |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | tag ID |
| `name` | TEXT UNIQUE NOT NULL | tag name (unique) |
| `category` | TEXT DEFAULT '' | `qc` (fixed tag) / `ai` (standard tag) / '' (normal) / custom |
| `color` | TEXT DEFAULT '' | tag color (`#rrggbb`; shown as color dot in Manager) |
| `icon` | TEXT DEFAULT '' | icon (emoji; non-empty = QC tag, shown as corner badge) |

### `image_tags`

| column | type | description |
|---|---|---|
| `filename` | TEXT NOT NULL | image filename (inside the folder owning `tags`) |
| `tag_id` | INTEGER NOT NULL | references `tags.id` |
| `source` | TEXT DEFAULT 'manual' | `manual` / `qc` (local detection) / `ai` (AI) / custom |
| `confidence` | REAL DEFAULT 1.0 | confidence 0~1 (used by AI/QC detection) |
| PK | `(filename, tag_id)` | composite primary key |

Indexes: `idx_image_tags_file(filename)`, `idx_image_tags_tag(tag_id)`.

## 3. Tag Classification Philosophy

QLens classifies tags **by detection method** — the core design of the protocol:

| category | meaning | detection | preset tags |
|---|---|---|---|
| `qc` | **fixed tag** | **reliably detectable without AI** (local CV) | 模糊 blur 🌫 / 曝光过度 overexposure ☀ / 色偏 color-cast 🎨 |
| `ai` | **standard tag** | **requires AI** (MCP / external AGENT) | 红眼 red-eye 👁 / 闭眼 closed-eyes 😑 |
| (empty) | normal tag | manual / anything | any |

- **Fixed tags (qc)**: QLens Manager's built-in **QC Detect** button batch-detects the current folder (overexposure = highlight ratio >15%; color-cast = max RGB channel mean deviation >20; blur = grayscale Laplacian variance <60).
- **Standard tags (ai)**: written via MCP `qlens_analyze` or external AI AGENTs; use `source='ai'` + `confidence`.
- **Icon**: non-empty means "QC tag" — rendered as an emoji corner badge on thumbnails (Manager and future clients).

## 4. Preset Fixed Tags

Manager idempotently presets these on DB open (fills icon/category if missing):

```
模糊 blur 🌫   → category='qc'
曝光过度 overexposure ☀ → category='qc'
色偏 color-cast 🎨 → category='qc'
红眼 red-eye 👁 → category='ai'
闭眼 closed-eyes 😑 → category='ai'
```

Clients/AGENTs just write the tag names — QLens side already defines the icons.

## 5. Import / Export Formats

### CSV (default, UTF-8 BOM)

```csv
filename,tags
IMG_0001.jpg,cat,white
IMG_0002.jpg,scenery
```

### JSON

```json
{
  "folder": "E:\\Pictures",
  "images": {
    "IMG_0001.jpg": ["cat", "white"],
    "IMG_0002.jpg": ["scenery"]
  }
}
```

Existing `(filename, tag_id)` pairs are skipped on import (idempotent).

## 6. Cross-platform Access

- **Python**: `src/mcp/qlens_lib.py` is the full access layer (`ensure_schema` / `get_tags` / `set_tags` / `add_tags` / `search_by_tag` / `combo_search` / `tag_stats` / `export_tags` / `import_tags`), with a CLI:
  ```
  python qlens_lib.py export <folder> <file> <csv|json>
  python qlens_lib.py import <folder> <file> <csv|json>
  ```
- **Direct SQL**: any SQLite client/driver works — read/write per the schema above.
