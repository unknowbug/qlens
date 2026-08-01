# QLens MCP Server

[English](README_EN.md) · 中文

把 QLens 图库操作暴露给任何 MCP 客户端（Claude Desktop / CherryStudio / Cursor ...）。
标签数据存储遵循公开的 **qltag.db 协议**（见 [QLENS_TAG_PROTOCOL.md](QLENS_TAG_PROTOCOL.md)）。

## 配置示例（mcp 客户端 settings.json / mcp.json）

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

## 为什么 MCP 必须启动 Manager 才能用？

**QLens MCP 绑定 Manager 生命周期**：Manager 未运行时，MCP 会拒绝连接。

原因：批量打标/图片分析是**高 token 消耗**操作。若 AGENT 直接把原图（可能 4K、数 MB）上传给大模型分析，一次可能烧掉数万 tokens。QLens 的做法是**在程序内部先做图片预压缩**（内存缩图 → 再上传 → 分析 → 写库），token 只花在"决定分析哪些图"，不花在图片字节上。

这个能力由 Manager 提供，所以 MCP 需要 Manager 在跑。

## 如果你看懂了这套设计，你可以自己做

QLens 本质不是看图工具，而是**一套通用的图片标签协议工具**：

- 标签存储 = 公开的 `qltag.db` 协议（每文件夹一个，纯文件名，WAL 并发安全）
- 任何脚本/AGENT 都可以**直接读写 qltag.db**，完全不需要 QLens 运行
- 我们公开 MCP 设计和协议文档，是希望使用者理解这套机制后，能按自己的需求扩展

**两个层级：**
| 层级 | 能力 | 前置条件 |
|------|------|---------|
| QLens MCP | 打标/搜索/归档（带预压缩） | 启动 Manager |
| qltag.db 协议 | 完全自由的读写 | 无（任何工具随时可用） |

## 工具清单

| 工具 | 说明 | 危险 |
|------|------|------|
| qlens_list_folder | 列出文件夹图片及标签 | 低 |
| qlens_search_tag | 按标签搜索图片 | 低 |
| qlens_get_tags | 查单张图片标签 | 低 |
| qlens_set_tags | 打/改标签 | 中 |
| qlens_add_tags | 增量添加标签 | 中 |
| qlens_folder_tags | 文件夹候选标签 | 低 |
| qlens_move_files | 移动/归档文件（标签迁移） | 中 |
| qlens_rename_files | 重命名（标签迁移） | 中 |
| qlens_delete_files | 删除（高危，会显式警告） | 高 |

## 数据存储

- 每文件夹一个 `qltag.db`（Windows 上隐藏），存纯文件名
- SQLite WAL 模式允许多进程并发读写（Manager + 多个 AGENT 同时访问）
