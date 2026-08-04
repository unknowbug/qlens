# QLens 标签协议规范

**版本**：v2（每文件夹一个 `qltag.db`）

QLens 的标签协议设计目标：**任何软件、脚本或 AGENT 都能读写图片标签**，不依赖 QLens 本体。

## 1. 核心约定

- **每个文件夹一个隐藏文件 `qltag.db`**（SQLite 3）——数据库就在它管理的文件夹里，删除文件夹即删除标签，子文件夹各自独立 DB，**树状天然**。
- **只存纯文件名**（如 `IMG_0001.jpg`），不存绝对路径——文件夹移动/重命名后标签不失效。
- **WAL 模式**（`PRAGMA journal_mode=WAL`）——支持 Manager 与 MCP/外部进程并发读写。

## 2. 表结构

### `tags`

| 列 | 类型 | 说明 |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | 标签 ID |
| `name` | TEXT UNIQUE NOT NULL | 标签名（唯一） |
| `category` | TEXT DEFAULT '' | 分类：`qc`（固定标）/ `ai`（标准标）/ 空（普通）/ 自定义 |
| `color` | TEXT DEFAULT '' | 标签颜色（`#rrggbb`；Manager 色点显示） |
| `icon` | TEXT DEFAULT '' | 图标（emoji；非空 = 固定标/标准标，缩略图角标显示） |

### `image_tags`

| 列 | 类型 | 说明 |
|---|---|---|
| `filename` | TEXT NOT NULL | 图片文件名（`tags` 所在文件夹内） |
| `tag_id` | INTEGER NOT NULL | 标签 ID（关联 `tags.id`） |
| `source` | TEXT DEFAULT 'manual' | 来源：`manual`（手动）/ `qc`（本地检测）/ `ai`（AI 检测）/ 自定义 |
| `confidence` | REAL DEFAULT 1.0 | 置信度 0~1（AI/QC 检测用） |
| PK | `(filename, tag_id)` | 联合主键 |

索引：`idx_image_tags_file(filename)`、`idx_image_tags_tag(tag_id)`。

## 3. 标签分类哲学

QLens 把标签按**检测方式**分类——这是协议的核心设计：

| category | 含义 | 检测方式 | 预置标签 |
|---|---|---|---|
| `qc` | **固定标** | **非 AI 算法能靠谱检测**（本地 CV） | 模糊 🌫 / 曝光过度 ☀ / 色偏 🎨 |
| `ai` | **标准标** | **必须 AI 检测**（MCP / 外部 AGENT） | 红眼 👁 / 闭眼 😑 |
| （空） | 普通标签 | 手动 / 任意 | 任意 |

- **固定标**：QLens Manager 内置「QC 检测」按钮批量检测（曝光过度 = 高光占比 >15%；色偏 = RGB 通道均值偏差 >20；模糊 = 灰度拉普拉斯方差 <60）。
- **标准标**：通过 MCP `qlens_analyze` 或外部 AI AGENT 打标，写入时建议带 `source='ai'` + `confidence`。
- **图标（icon）**：非空即视为"质检标"，缩略图右上角叠加 emoji 角标（Manager 与未来客户端通用）。

## 4. 固定标预置

Manager 打开数据库时幂等预置（已存在则补 icon/category）：

```
模糊 🌫  → category='qc'
曝光过度 ☀ → category='qc'
色偏 🎨  → category='qc'
红眼 👁  → category='ai'
闭眼 😑  → category='ai'
```

客户端/AGENT 写入这些标签名时，不需要关心 icon——QLens 侧已定义。

## 5. 导入 / 导出格式

### CSV（默认，UTF-8 BOM）

```csv
filename,tags
IMG_0001.jpg,猫,白
IMG_0002.jpg,风景
```

### JSON

```json
{
  "folder": "E:\\Pictures",
  "images": {
    "IMG_0001.jpg": ["猫", "白"],
    "IMG_0002.jpg": ["风景"]
  }
}
```

导入已存在的 `(filename, tag_id)` 组合自动跳过（幂等）。

## 6. 跨平台访问

- **Python**：`src/mcp/qlens_lib.py` 提供完整访问层（`ensure_schema` / `get_tags` / `set_tags` / `add_tags` / `search_by_tag` / `combo_search` / `tag_stats` / `export_tags` / `import_tags`），并有 CLI 入口：
  ```
  python qlens_lib.py export <folder> <file> <csv|json>
  python qlens_lib.py import <folder> <file> <csv|json>
  ```
- **SQL 直接访问**：任何 SQLite 客户端/驱动均可——按上面表结构读写即可。
