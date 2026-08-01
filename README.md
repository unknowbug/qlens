# QLens

> **QLens 是一个看图工具，更重要的是，它提供了一套为 AI 时代思考设计的通用图片标签协议。**

[English](README_EN.md) · 中文

轻量图片查看器 + 管理器，围绕一套开放的图片标签协议（`qltag.db`）构建，
让任何软件、脚本或 AGENT 都能读写图片标签。

---

## 为什么会有 QLens？

### 起点：看图工具太多了，但都缺一样东西

每个人电脑里都有一堆照片——婚礼的、旅行的、随手拍的、工作素材的。市面上看图工具能把图打开、缩放、翻页，做得又快又漂亮，但**没有一样东西能回答**：

> "我去年在海边拍的那 200 张照片里，哪些是闭眼的？"

照片存在硬盘里，记忆在脑子里，中间缺一层——**标签**。QLens 首先是一个称职的看图工具；但它的真正价值，是给"看图"这件事补上了缺失的标签层。

### 转折：AI 时代让打标签第一次有了可能

以前给照片打标签是苦力活：一张一张手动写"婚礼"、"海边"、"闭眼"。AI 出现后，识别图片内容变成了几行代码的事。但 AI 打标带来一个新问题：

**视觉模型按图片收 token，一张 4K 原图可能烧掉几万 tokens。** 客户如果直接把原图丢给 AGENT 分析，一次批量打标就能烧掉一大笔钱。

### 于是 QLens 的核心矛盾浮现了

- 客户想要 **AI 带来的灵活玩法**（让 AGENT 按自己想法整理图库）
- 但批量打标必须 **预压缩 + 批处理**，否则 token 爆炸
- 这两个方向是冲突的：AGENT 说了算 vs 程序自己干

### 我们的解法：把"协议"和"执行"分开

```
QLens Manager ── MCP（绑定 Manager 生命周期，批量分析自动预压缩）
      │
      └─ qltag.db（每文件夹一个，公开协议）── 客户自建 AGENT/脚本自由读写
```

- **MCP 层**：AGENT 想玩花样？给你 9 个工具随便调。要批量打标？走 `qlens_analyze`，程序内部先缩图再分析，token 不烧。
- **协议层**：标签数据存在每个文件夹的 `qltag.db` 里，格式完全公开。客户看懂了，可以**完全自己设计**——自己的脚本、自己的 AGENT、别的工具，任何时候都能读写，不需要 QLens 运行。

QLens 管"怎么打标签"（执行），把"打完标签干什么"（玩法）完全交给客户。

> **QLens 首先是一个看图工具；它真正交付的，是一套为 AI 时代设计的图片标签协议 + 一个不让 AI 烧你钱的执行引擎。**

---

## 组件

| 组件 | 说明 |
|------|------|
| **qlens_quick** (C++/Qt) | 极简看图器：拖入即看，缩略图条，缩放/翻页 |
| **qlens_manager** (C++/Qt) | 图库管理器：文件夹树 + 虚拟化缩略图网格 + 标签面板 |
| **qlens MCP Server** (Python) | 暴露图库操作给任何 MCP 客户端（Claude / CherryStudio / Cursor） |
| **qltag.db 协议** | 公开的标签存储格式，客户可自由扩展 |

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

- [标签协议 (qltag.db)](src/mcp/QLENS_TAG_PROTOCOL.md) — 公开数据格式（[English](src/mcp/QLENS_TAG_PROTOCOL_EN.md)）
- [MCP 设计说明](src/mcp/README.md) — 为什么绑定 Manager + 工具清单（[English](src/mcp/README_EN.md)）

## 构建

```bash
cmake -B build
cmake --build build
```

依赖：Qt 6 (Core/Gui/Widgets/Sql)、LibRaw、LCMS2（CMake 自动查找）。

## 许可

Apache-2.0
