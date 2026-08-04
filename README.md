# QLens

<img src="assets/QLens.png" width="64" align="left" style="margin-right:12px" />

> **QLens 是一个为 AI 时代设计的轻量图片查看器 + 标签管理器。**

[English](README_EN.md) · 中文

QLens 由三部分组成，围绕一套开放的图片标签协议（`qltag.db`）构建，
让任何软件、脚本或 AGENT 都能读写图片标签：

| 组件 | 说明 |
|---|---|
| **QLens QuickView** | 原生 Win32 + D3D11 极速看图器——启动快、支持 **HDR 渲染**、WIC 全格式 + 解码插件 |
| **QLens Manager** | Qt 文件管理器风格——缩略图浏览、**标签管理**（打标/颜色/组合筛选）、**QC 质检**（自动检测过曝/模糊/色偏） |
| **QLens MCP Server** | 把图片库开放给 AI 客户端（Claude / Cursor 等）——搜索/打标/统计/批量分析 |

## 快速开始

```
bin/qlens_quickview.exe  双击图片或拖入图片即看（F=100% 原尺寸，S=适配窗口，滚轮翻页）
bin/qlens_manager.exe    浏览文件夹、打标签、QC 检测（进入文件夹 → 双击图片进查看器）
```

**系统要求**：Windows 10 1809+（HDR 功能需要 HDR 显示器；HEIC/AVIF 等格式需要 WIC 扩展或解码插件）。

## 文档

- [产品概述](docs/01-overview.md) —— 设计哲学与三件套
- [QuickView 手册](docs/02-quickview.md) —— 快捷键 / HDR / 插件
- [Manager 手册](docs/03-manager.md) —— 文件管理 / 标签 / QC / 批量
- [标签协议规范](docs/QLENS_TAG_PROTOCOL.md) —— `qltag.db` schema 与分类哲学 ★
- [MCP Server 文档](docs/05-mcp.md) —— 工具列表 / 配置 / 示例
- [插件开发指南](docs/06-plugin-dev.md) —— 解码插件 API
- [构建与发布](docs/07-build.md) —— 依赖 / 编译 / 系统要求

## 开源协议

[LICENSE](LICENSE)
