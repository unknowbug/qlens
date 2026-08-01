# QLens

> **QLens is an image viewer — and more importantly, it provides a universal image-tagging protocol designed for the AI era.**

[English](README_EN.md) · 中文

A lightweight image viewer + manager built around an open tagging protocol (`qltag.db`),
letting any software, script, or agent read and write image tags.

---

## Why does QLens exist?

### The starting point: plenty of viewers, but all missing one thing

Everyone has folders full of photos — weddings, trips, snapshots, work assets. Viewers today
can open, zoom, and flip through images fast and beautifully, but none of them can answer:

> "Which of the 200 photos I took at the beach last year have closed eyes?"

Photos live on disk, memories live in your head — and between them sits a missing layer: **tags**.
QLens is first and foremost a capable viewer; its real value is adding that missing tag layer to "viewing photos".

### The turning point: AI made tagging possible for the first time

Tagging photos used to be pure drudgery: manually writing "wedding", "beach", "closed_eyes"
one by one. With AI, recognizing image content became a few lines of code. But AI tagging
brought a new problem:

**Vision models charge by the image — a single 4K original can burn tens of thousands of tokens.**
If a customer feeds original images straight to an agent, one batch-tagging session can blow
a large chunk of their budget.

### And so QLens's core tension emerged

- Customers want **AI-powered flexibility** (letting an agent organize their library however they like)
- But batch tagging **requires pre-compression and batching**, or tokens explode
- These two directions pull against each other: agent-driven vs program-controlled

### Our answer: separate the "protocol" from the "execution"

```
QLens Manager ── MCP (tied to Manager lifecycle; batch analysis auto-pre-compresses)
      │
      └─ qltag.db (one per folder, open protocol) ── customers read/write freely with their own agents
```

- **MCP layer**: Agents want to play? Nine tools, call them freely. Need batch tagging? Go through
  `qlens_analyze` — the program pre-compresses before analysis, so tokens don't burn.
- **Protocol layer**: Tag data lives in a `qltag.db` in every folder, format fully open. Customers
  who understand it can **design their own extensions** — their own scripts, their own agents, any
  tool, any time, without QLens even running.

QLens owns *how tags get written* (execution); *what you do with tagged photos* (the playbook) is entirely yours.

> **QLens is first an image viewer; what it truly delivers is a tagging protocol designed for the AI era, plus an execution engine that stops AI from burning your tokens.**

---

## Components

| Component | Description |
|-----------|-------------|
| **qlens_quick** (C++/Qt) | Minimal viewer: drag-and-drop, thumbnail strip, zoom / page navigation |
| **qlens_manager** (C++/Qt) | Library manager: folder tree + virtualized thumbnail grid + tag panel |
| **qlens MCP Server** (Python) | Exposes library operations to any MCP client (Claude / CherryStudio / Cursor) |
| **qltag.db protocol** | Open tag storage format, freely extensible by customers |

## Core design

- **One `qltag.db` per folder** (hidden file): tags travel with the folder — copy or move, tags follow
- **Pure filenames**: each DB lives in the folder it manages, naturally tree-structured and recursive
- **WAL concurrency**: Manager and multiple agents can read/write simultaneously
- **MCP tied to Manager**: QLens pre-compresses images internally before batch tagging, so agents never upload originals and burn tokens

## Quick start

### View images (QuickView)
```powershell
.\build\bin\Debug\qlens_quick.exe
```

### Manager + tags (Manager)
```powershell
.\build\bin\Debug\qlens_manager.exe
```

### MCP (external agents)
1. Start `qlens_manager.exe` first (MCP depends on Manager)
2. Configure in your MCP client:
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

## Docs

- [Tag protocol (qltag.db)](src/mcp/QLENS_TAG_PROTOCOL.md) — open data format ([中文](src/mcp/QLENS_TAG_PROTOCOL.md))
- [MCP design notes](src/mcp/README.md) — why MCP ties to Manager + tool list ([中文](src/mcp/README.md))

## Build

```bash
cmake -B build
cmake --build build
```

Dependencies: Qt 6 (Core/Gui/Widgets/Sql), LibRaw, LCMS2 (auto-detected by CMake).

## License

Apache-2.0
