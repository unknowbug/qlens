# QLens MCP Server 文档

QLens 通过 **MCP（Model Context Protocol）** 把图片库开放给外部 AI 客户端（Claude Desktop / Cursor / CherryStudio 等）——AI 可以搜索、打标、统计、批量分析你的图片，**QLens 本身零 AI 代码**。

## 配置

在 MCP 客户端添加 stdio server：

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

**注意**：请保持 QLens Manager 运行——`qlens_analyze` 分析前自动预压缩超大图，防止超大图直接上传烧爆 token（通过 Manager 的解码服务）。

## 工具列表（14 个）

### 查询

| 工具 | 功能 | 参数 |
|---|---|---|
| `qlens_list_folder` | 列出文件夹内所有图片及已打标签 | `folder` |
| `qlens_search_tag` | 按单个标签搜索图片（返回绝对路径） | `tag`, `folder?` |
| `qlens_combo_search` | **多标签组合搜索**（AND/OR） | `tags[]`, `folder?`, `match: "all"\|"any"` |
| `qlens_get_tags` | 查询单张图片的标签列表 | `image_path` |
| `qlens_folder_tags` | 列出文件夹内全部已用标签（去重） | `folder` |
| `qlens_tag_stats` | **每个标签的图片数统计**（降序） | `folder` |

### 打标

| 工具 | 功能 | 参数 |
|---|---|---|
| `qlens_set_tags` | 全量设置图片标签（替换/清空） | `image_path`, `tags[]` |
| `qlens_add_tags` | 增量添加标签（不覆盖） | `image_path`, `tags[]` |
| `qlens_export_tags` | 导出文件夹全部图片标签到 CSV/JSON | `folder`, `out_path`, `fmt?` |
| `qlens_import_tags` | 从 CSV/JSON 导入标签（已存在跳过） | `folder`, `in_path`, `fmt?` |

### 文件操作

| 工具 | 功能 | 参数 |
|---|---|---|
| `qlens_move_files` | 移动/归档文件（标签迁移到新 qltag.db） | `moves[]: {src, dst}` |
| `qlens_rename_files` | 重命名文件（标签随新名迁移） | `renames[]: {src, new_name}` |
| `qlens_delete_files` | ⚠️ **永久删除**（无回收站，需客户端确认） | `paths[]` |

### 分析

| 工具 | 功能 | 参数 |
|---|---|---|
| `qlens_analyze` | 批量 QC 质检打标（OpenCV 本地检测，写 `source` + `confidence`） | `folder`, `task?="qc"`, `recursive?`, `confidence?=0.5` |

## 示例（AI 客户端提示词）

```
1. 找出 E:\Pictures 里同时打了"猫"和"白"标签的图
   → qlens_combo_search(tags=["猫","白"], folder="E:\\Pictures", match="all")

2. 统计 E:\Pictures 里最常见的 10 个标签
   → qlens_tag_stats(folder="E:\\Pictures")

3. 给 E:\Pictures\a.jpg 追加标签"竖构图"
   → qlens_add_tags(image_path="E:\\Pictures\\a.jpg", tags=["竖构图"])

4. 批量检查 E:\Pictures 里哪些图过曝/模糊（自动写固定标）
   → qlens_analyze(folder="E:\\Pictures", task="qc")
```

## 标签分类提示（写标建议）

- **固定标（qc）**：曝光过度 / 模糊 / 色偏——本地 CV 检测，写 `source='qc'`。
- **标准标（ai）**：红眼 / 闭眼——需 AI 视觉判断，写 `source='ai'` + `confidence`。
- 固定标名/icon 由 QLens 预置，AGENT 只需写标签名。

详见[标签协议规范](QLENS_TAG_PROTOCOL.md)。

## 独立 CLI（无需 MCP 客户端）

```bash
# 导出/导入标签（CSV/JSON）
python src/mcp/qlens_lib.py export <folder> <out_file> <csv|json>
python src/mcp/qlens_lib.py import <folder> <in_file> <csv|json>
```

## 自测

```bash
python src/mcp/test_mcp.py    # MCP 工具冒烟测试
python src/mcp/test_analyze.py # 分析流程测试
```
