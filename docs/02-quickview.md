# QLens QuickView 手册

原生 Win32 + D3D11 极速看图器。**无 Qt 依赖，启动快、内存低。**

## 启动

| 方式 | 说明 |
|---|---|
| 双击图片 | 文件关联注册后（Manager「设置 → 注册为默认看图器」或首次启动自动注册） |
| 拖入图片 | 从资源管理器拖文件到窗口 |
| 命令行参数 | `qlens_quickview.exe 图片路径`（第一个非 `-` 参数）；`--monitor N` 指定显示器索引 |

- 双击图片会启动同目录 `qlens_manager.exe` 打开所在文件夹并自关闭。
- 无参数启动：打开空窗口 + 注册文件关联 + 加载插件 + 按语言设置初始化。

## 键盘快捷键

| 按键 | 功能 |
|---|---|
| `Esc` | 退出 |
| `←` / `↑` | 上一张 |
| `→` / `↓` / `Space` | 下一张 |
| `F` | 100% 原尺寸 |
| `S` | 适配窗口 |
| `Ctrl+滚轮` | 缩放（×1.25 / ×0.8，下限 0.05） |
| `Ctrl+C` | 复制当前图（路径 + 文件 + 位图三合一入剪贴板） |
| `Q` / `E` | 左旋 / 右旋 90° |
| `Del` | 删除当前图到回收站 |
| `Ctrl+Shift+S` | 另存为（PNG/JPEG） |
| `F12` | 调试信息开关（显示 FMT/IMG/WIN/ZOOM/ROT/PAN/MON/HDR/PEAK；普通模式禁拖窗口，F12 后允许） |

## 鼠标操作

| 操作 | 功能 |
|---|---|
| 滚轮（无 Ctrl） | 翻页：上滚 = 上一张，下滚 = 下一张 |
| `Ctrl+滚轮` | 缩放 |
| 拖入文件 | 打开新图片 |
| 双击 | 打开 Manager（同目录） |
| 左键拖动（放大后） | 平移画布 |
| 点击底部缩略图 | 跳转到该图 |
| 右键 | 菜单：左旋转/右旋转/放大/缩小/复制/另存为/删除 |

## 屏幕按钮

- **右上角**：关闭按钮（常驻）
- **底部中央**：上一张 / 下一张 / Manager 三个按钮——鼠标移动时显示，2 秒无动作自动隐藏

## 支持的图片格式

- **内置过滤**：`.jpg .jpeg .png .webp .bmp .gif .tif .tiff .svg .heic .heif .avif .jxr .wdp`
- **解码插件**（exe 旁 `plugins/` 目录）：
  - HEIC：`heic/heif/avif/heifs`（libheif 动态加载，含 8bit/16bit）
  - SVG：`svg`（含文件头签名识别）
- **解码顺序**：插件优先（扩展名 + 文件头签名）→ WIC 兜底；高位深源转 RGBA16F

## HDR 能力

| 场景 | 行为 |
|---|---|
| 真 HDR 图（16bit+）在 HDR 屏 | **物理直通**：scRGB 线性，高光 >1.0（>80nit 真实亮度），不做自适应 tone map |
| SDR 图在 HDR 屏 | **SDR→HDR 增强**：sRGB→linear→×peakNit→PQ 编码；高光>0.7 软压缩、亮图按高光比例降增益 |
| 真 HDR 图在 SDR 屏 | **HDR→SDR 回退**：Reinhard tone map（v/(1+v)）转 8bit，防黑屏 |
| UI（缩略图条/按钮） | 始终保持 SDR 亮度 |

- HDR 检测：DXGI 枚举显示器——HDR10 色彩空间（G2084/P2020）或 MaxLuminance>100 判定 HDR 屏。
- 显示器色彩空间：scRGB（G10）物理线性。

## 缩略图导航条

- 底部深色条，当前图蓝框高亮 + 整体提亮，其余压暗
- 自动居中滚动，窗口 resize 后重新居中
- 后台线程异步生成（低于正常优先级，不卡 UI），螺旋顺序（当前→右→左）
- 只绘制完整缩略图（被窗口切掉的半张不显示）
- HDR 模式下 FP16 半精度缓冲绘制

## 其他

- **GIF 动画**：自动播放
- **EXIF Orientation**：自动旋转
- **崩溃日志**：`%APPDATA%/QLens/crash.log`
- **Per-Monitor V2 DPI** 感知
- **语言**：`qlens_config.ini` 的 `language` → `language/<Lang>.po`（默认中文）
- **文件关联**：启动时写 HKCU `OpenWithProgids`：`.jpg .jpeg .png .webp .bmp .gif .svg`

## 系统要求

- Windows 10 1809+（Per-Monitor V2 DPI 需要 1809+）
- HDR 功能需要 HDR 显示器（Windows HDR 开关开启）
- HEIC/AVIF：需要 `plugins/` 内有 heic 插件（libheif）或系统 WIC HEIF 扩展
