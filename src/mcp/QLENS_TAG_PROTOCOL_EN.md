# QLens Tag Protocol (qltag.db)

[中文](QLENS_TAG_PROTOCOL.md) · English

QLens stores tag data as **one `qltag.db` per folder**. This is QLens's **public data protocol** —
any software, script, or agent can read and write it directly, decoupled from the QLens application itself.

## Core rules

1. **Location**: one `qltag.db` per folder (marked hidden on Windows)
2. **Scope**: a DB manages only **images in its own folder** (does not recurse into subfolders)
3. **Subfolders**: each has its own independent DB — a natural tree structure from the filesystem
4. **Path storage**: the DB stores **pure filenames** (e.g. `001.jpg`), because the DB lives in the same directory as the images
5. **Concurrency**: SQLite WAL mode — Manager, MCP, and other processes can read/write concurrently

## Directory example

```
D:/my-library/
├── qltag.db            ← manages only images in this level
├── 001.jpg
├── wedding/
│   ├── qltag.db        ← manages only images inside wedding/
│   ├── 001.jpg
│   └── 002.jpg
└── landscape/
    ├── qltag.db
    └── mountain.jpg
```

## Schema

```sql
-- Tag dictionary (all tags used within this folder)
CREATE TABLE tags (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT UNIQUE NOT NULL,      -- tag name, e.g. "wedding", "blurry"
    category TEXT DEFAULT '',            -- reserved (e.g. "qc" / "content")
    color    TEXT DEFAULT ''             -- reserved (grid highlight color)
);

-- Image-tag association
CREATE TABLE image_tags (
    filename   TEXT NOT NULL,            -- pure filename, e.g. "001.jpg"
    tag_id     INTEGER NOT NULL,         -- references tags.id
    source     TEXT DEFAULT 'manual',    -- origin: manual / qc / api / agent
    confidence REAL DEFAULT 1.0,         -- confidence 0~1 (for AI tagging)
    PRIMARY KEY (filename, tag_id)
);

-- Index
CREATE INDEX idx_image_tags_file ON image_tags(filename);
```

## Conventions

### Tag naming
- Any UTF-8 string, no enforced format
- QC quality tags have a fixed set (below) shown with dedicated corner badges in the grid

### QC quality tags (built-in badges)
| Tag name | Meaning |
|----------|---------|
| 红眼 (red-eye) | red eye |
| 闭眼 (closed-eye) | eyes closed |
| 模糊 (blurry) | blurred image |
| 曝光过度 (overexposed) | overexposed |
| 色偏 (color cast) | color cast |

### Write rules
- **New images**: no pre-registration needed — writing to `image_tags` auto-creates a tag if missing
- **Move/rename images**: delete records from the old DB, write into the new location's DB (tag migration)
- **Delete images**: remove their `image_tags` records at the same time

## Query examples

```sql
-- All tags for one image
SELECT t.name FROM tags t
JOIN image_tags it ON t.id = it.tag_id
WHERE it.filename = '001.jpg'
ORDER BY t.name;

-- All tags in this folder (deduplicated)
SELECT DISTINCT t.name FROM tags t
JOIN image_tags it ON t.id = it.tag_id
ORDER BY t.name;
```

## AI tagging pre-compression spec

QLens's `qlens_analyze` (AI batch tagging) pre-compresses images **in-process** before calling the
vision model, preventing agents from uploading originals and burning tokens.

### Resolution targets

| Task | Target | Note |
|------|--------|------|
| **Content tags** (wedding/landscape/people/objects) | **long edge ≤ 720px** | Vision models are trained at 512–768px; 720px already exceeds effective input |
| **QC checks** (blurry/overexposure/color cast/closed-eye/red-eye) | algorithm on **original resolution** | downsampling misjudges blur; QC does not go through a VLM |
| **Detail captions** (optional) | long edge ≤ 1080px | only when describing fine detail |

### Why 720px is enough

Modern vision models (CLIP/ViT-style) encode images at fixed patch sizes — content beyond the
training resolution gets cropped/downscaled. **Feeding a 4K original vs a 720p version carries the
same effective information, but costs 90%+ more tokens.** 720px long edge is the sweet spot for
tagging accuracy vs cost.

### Rules

1. Compression happens inside QLens; **image bytes never enter the agent context**
2. stable_id (hash) is always computed on the **original file** (size + mtime + first 4KB), independent of compression
3. QC detection runs algorithmically on the original resolution, uncompressed

## Relationship with QLens components

| Component | Role | When needed |
|-----------|------|-------------|
| QLens Manager (GUI) | Reads/writes qltag.db; manual tagging, highlighting, filtering | Manual user operations |
| QLens MCP Server | Reads/writes qltag.db for external agents | Requires Manager running |
| **Your script/agent** | **Read/write qltag.db directly** | **Anytime — QLens does not need to run** |

**QLens MCP is tied to the Manager lifecycle; the qltag.db protocol itself is fully independent —
once you have the tag data, you can do whatever you want with any tool.**
