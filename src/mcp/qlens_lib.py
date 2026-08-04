"""QLens 标签库 Python 访问层（协议 v2：每文件夹一个 qltag.db）。

与 Manager (C++) TagStore 共享同一格式：
  每个文件夹一个隐藏文件 qltag.db，存纯文件名（DB 就在它管的文件夹里）。
  子文件夹各自独立 DB，树状天然。

表结构（与 TagStore.cpp 一致）：
  tags(id INTEGER PK, name TEXT UNIQUE, category TEXT, color TEXT)
  image_tags(filename TEXT, tag_id INT, source TEXT, confidence REAL, PK(filename, tag_id))
"""
import os
import csv
import json
import sqlite3
import threading

# 连接非线程安全 → 每线程一个连接，按 db 路径缓存
_local = threading.local()


def _conn_for(db_path):
    conns = getattr(_local, "conns", None)
    if conns is None:
        conns = {}
        _local.conns = conns
    conn = conns.get(db_path)
    if conn is None:
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row
        conns[db_path] = conn
    return conn


def qltag_path(folder):
    """文件夹对应的 qltag.db 路径。"""
    return os.path.join(folder, "qltag.db")


def close_all():
    """关闭本线程所有缓存连接（测试清理 / 退出前调用）。"""
    conns = getattr(_local, "conns", None)
    if conns:
        for c in conns.values():
            try:
                c.close()
            except Exception:
                pass
        conns.clear()


def ensure_schema(folder):
    """确保 folder/qltag.db 存在且有表结构（与 Manager 一致）。"""
    os.makedirs(folder, exist_ok=True)
    db_path = qltag_path(folder)
    conn = _conn_for(db_path)
    conn.execute("CREATE TABLE IF NOT EXISTS tags ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "name TEXT UNIQUE NOT NULL,"
                 "category TEXT DEFAULT '',"
                 "color TEXT DEFAULT '',"
                 "icon TEXT DEFAULT '')")
    conn.execute("CREATE TABLE IF NOT EXISTS image_tags ("
                 "filename TEXT NOT NULL,"
                 "tag_id INTEGER NOT NULL,"
                 "source TEXT DEFAULT 'manual',"
                 "confidence REAL DEFAULT 1.0,"
                 "PRIMARY KEY(filename, tag_id))")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_image_tags_file ON image_tags(filename)")
    conn.execute("PRAGMA journal_mode=WAL")
    conn.commit()
    return db_path


def _resolve(image_path):
    """图片路径 → (db_path, filename)。
    规则：qltag.db 在图片所在目录；filename 是纯文件名。"""
    folder = os.path.dirname(os.path.abspath(image_path))
    db_path = ensure_schema(folder)
    return db_path, os.path.basename(image_path)


def _tag_id(conn, name):
    row = conn.execute("SELECT id FROM tags WHERE name=?", (name,)).fetchone()
    if row:
        return row["id"]
    cur = conn.execute("INSERT INTO tags(name) VALUES(?)", (name,))
    return cur.lastrowid


def get_tags(image_path):
    db_path, fname = _resolve(image_path)
    conn = _conn_for(db_path)
    rows = conn.execute(
        "SELECT t.name FROM tags t JOIN image_tags it ON t.id=it.tag_id "
        "WHERE it.filename=? ORDER BY t.name", (fname,)).fetchall()
    return [r["name"] for r in rows]


def set_tags(image_path, tag_names):
    db_path, fname = _resolve(image_path)
    conn = _conn_for(db_path)
    conn.execute("DELETE FROM image_tags WHERE filename=?", (fname,))
    for name in tag_names:
        name = name.strip()
        if name:
            tid = _tag_id(conn, name)
            conn.execute("INSERT OR IGNORE INTO image_tags(filename, tag_id) VALUES(?,?)",
                         (fname, tid))
    conn.commit()


