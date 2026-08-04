# QLens 插件开发指南

QLens 的解码插件体系：**未来新的图片格式出现，大家可以通过插件 DLL 直接支持**，无需改 QLens 本体。当前已实现：HEIC/AVIF（libheif）、SVG。

## 插件定位

- 插件是 **DLL**，放在 exe 同目录 `plugins/` 下，启动时自动加载。
- 每插件注册**一个或多个扩展名**（如 `heic|heif|avif|heifs`）。
- 解码顺序：**插件优先**（扩展名 + 文件头签名匹配）→ WIC 兜底。
- QuickView 与 Manager（经 qlens_core）共享同一套插件接口。

## 接口定义

### 入口

```c
extern "C" __declspec(dllexport) void qlens_plugin_entry(RegApi *api);
```

加载器用 `GetProcAddress(h, "qlens_plugin_entry")` 找到入口，传入 `RegApi`：

```c
typedef struct RegApi {
    void (*regDecoder)(const DecodePlugin *plugin);
} RegApi;
```

### 注册描述

```c
typedef struct DecodePlugin {
    const char *ext;        // 小写扩展名，多个用 | 分隔："heic|heif|avif"
    const char *signature;  // 文件头签名（十六进制字节表），如 "3C3F786D6C"（= "<?xm"）；空 = 只认扩展名
    int  (*query)(const char *path, DecodeInfo *info);   // 查询：格式/尺寸/帧数/元数据
    int  (*decode)(const char *path, int frame, int targetW, int targetH, ImageBuffer *out);  // 解码
} DecodePlugin;
```

### 查询结果

```c
typedef struct DecodeInfo {
    int width, height;        // 建议宽高（已考虑 EXIF 旋转）
    int frames;               // 帧数（GIF/动图）
    int vector;               // 是否矢量（SVG）
    int hasAlpha;             // 是否带 alpha
    int exifRot;              // EXIF Orientation
    int hasICC;               // 是否带 ICC 色彩配置
    int peakNit;              // 峰值亮度（HDR）
    int transfer;             // 传递函数：sRGB/PQ/HLG/linear
    int error;                // 错误码：OK/NOT_SUPPORTED/CORRUPT/IO_ERROR
    // ...（详见 plugins.h）
} DecodeInfo;
```

### 解码输出

```c
typedef struct ImageBuffer {
    void  *pixels;            // 像素数据（BGRA8 或 RGBA16F）
    int    width, height;
    int    stride;            // 行字节数
    int    format;            // 像素格式枚举
    // ...
} ImageBuffer;
```

## 完整示例（HEIC 插件骨架）

```c
#include "plugins.h"

static int heic_query(const char *path, DecodeInfo *info) { /* libheif 查询 */ }
static int heic_decode(const char *path, int frame, int tw, int th, ImageBuffer *out) {
    /* libheif 解码 → BGRA8/RGBA16F 填 out */
}

static DecodePlugin g_heic = {
    "heic|heif|avif|heifs",   // 注册 4 种格式
    "",                        // 无文件头签名（HEIC 有专用容器头，走扩展名）
    heic_query, heic_decode
};

extern "C" __declspec(dllexport) void qlens_plugin_entry(RegApi *api)
{
    api->regDecoder(&g_heic);
}
```

## 编译要求

- **64 位** Windows DLL（与 QLens exe 同架构）。
- 导出符号必须**精确匹配**（`__declspec(dllexport)` + C 链接 `extern "C"`）。
- 动态加载第三方库（如 libheif）用 `LoadLibrary` + `GetProcAddress`，**不静态链接**——发布体积小、依赖可控。

## 开发注意事项（踩过的坑）

1. **结构体返回**（x64 ABI）：函数返回结构体时用隐藏指针返回——函数指针声明必须与结构体精确匹配，声明错（如返回 `int`）会**栈错位崩溃**。
2. **枚举值**：第三方库枚举（如 `heif_colorspace_RGB=1`、`heif_chroma_interleaved_RGBA=11`）必须查准，传错返回 `Unsupported_feature`。
3. **尺寸 API**：有些库的宽度查询对 interleaved 图返回 -1，改用 handle 级 API。
4. **文件头签名**：`signature` 支持只靠内容识别（不认扩展名）的格式——改名文件也能打开。

## 格式图标约定（插件）

QLens 的格式图标读取协议（Manager 缩略图网格）：

1. **优先读 `icons/<EXT>.ico`**（exe 旁文件夹，`EXT` 为大写，如 `icons/JPG.ico`）——**插件开发者只要把图标放到这个位置，Manager 自动显示，无需改代码/重新编译**
2. 兜底：编译进 exe 的 qrc 资源（内建格式用）

- **内建格式**：图标已编译进 exe（qrc）
- **插件格式**：插件随附 `icons/<EXT>.ico`（在 QLens 安装目录的 icons/ 下）即可——示例：`.foo` 格式 → `icons/FOO.ico`（建议 16/32/48/256 多尺寸 ICO）

**可选（更完整）**：插件 DLL 内嵌 ICON 资源（资源 ID 0 = 主格式图标）——Windows 资源管理器的 `DefaultIcon` 可指向插件 DLL（`"C:\...\plugins\foo.dll",0`），文件类型图标与解码器一体分发。

## 测试

- 把 DLL 放到 `build-qv/Release/plugins/`（QuickView 同目录），启动 QuickView 打开对应格式图片。
- Manager（qlens_core）在相同 `plugins/` 目录加载——缩略图与查看器同时生效。
