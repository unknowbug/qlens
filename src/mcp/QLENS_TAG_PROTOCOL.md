# QLens 标签协议（qltag.db）

QLens 的标签数据以 **每文件夹一个 `qltag.db`** 的形式存储。这是 QLens 的**公开数据协议**——任何软件、脚本或 AGENT 都可以直接读写，与 QLens 应用本身解耦。

## 核心规则

1. **位置**：每个文件夹一个 `qltag.db`（Windows 上标记为隐藏文件）
2. **范围**：一个 DB 只管**本文件夹内的图片**（不递归子文件夹）
3. **子文件夹**：各自独立 DB，文件系统天然的树状结构
4. **路径存储**：DB 里存**纯文件名**（如 `001.jpg`），因为 DB 就在图片所在目录
5. **并发**：SQLite WAL 模式，Manager / MCP / 其他进程可并发读写

## 目录示例

```
D:/我的图库/
├── qltag.db            ← 只管本层图片
├── 001.jpg
├── 婚礼/
│   ├── qltag.db        ← 只管婚礼/ 内图片
│   ├── 001.jpg
│   └── 002.jpg
└── 风景/
    ├── qltag.db
    └── 山.jpg
```

## 表结构

```sql
-- 标签字典（本文件夹内所有用到的标签）
CREATE TABLE tags (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT UNIQUE NOT NULL,      -- 标签名，如 "婚礼"、"模糊"
    category TEXT DEFAULT '',            -- 分类（预留，如 "qc" / "content"）
    color    TEXT DEFAULT ''             -- 标签颜色（预留，网格着色用）
);

-- 图片-标签关联
CREATE TABLE image_tags (
    filename   TEXT NOT NULL,            -- 纯文件名，如 "001.jpg"
    tag_id     INTEGER NOT NULL,         -- 引用 tags.id
    source     TEXT DEFAULT 'manual',    -- 来源：manual / qc / api / agent
    confidence REAL DEFAULT 1.0,         -- 置信度 0~1（AI 打标用）
    PRIMARY KEY (filename, tag_id)
);

-- 索引
CREATE INDEX idx_image_tags_file ON image_tags(filename);
```

## 约定

### 标签命名
- 任意 UTF-8 字符串，无强制格式
- QC 质检标签有固定集合（见下），网格会用专用角标显示

### QC 质检标签（内置角标）
| 标签名 | 含义 |
|--------|------|
| 红眼 | 红眼 |
| 闭眼 | 闭眼 |
| 模糊 | 画面模糊 |
| 曝光过度 | 曝光过度 |
| 色偏 | 颜色偏色 |

### 写入规范
- **新增图片**：无需提前注册，写 `image_tags` 时若标签不存在会自动创建
- **移动/重命名图片**：应从旧 DB 删除记录，在新位置 DB 写入（标签迁移）
- **删除图片**：应同时删除其 `image_tags` 记录

## 查询示例

```sql
-- 某张图的全部标签
SELECT t.name FROM tags t
JOIN image_tags it ON t.id = it.tag_id
WHERE it.filename = '001.jpg'
ORDER BY t.name;

-- 本文件夹所有标签（去重）
SELECT DISTINCT t.name FROM tags t
JOIN image_tags it ON t.id = it.tag_id
ORDER BY t.name;
```

## 与 QLens 组件的关系

| 组件 | 角色 | 何时需要 |
|------|------|---------|
| QLens Manager (GUI) | 读写 qltag.db，手动打标/着色/筛选 | 用户手动操作 |
| QLens MCP Server | 读写 qltag.db，供外部 AGENT 调用 | 需先启动 Manager |
| **你的脚本/AGENT** | **直接读写 qltag.db** | **任何时间，无需 QLens 运行** |

**QLens MCP 绑定 Manager 生命周期；但 qltag.db 协议本身完全独立——客户拿到标签数据后，可以用任何工具自由发挥。**
