#!/usr/bin/env python3
"""QLens MCP Server —— 把 QLens 图库操作暴露给任何 MCP 客户端。

用法：
  python server.py            # stdio 模式（默认，MCP 客户端用）
  python server.py --tool qlens_list_folder --arg folder=D:/Pics   # 自测

前置条件：QLens Manager 必须正在运行（MCP 是 Manager 的外接接口）。
危险操作（delete_files）在工具描述里显式警告，配合客户端确认流。
"""
import argparse
import os
import shutil
import subprocess
import sys

from mcp.server.fastmcp import FastMCP

import qlens_lib
import qc_detect


def manager_running() -> bool:
    """检查 qlens_manager.exe 是否在运行（Windows）。"""
    if sys.platform == "win32":
        try:
            result = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq qlens_manager.exe"],
                capture_output=True, text=True, timeout=5)
            return "qlens_manager.exe" in result.stdout
        except Exception:
            return False
    # 非 Windows：查同名进程
    try:
        result = subprocess.run(["pgrep", "-x", "qlens_manager"],
                                capture_output=True, text=True, timeout=5)
        return result.returncode == 0
    except Exception:
        return False


mcp = FastMCP("qlens")


@mcp.tool()
def qlens_list_folder(folder: str) -> list[dict]:
    """列出文件夹内所有图片及它们已打的标签。folder: 绝对路径目录。"""
    return qlens_lib.list_folder(folder)


@mcp.tool()
def qlens_search_tag(tag: str, folder: str = "") -> list[str]:
    """按标签搜索图片，返回绝对路径列表。tag: 标签名；folder: 可选限定目录。"""
    return qlens_lib.search_by_tag(tag, folder or None)


@mcp.tool()
def qlens_get_tags(image_path: str) -> list[str]:
    """查询单张图片的标签列表。image_path: 图片绝对路径。"""
    return qlens_lib.get_tags(image_path)


@mcp.tool()
def qlens_set_tags(image_path: str, tags: list[str]) -> dict:
    """全量设置图片标签（替换原有标签）。tags: 标签名数组，传空数组清空标签。"""
    qlens_lib.set_tags(image_path, tags)
    return {"ok": True, "image": image_path, "tags": qlens_lib.get_tags(image_path)}


@mcp.tool()
def qlens_add_tags(image_path: str, tags: list[str]) -> dict:
    """增量添加标签（已有标签忽略，不覆盖）。"""
    qlens_lib.add_tags(image_path, tags)
    return {"ok": True, "image": image_path, "tags": qlens_lib.get_tags(image_path)}


@mcp.tool()
def qlens_folder_tags(folder: str) -> list[str]:
    """列出该文件夹内所有图片已用到的标签（去重），用于筛选候选。"""
    return qlens_lib.folder_tags(folder)


@mcp.tool()
def qlens_move_files(moves: list[dict]) -> dict:
    """移动/归档文件。moves: [{"src": 源绝对路径, "dst": 目标绝对路径}]。
    目标目录不存在会自动创建。移动后标签从旧 qltag.db 迁到新 qltag.db。"""
    qlens_lib.ensure_schema(os.path.dirname(os.path.abspath(os.getcwd())))
    done, failed = [], []
    for mv in moves:
        src, dst = mv.get("src"), mv.get("dst")
        if not src or not dst:
            failed.append({"src": src, "reason": "missing src/dst"})
            continue
        try:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            tags = qlens_lib.get_tags(src)
            shutil.move(src, dst)
            if tags:
                qlens_lib.set_tags(dst, tags)     # 新 qltag.db 写入
                qlens_lib.set_tags(src, [])       # 旧 qltag.db 清空（src 已移动，仅清记录）
            done.append({"src": src, "dst": dst})
        except Exception as e:  # noqa: BLE001
            failed.append({"src": src, "dst": dst, "reason": str(e)})
    return {"ok": not failed, "moved": done, "failed": failed}


@mcp.tool()
def qlens_rename_files(renames: list[dict]) -> dict:
    """重命名文件。renames: [{"src": 绝对路径, "new_name": "新文件名含扩展名"}]。
    标签从旧名迁到新名（同一 qltag.db）。"""
    qlens_lib.ensure_schema(os.path.dirname(os.path.abspath(os.getcwd())))
    done, failed = [], []
    for rn in renames:
        src, new = rn.get("src"), rn.get("new_name")
        if not src or not new:
            failed.append({"src": src, "reason": "missing src/new_name"})
            continue
        try:
            dst = os.path.join(os.path.dirname(src), new)
            tags = qlens_lib.get_tags(src)
            os.rename(src, dst)
            if tags:
                qlens_lib.set_tags(dst, tags)
                qlens_lib.set_tags(src, [])
            done.append({"src": src, "dst": dst})
        except Exception as e:  # noqa: BLE001
            failed.append({"src": src, "reason": str(e)})
    return {"ok": not failed, "renamed": done, "failed": failed}


