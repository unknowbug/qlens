# QLens 构建与发布

## 系统要求（运行）

- **Windows 10 1809+**（QuickView 用 Per-Monitor V2 DPI；1809 之前不支持）
- HDR 功能：HDR 显示器 + Windows HDR 开关（`设置 → 系统 → 屏幕 → HDR`）
- HEIC/AVIF：`plugins/` 内有 heic 插件（libheif）或系统 WIC HEIF 扩展
- RAW（CR2/CR3/NEF/ARW/DNG/RW2）：Manager 内置（LibRaw）

## 依赖

| 依赖 | 用途 | 提供方 |
|---|---|---|
| Qt 6 (Core/Gui/Widgets/Sql/Concurrent) | Manager + qlens_core | 系统安装（CMake find_package） |
| LibRaw | RAW 解码 | `find_package(libraw)` |
| lcms2 | 色彩管理 | `find_package(lcms)` |
| Windows SDK | WIC / D3D11 / DXGI / shlwapi | 系统 |
| Python 3 | MCP Server + 导入导出 CLI | 可选（仅 MCP/导入导出需要） |

## 构建

### 主运行根：`build-qv\Release`

**主运行根 = `build-qv\Release`**——两 exe + Qt Release DLL + 解码插件 + language/icons/docs/mcp 都在这，开发测试、打包、安装器都从这里取（以后统一从这开始）：

```
build-qv\Release\
├── qlens_quickview.exe / qlens_manager.exe / qlens_core.dll
├── Qt6*.dll（windeployqt Release）+ platforms\ + imageformats\ 等
├── raw_r.dll / z.dll / lcms2-2.dll / D3Dcompiler_47.dll / dxcompiler.dll / dxil.dll
├── plugins\（qlens_heic.dll + qlens_svg.dll + libheif 依赖）
├── language\{zh,en}\  icons\  docs\  mcp\  LICENSE  README*.md
```

- QuickView 构建到 `build-qv\Release\qlens_quickview.exe`（`cmake -S src/quickview -B build-qv`）
- Manager 构建到 `build\bin\Release\`（CMake 自动部署 qlens_core/raw_r/z）→ **复制到 build-qv\Release**
- `windeployqt build-qv\Release\qlens_manager.exe` 补齐 Qt Release DLL（增量/幂等）
- **程序移动后**：Manager 启动自动检测注册表关联路径，不一致则静默重注册（防空白右键菜单）

### 打包

```bat
rem 1. 保证 build-qv\Release 是最新运行集（构建 + 复制 + windeployqt）
rem 2. pack.ps1：复制运行集 → dist\QLens-0.2.2\ + zip（103 文件）
powershell -ExecutionPolicy Bypass -File pack.ps1
rem 3. Inno Setup：setup.exe（安装写关联 + 卸载自动清理）
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" QLens.iss
```

### Manager + qlens_core（Qt 部分）

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:\Qt\6.11.1\msvc2022_64
cmake --build build --config Debug --target qlens_manager
```

目标：
- `qlens_core`（SHARED）——解码/插件/i18n 共享库
- `qlens_manager`（WIN32 exe）——管理器

### QuickView（原生 Win32，独立构建）

```bat
cmake -S src/quickview -B build-qv -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:\Qt\6.11.1\msvc2022_64
cmake --build build-qv --config Release
```

（QuickView 用 WIC/D3D11，实际不依赖 Qt——独立目录便于发布轻量版。）

## 发布目录结构

```
qlens/
├── qlens_quickview.exe      # 极速看图器（原生）
├── qlens_manager.exe        # 管理器（Qt）
├── qlens_core.dll           # 共享库（Manager 依赖）
├── Qt6*.dll                 # Qt 运行时（Manager 依赖，windeployqt 部署）
├── opengl32sw.dll           # Qt 软件渲染回退（可选）
├── plugins/                 # 解码插件（heic/svg...）
├── mcp/                     # MCP Server（server.py + qlens_lib.py + qc_detect.py）
├── docs/                    # 文档（含 QLENS_TAG_PROTOCOL.md）
├── language/zh/qlens_quickview.po
├── language/en/qlens_quickview.po
├── language/zh/qlens_manager.po
├── language/en/qlens_manager.po
├── qlens_config.ini         # 可选：language=zh/en（便携模式；否则写 %APPDATA%/QLens/）
└── docs/                    # 文档
```

- **Qt DLL 部署**：`windeployqt qlens_manager.exe`（自动复制 Qt6Core/Gui/Widgets/Sql/Concurrent + 平台插件）。
- **QuickView 不需要 Qt**——发布时可只带 qlens_quickview.exe + plugins/ + language/。
- **便携模式**：exe 旁可写 `qlens_config.ini` 时配置写 exe 旁；否则写 `%APPDATA%/QLens/`。
- **崩溃日志**：`%APPDATA%/QLens/crash.log`（QuickView）。

## 语言（i18n）

- 语言文件：`language/<Lang>/<app>.po`（`qlens_quickview.po` / `qlens_manager.po`）
- 配置：`qlens_config.ini` 的 `[General] language=zh|en`（Manager「设置 → 语言」写入）
- 默认跟随系统语言，无匹配回退中文
- 加新语言：复制 po 模板翻译，放到对应 `language/<Lang>/` 目录

## 卸载清理（文件关联）

「设置 → 注册为默认看图器」写入的都是 **HKCU 用户级**键，卸载时应删除：

```
HKCU\Software\Classes\QLensQuickView                    （ProgID 整棵）
HKCU\Software\Classes\Applications\qlens_quickview.exe  （整棵）
HKCU\Software\Classes\.jpg\OpenWithProgids\QLensQuickView  （每个已注册扩展的值）
  ...（.jpeg .png .gif .bmp .webp .heic .heif .avif .jxr .wdp .svg .tif .tiff 同理）
```

注意：**只删 `QLensQuickView` 这个值**，不要删 `OpenWithProgids` 键本身（可能有其他程序的条目）。删除后调 `SHChangeNotify(SHCNE_ASSOCCHANGED)` 刷新。

## 安装包（Inno Setup）

正式发布用 **Inno Setup** 生成 setup.exe（替代/补充 zip）——**卸载时自动清理全部注册关联，不留空白右键菜单**（已实测：装→注册关联写入→卸载→ProgID/RegisteredApplications/OpenWithProgids 全部清空、文件删除）。

```bat
rem 1. 装 Inno Setup（winget install JRSoftware.InnoSetup，或官网 GitHub release）
rem 2. 编译（需要先跑 pack.ps1 生成 dist\QLens-0.2.2\）
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" QLens.iss
rem 产出：dist\QLens-0.2.2-setup.exe
```

- `QLens.iss`：安装写文件关联（与 Manager 注册逻辑一致，`uninsdeletekey/uninsdeletevalue` 卸载自动清理）；HKCU 用户级——免管理员
- 简体中文语言文件 `ChineseSimplified.isl` 需放入 Inno Setup 的 `Languages\`（6.7.x 官方包未自带）
- 用户级安装（`PrivilegesRequired=lowest`）——程序移到新路径后 Manager 启动会自动重注册关联

## 插件

- 解码插件放 `plugins/`（DLL）——见[插件开发指南](06-plugin-dev.md)
- 现有：HEIC/AVIF（libheif）、SVG