def add_tags(image_path, tag_names):
    db_path, fname = _resolve(image_path)
    conn = _conn_for(db_path)
    for name in tag_names:
        name = name.strip()
        if name:
            tid = _tag_id(conn, name)
            conn.execute("INSERT OR IGNORE INTO image_tags(filename, tag_id) VALUES(?,?)",
                         (fname, tid))
    conn.commit()


def remove_tag(image_path, tag_name):
    db_path, fname = _resolve(image_path)
    conn = _conn_for(db_path)
    conn.execute(
        "DELETE FROM image_tags WHERE filename=? AND tag_id IN "
        "(SELECT id FROM tags WHERE name=?)", (fname, tag_name))
    conn.commit()


def add_tag_with_confidence(image_path, tag_name, confidence):
    """带置信度写标签（QC/AI 打标用），已存在则更新置信度。"""
    db_path, fname = _resolve(image_path)
    conn = _conn_for(db_path)
    tid = _tag_id(conn, tag_name)
    conn.execute(
        "INSERT INTO image_tags(filename, tag_id, source, confidence) VALUES(?,?,?,?) "
        "ON CONFLICT(filename, tag_id) DO UPDATE SET confidence=excluded.confidence",
        (fname, tid, "qc", float(confidence)))
    conn.commit()


def search_by_tag(tag_name, folder=None, recursive=True):
    """按标签搜索。folder 可选；recursive=True 时递归子文件夹的 qltag.db。"""
    if folder is None:
        folder = os.getcwd()
    folder = os.path.abspath(folder)
    hits = []
    for root, dirs, files in os.walk(folder):
        if "qltag.db" not in files:
            if not recursive:
                break
            continue
        db_path = os.path.join(root, "qltag.db")
        conn = _conn_for(db_path)
        rows = conn.execute(
            "SELECT it.filename FROM image_tags it JOIN tags t ON t.id=it.tag_id "
            "WHERE t.name=?", (tag_name,)).fetchall()
        for r in rows:
            hits.append(os.path.join(root, r["filename"]))
        if not recursive:
            break
    return sorted(hits)


def combo_search(tags, folder=None, match="all", recursive=True):
    """多标签组合搜索。match='all'=AND（同时命中全部），'any'=OR（任一命中）。"""
    if folder is None:
        folder = os.getcwd()
    folder = os.path.abspath(folder)
    tags = [t.strip() for t in tags if t and t.strip()]
    if not tags:
        return []
    placeholders = ",".join("?" * len(tags))
    hits = []
    for root, dirs, files in os.walk(folder):
        if "qltag.db" not in files:
            if not recursive:
                break
            continue
        db_path = os.path.join(root, "qltag.db")
        conn = _conn_for(db_path)
        if match == "any":
            rows = conn.execute(
                "SELECT DISTINCT it.filename FROM image_tags it "
                "JOIN tags t ON t.id=it.tag_id "
                f"WHERE t.name IN ({placeholders})", tags).fetchall()
        else:  # all = AND：每图命中的 tag 数 = 目标数
            rows = conn.execute(
                "SELECT it.filename, COUNT(DISTINCT t.id) AS n FROM image_tags it "
                "JOIN tags t ON t.id=it.tag_id "
                f"WHERE t.name IN ({placeholders}) "
                "GROUP BY it.filename HAVING n=?", (tags + [len(tags)])).fetchall()
        for r in rows:
            hits.append(os.path.join(root, r["filename"]))
        if not recursive:
            break
    return sorted(hits)


def tag_stats(folder):
    """该文件夹内每个标签的图片数（按数量降序）。"""
    folder = os.path.abspath(folder)
    db_path = ensure_schema(folder)
    conn = _conn_for(db_path)
    rows = conn.execute(
        "SELECT t.name, COUNT(it.filename) AS n FROM tags t "
        "JOIN image_tags it ON t.id=it.tag_id "
        "GROUP BY t.id ORDER BY n DESC, t.name").fetchall()
    return [{"tag": r["name"], "count": r["n"]} for r in rows]