@mcp.tool()
def qlens_delete_files(paths: list[str]) -> dict:
    """⚠️⚠️ 危险操作：永久删除文件，不可恢复！
    调用前必须向用户确认：1) 文件确实不再需要；2) 用户明确同意删除。
    如果只是整理，请优先用 qlens_move_files 移动到回收目录。
    paths: 要删除的文件绝对路径数组。"""
    done, failed = [], []
    for p in paths:
        try:
            os.remove(p)
            qlens_lib.set_tags(p, [])   # 清理标签记录
            done.append(p)
        except Exception as e:  # noqa: BLE001
            failed.append({"path": p, "reason": str(e)})
    return {"ok": not failed, "deleted": done, "failed": failed}


@mcp.tool()
def qlens_analyze(folder: str, task: str = "qc", recursive: bool = False,
                  confidence: float = 0.5) -> dict:
    """✅ 推荐工具：批量图片质量检测打标（QC）。
    当用户要求分析/质检一批图片时，必须用本工具，不要逐张读图或让用户上传原图。
    本工具在程序内部用 OpenCV 算法检测（不出本机、零 token、零 API 配置），
    检测结果直接写入 qltag.db。
    folder: 目标文件夹绝对路径；
    task: 当前仅 "qc"（模糊/曝光过度/曝光不足/色偏/红眼/闭眼）；
    recursive: 是否递归子文件夹；
    confidence: 置信度阈值 0~1（默认 0.5，低于此不写标签）。"""
    results = qc_detect.analyze_folder(folder, recursive=recursive)
    written, skipped = 0, 0
    for item in results:
        tags = [t for t, conf in item["qc"].items() if conf >= confidence]
        if tags:
            # 逐标签写库（保留置信度）
            for t in tags:
                qlens_lib.add_tag_with_confidence(item["path"], t, item["qc"][t])
            written += 1
        else:
            skipped += 1
    return {"ok": True, "folder": folder, "task": task,
            "analyzed": len(results), "tagged": written, "skipped": skipped}


def self_test():
    """非 MCP 自测：直接调用工具函数验证协议逻辑。"""
    test_tag = "__selftest__"
    test_dir = os.path.join(os.environ.get("TEMP", "."), "qlens_mcp_selftest")
    os.makedirs(test_dir, exist_ok=True)
    test_file = os.path.join(test_dir, "probe.png")
    with open(test_file, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"\0" * 64)

    # 打标签 → 查询 → 搜索 → 清空
    qlens_lib.set_tags(test_file, [test_tag])
    assert qlens_lib.get_tags(test_file) == [test_tag], "set/get 失败"
    hits = qlens_lib.search_by_tag(test_tag, folder=test_dir)
    assert test_file in hits, "search 失败"
    qlens_lib.remove_tag(test_file, test_tag)
    assert qlens_lib.get_tags(test_file) == [], "remove 失败"

    # 归档：移动到子目录 → 标签跟随
    sub = os.path.join(test_dir, "archive")
    qlens_lib.set_tags(test_file, [test_tag])
    moved = os.path.join(sub, "probe.png")
    os.makedirs(sub, exist_ok=True)
    shutil.move(test_file, moved)
    qlens_lib.set_tags(moved, [test_tag])
    qlens_lib.set_tags(test_file, [])
    assert test_tag in qlens_lib.get_tags(moved), "归档后标签未跟随"

    qlens_lib.close_all()
    shutil.rmtree(test_dir)
    print("SELF-TEST OK")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", help="自测单个工具")
    parser.add_argument("--selftest", action="store_true", help="跑完整自测")
    args = parser.parse_args()

    if args.selftest:
        self_test()
        sys.exit(0)

    # MCP 绑定 Manager 生命周期：Manager 未启动则拒绝连接
    if not manager_running():
        sys.stderr.write(
            "QLens Manager 未启动。请先打开 QLens Manager，再连接 MCP 服务。\n")
        sys.exit(1)

    mcp.run()
