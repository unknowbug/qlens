# QLens MCP Server

QLens exposes your image library to external AI clients (Claude Desktop / Cursor / CherryStudio etc.) via **MCP (Model Context Protocol)** — AI can search, tag, analyze, and batch-process your images. **QLens itself contains zero AI code.**

## Setup

Add a stdio server in your MCP client:

```json
{
  "mcpServers": {
    "qlens": {
      "command": "python",
      "args": ["<QLens>/src/mcp/server.py"],
      "cwd": "<QLens>/src/mcp"
    }
  }
}
```

**Note**: keep QLens Manager running — `qlens_analyze` auto-precompresses oversized images before upload to save tokens (via Manager's decode service).

## Tools (14)

### Query

| tool | function | args |
|---|---|---|
| `qlens_list_folder` | list all images + tags in a folder | `folder` |
| `qlens_search_tag` | search by single tag (returns absolute paths) | `tag`, `folder?` |
| `qlens_combo_search` | **multi-tag combo search** (AND/OR) | `tags[]`, `folder?`, `match: "all"\|"any"` |
| `qlens_get_tags` | tags of one image | `image_path` |
| `qlens_folder_tags` | all used tags in a folder (deduped) | `folder` |
| `qlens_tag_stats` | **per-tag image counts** (desc) | `folder` |

### Tagging

| tool | function | args |
|---|---|---|
| `qlens_set_tags` | set tags wholesale (replace/clear) | `image_path`, `tags[]` |
| `qlens_add_tags` | incrementally add tags (no overwrite) | `image_path`, `tags[]` |
| `qlens_export_tags` | export all tags to CSV/JSON | `folder`, `out_path`, `fmt?` |
| `qlens_import_tags` | import tags from CSV/JSON (skip existing) | `folder`, `in_path`, `fmt?` |

### File ops

| tool | function | args |
|---|---|---|
| `qlens_move_files` | move/archive files (tags migrate to new qltag.db) | `moves[]: {src, dst}` |
| `qlens_rename_files` | rename files (tags migrate to new name) | `renames[]: {src, new_name}` |
| `qlens_delete_files` | ⚠️ **permanent delete** (no Recycle Bin; require client confirmation) | `paths[]` |

### Analysis

| tool | function | args |
|---|---|---|
| `qlens_analyze` | batch QC tagging (OpenCV local detection, writes `source` + `confidence`) | `folder`, `task?="qc"`, `recursive?`, `confidence?=0.5` |

## Examples (AI client prompts)

```
1. Find images in E:\Pictures tagged BOTH "cat" and "white"
   → qlens_combo_search(tags=["cat","white"], folder="E:\\Pictures", match="all")

2. Count the 10 most common tags in E:\Pictures
   → qlens_tag_stats(folder="E:\\Pictures")

3. Append tag "portrait" to E:\Pictures\a.jpg
   → qlens_add_tags(image_path="E:\\Pictures\\a.jpg", tags=["portrait"])

4. Batch-check E:\Pictures for overexposure/blur (auto-writes fixed tags)
   → qlens_analyze(folder="E:\\Pictures", task="qc")
```

## Tag Classification Hints

- **Fixed tags (qc)**: overexposure / blur / color-cast — local CV, write `source='qc'`.
- **Standard tags (ai)**: red-eye / closed-eyes — need AI vision, write `source='ai'` + `confidence`.
- Fixed tag names/icons are preset by QLens — AGENTs just write tag names.

See [Tag Protocol](QLENS_TAG_PROTOCOL.md).

## Standalone CLI (no MCP client needed)

```bash
# export/import tags (CSV/JSON)
python src/mcp/qlens_lib.py export <folder> <out_file> <csv|json>
python src/mcp/qlens_lib.py import <folder> <in_file> <csv|json>
```

## Self-test

```bash
python src/mcp/test_mcp.py    # MCP tool smoke test
python src/mcp/test_analyze.py # analysis flow test
```