def export_tags(folder, out_path, fmt="csv"):
    """导出文件夹内全部图片的标签。fmt: 'csv'（filename,tag1,tag2…）/'json'。"""
    folder = os.path.abspath(folder)
    db_path = ensure_schema(folder)
    conn = _conn_for(db_path)
    rows = conn.execute(
        "SELECT it.filename, GROUP_CONCAT(t.name, ',') AS tags "
        "FROM image_tags it JOIN tags t ON t.id=it.tag_id "
        "GROUP BY it.filename ORDER BY it.filename").fetchall()
    if fmt == "json":
        data = {"folder": folder,
                "images": {r["filename"]: r["tags"].split(",") if r["tags"] else []
                           for r in rows}}
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    else:
        with open(out_path, "w", encoding="utf-8-sig", newline="") as f:
            w = csv.writer(f)
            w.writerow(["filename", "tags"])
            for r in rows:
                w.writerow([r["filename"], r["tags"] or ""])
    return {"ok": True, "path": out_path, "images": len(rows)}


def import_tags(folder, in_path, fmt="csv"):
    """从 CSV/JSON 导入标签到文件夹的 qltag.db（已存在的跳过）。"""
    folder = os.path.abspath(folder)
    db_path = ensure_schema(folder)
    conn = _conn_for(db_path)
    added = 0

    def _apply(fname, tags):
        nonlocal added
        for t in tags:
            t = t.strip()
            if not t:
                continue
            tid = _tag_id(conn, t)
            cur = conn.execute(
                "INSERT OR IGNORE INTO image_tags(filename, tag_id) VALUES(?,?)",
                (fname, tid))
            added += cur.rowcount

    if fmt == "json":
        with open(in_path, encoding="utf-8") as f:
            data = json.load(f)
        for fname, tags in data.get("images", {}).items():
            _apply(fname, tags)
    else:
        with open(in_path, encoding="utf-8-sig", newline="") as f:
            for row in csv.reader(f):
                if not row or row[0] == "filename":
                    continue
                _apply(row[0], row[1].split(",") if len(row) > 1 and row[1] else [])
    conn.commit()
    return {"ok": True, "added": added}


# CLI 入口：python qlens_lib.py export|import <folder> <file> <csv|json>
if __name__ == "__main__":
    import sys
    if len(sys.argv) >= 5 and sys.argv[1] == "export":
        print(export_tags(sys.argv[2], sys.argv[3], sys.argv[4]))
    elif len(sys.argv) >= 5 and sys.argv[1] == "import":
        print(import_tags(sys.argv[2], sys.argv[3], sys.argv[4]))
    else:
        print("用法: qlens_lib.py export|import <folder> <file> <csv|json>")


def folder_tags(folder):
    """该文件夹（仅本层）内所有图片用到的标签。"""
    folder = os.path.abspath(folder)
    db_path = ensure_schema(folder)
    conn = _conn_for(db_path)
    rows = conn.execute(
        "SELECT DISTINCT t.name FROM tags t "
        "JOIN image_tags it ON t.id=it.tag_id ORDER BY t.name").fetchall()
    return [r["name"] for r in rows]


def list_folder(folder, recursive=False, image_exts=None):
    """列出文件夹图片（含标签）。recursive=True 递归子文件夹。"""
    if image_exts is None:
        image_exts = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif",
                      ".cr2", ".cr3", ".nef", ".arw", ".dng", ".rw2"}
    folder = os.path.abspath(folder)
    ensure_schema(folder)
    result = []
    walker = os.walk(folder) if recursive else [(folder, [], os.listdir(folder))]
    for root, _dirs, files in walker:
        db_path = ensure_schema(root)
        conn = _conn_for(db_path)
        for fname in sorted(files):
            if os.path.splitext(fname)[1].lower() in image_exts:
                rows = conn.execute(
                    "SELECT t.name FROM tags t JOIN image_tags it ON t.id=it.tag_id "
                    "WHERE it.filename=? ORDER BY t.name", (fname,)).fetchall()
                tags = [r["name"] for r in rows]
                result.append({"path": os.path.join(root, fname), "name": fname, "tags": tags})
    return result
