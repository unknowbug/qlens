# QLens

> **QLens 不是看图工具，是一套通用的图片标签协议工具。**

轻量图片查看器 + 管理器，核心是一套开放的图片标签协议（`qltag.db`），
让任何软件、脚本或 AGENT 都能读写图片标签。

## 组件

| 组件 | 说明 |
|------|------|
| **qlens_quick** (C++/Qt) | 极简看图器：拖入即看，缩略图条，缩放/翻页 |
| **qlens_manager** (C++/Qt) | 图库管理器：文件夹树 + 虚拟化缩略图网格 + 标签面板 |
| **qlens MCP Server** (Python) | 暴露图库操作给任何 MCP 客户端（Claude / CherryStudio / Cursor） |
| **qltag.db 协议** | 公开的标签存储格式，客户可自由扩展 |

## 架构

```
QLens Manager ── MCP（绑定 Manager 生命周期，批量分析带预压缩）
      │
      └─ qltag.db（每文件夹一个，公开协议）── 客户自建 AGENT/脚本自由读写
```

## 核心设计

- **每文件夹一个 `qltag.db`**（隐藏文件）：标签跟着文件夹走，拷贝/移动即带走
- **纯文件名存储**：DB 就在它管的文件夹里，天然树状递归
- **WAL 并发**：Manager + 多个 AGENT 可同时读写
- **MCP 绑定 Manager**：批量打标前 QLens 内部先缩图，防止 AGENT 上传原图烧爆 token

## 快速开始

### 看图（QuickView）
```powershell
.\build\bin\Debug\qlens_quick.exe
```

### 管理器 + 标签（Manager）
```powershell
.\build\bin\Debug\qlens_manager.exe
```

### MCP（外部 AGENT）
1. 先启动 `qlens_manager.exe`（MCP 依赖 Manager）
2. 在 MCP 客户端配置：
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

## 文档

- [标签协议 (qltag.db)](src/mcp/QLENS_TAG_PROTOCOL.md) — 公开数据格式
- [MCP 设计说明](src/mcp/README.md) — 为什么绑定 Manager + 工具清单

## 构建

```bash
cmake -B build
cmake --build build
```

依赖：Qt 6 (Core/Gui/Widgets/Sql)、LibRaw、LCMS2（CMake 自动查找）。

## 许可

Apache-2.0
