# QLens 更新日志

## 0.2.2（2026-08-05）

- **首次启动现场写默认配置**：config 不存在时用默认配置启动并自动生成 `qlens_config.ini`（默认语言跟随系统）；打包不再内置 config 文件
- **崩溃日志机制**：共享 crashlog 模块（版本/系统/异常码/调用栈+模块偏移 → `%APPDATA%\QLens Manager\crash.log`），启动检测到上次崩溃时提示日志路径
- **QC 检测 emoji 角标立即显示**：修复三连坑——检测完成后重扫重载、WAL 跨连接读不到新写入、`isCanceled()` 多次调用不一致
- **大目录卡顿修复**：缩略图扫描主线程零 SQLite、零 PNG 编码（全部移 worker 线程，批量预查 QC 标签）
- **缩略图尺寸策略**：滑块 32~480、标准生成尺寸 320（缓存键含 size，旧缓存自动失效）
- **状态栏滑块**：网格态调缩略图大小、查看态调缩放（Ctrl+滚轮联动；滚轮缩放从当前显示比例连续起算，不跳 100%）
- **Manager 布局持久化加固**：dock/窗口状态 + 内部 splitter 比例自动保存到 config；崩溃自愈（trusted 标记，布局恢复异常自动重建默认布局）
- **修复**：布局恢复后窗口不显示、ThumbnailCache SQLite 跨线程闪退、滑块模式切换互相污染（setRange clamp 误发 valueChanged）
- **i18n**：语言无匹配一律回退英文；Manager 语言切换自动重启生效

## 0.2.1（2026-08-04）

- **修复 Manager 缩略图交互 BUG 群**：单击蓝框、双击、框选、多选标签聚合
- **README**：新增 HDR 亮度测试章节 + 测试图（`testdata/hdr/hdr_range_test.jxr`，0–10000nit 亮度块，测显示器真实 HDR 峰值）

## 0.2.0（2026-07-xx）

- 首个公开打包版本（功能交付见 README 总览）
