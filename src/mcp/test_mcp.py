"""MCP stdio 客户端集成测试：连接 server.py，调用工具验证协议。"""
import asyncio
import os
import shutil
import tempfile

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    # 准备测试图片
    test_dir = tempfile.mkdtemp(prefix="qlens_mcp_test_")
    img = os.path.join(test_dir, "a.jpg")
    with open(img, "wb") as f:
        f.write(b"\xff\xd8\xff\xe0" + b"\0" * 128)

    params = StdioServerParameters(
        command="python",
        args=[os.path.join(os.path.dirname(__file__), "server.py")],
        cwd=os.path.dirname(__file__),
    )

    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            # 列工具
            tools = await session.list_tools()
            names = [t.name for t in tools.tools]
            print("tools:", names)
            assert "qlens_set_tags" in names, "工具未注册"

            # 打标签 → 查标签 → 搜索
            r1 = await session.call_tool("qlens_set_tags", {"image_path": img, "tags": ["test_tag"]})
            print("set:", r1.content[0].text if r1.content else r1)
            r2 = await session.call_tool("qlens_get_tags", {"image_path": img})
            print("get:", r2.content[0].text if r2.content else r2)
            assert "test_tag" in (r2.content[0].text if r2.content else ""), "get 失败"

            r3 = await session.call_tool("qlens_search_tag", {"tag": "test_tag", "folder": test_dir})
            print("search:", r3.content[0].text if r3.content else r3)

            # 清理图片（qltag.db 待 server 退出后释放）
            await session.call_tool("qlens_delete_files", {"paths": [img]})

    # session 已退出，server 进程连接释放，可删临时目录
    shutil.rmtree(test_dir, ignore_errors=True)
    print("MCP INTEGRATION OK")


if __name__ == "__main__":
    asyncio.run(main())
