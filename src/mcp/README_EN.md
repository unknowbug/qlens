# QLens MCP Server

Exposes QLens library operations to any MCP client (Claude Desktop / CherryStudio / Cursor ...).
Tag storage follows the open **qltag.db protocol** (see [QLENS_TAG_PROTOCOL.md](QLENS_TAG_PROTOCOL.md)).

## Configuration example (mcp client settings.json / mcp.json)

```json
{
  "mcpServers": {
    "qlens": {
      "command": "python",
      "args": ["E:/PYTHON/qlens/src/mcp/server.py"],
      "cwd": "E:/PYTHON/qlens/src/mcp"
    }
  }
}
```

## Why must Manager be running for MCP to work?

**QLens MCP is tied to the Manager lifecycle**: if Manager is not running, MCP refuses to connect.

Reason: batch tagging / image analysis is a **high-token-consuming** operation. If an agent uploads
original images (possibly 4K, several MB) straight to a vision model, one analysis can burn tens of
thousands of tokens. QLens instead **pre-compresses images in-process** (downscale in memory → upload →
analyze → write to DB), so tokens are spent only on *deciding which images to analyze*, never on image bytes.

That capability lives in Manager, which is why MCP needs Manager running.

## If you understand this design, you can build your own

QLens is fundamentally not a viewer — it's **a universal image tagging protocol tool**:

- Tag storage = the open `qltag.db` protocol (one per folder, pure filenames, WAL concurrency-safe)
- Any script/agent can **read/write qltag.db directly**, with no need for QLens to run
- We publish the MCP design and protocol docs so customers understand the mechanism and can extend it their own way

**Two tiers:**
| Tier | Capability | Prerequisite |
|------|-----------|--------------|
| QLens MCP | Tag/search/archive (with pre-compression) | Start Manager |
| qltag.db protocol | Fully free read/write | None (any tool, anytime) |

## Tools

| Tool | Description | Risk |
|------|-------------|------|
| qlens_list_folder | List images and tags in a folder | low |
| qlens_search_tag | Search images by tag | low |
| qlens_get_tags | Get tags for one image | low |
| qlens_set_tags | Set/replace image tags | medium |
| qlens_add_tags | Incrementally add tags | medium |
| qlens_folder_tags | Candidate tags in a folder | low |
| qlens_move_files | Move/archive files (tag migration) | medium |
| qlens_rename_files | Rename (tag migration) | medium |
| qlens_delete_files | Delete (high-risk, explicit warning) | high |

## Storage

- One `qltag.db` per folder (hidden on Windows), storing pure filenames
- SQLite WAL mode allows concurrent read/write from multiple processes (Manager + multiple agents)
