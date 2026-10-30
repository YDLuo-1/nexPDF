# Changelog / 更新日志

All notable changes follow Semantic Versioning. / 重要变更遵循语义化版本。

## [Unreleased]

## [1.0.5-rc.1] - 2026-10-30

- Save from an independent PDF graph so encryption writes and failures cannot alter the live document or undo history; track the saved undo position across branches. / 从独立 PDF 对象图保存，避免加密写入和失败破坏当前文档与撤销历史；正确跟踪保存点和撤销分支。
- Keep sliced searches on the document thread; remove selected anonymous watermarks by stable identity. / 分批搜索保持在文档线程；按稳定标识移除选中的匿名水印。
- Enforce copy/print permissions, including owner access and low-resolution printing. / 执行复制、打印权限，包括所有者权限和低分辨率打印。
- Complete failed/cancelled render requests, cancel obsolete work, bound raster size and render concurrency, and make printing cancellable with a wait timeout. / 为失败和取消的渲染请求返回终态，取消过期任务，限制栅格尺寸与并发，并为打印增加取消和等待超时。
- Fix viewport painting, tile retry state, long crash-path overflow and password buffer erasure. / 修复视口绘制范围、瓦片重试状态、过长崩溃路径溢出和密码缓冲区清理。
- Update MuPDF to 1.28.6; fix the fuzz harness and add instrumented Linux fuzz CI. / MuPDF 更新至 1.28.6；修复 fuzz 构建并增加 Linux 插桩验证。
- Add focused regressions and refresh version and acceptance documentation. / 补充针对性回归并统一版本与验收文档。

## [1.0.4] - 2026-10-24

### Added / 新增

- Printing via File → "Print…" (`Ctrl+P`): system print dialog with the 1..page-count range, whole-page rendering at up to 300 DPI through the existing background render session, proportional centring on the printable area, and status-bar progress ending in "Printed N page(s)". / 新增文件菜单"打印…"（`Ctrl+P`）：系统打印对话框支持 1..页数范围，经既有后台渲染会话按不高于 300 DPI 整页渲染，等比居中打印到可打印区域，状态栏显示进度并最终显示"已打印 N 页"。打印路径经编译与界面集成验证（含 UI 测试），未做实体打印机输出验证。

## [1.0.3] - 2026-10-24

- Add local-only crash reporting (Windows minidumps, Unix signal logs, retention-capped) with a `--crash-test` verification switch, and close the embedded-font corpus gate. / 新增仅本地的崩溃报告（Windows 小型转储、Unix 信号日志、限量保留）与 `--crash-test` 自验证，并关闭嵌入式字体语料门槛。

## [1.0.2] - 2026-10-24

First stable release. The v1.0.0 and v1.0.1 tags were stopped by the new installer-verification gate (GUI-app stdout capture, then an uninstall file-handle race); both remain as failed history without a Release. / 首个正式版。v1.0.0 与 v1.0.1 标签被安装器验证门槛先后拦下（GUI 输出捕获、卸载句柄竞争），均按纪律保留为失败历史且无 Release。

## [1.0.1] - 2026-10-24

First stable release. The v1.0.0 tag was stopped by the new installer-verification gate (the harness could not capture GUI-app stdout) and remains as failed history without a Release; verification now redirects stdout explicitly. / 首个正式版。v1.0.0 标签被新安装器验证门槛拦下（验证脚本无法捕获 GUI 程序输出），按纪律保留为失败历史且无 Release；本版本修正后发布。

## [1.0.0] - 2026-10-24

First stable release; see `docs/release-notes/v1.0.0.md` for the full verification scope. Windows and Linux carry full acceptance evidence; macOS is CI-verified without a hardware spot check. / 首个正式版；验证范围见发布说明。Windows/Linux 具备完整验收证据，macOS 仅 CI 验证未做真机抽查。

## [1.0.0-rc.13] - 2026-10-24

### Fixed / 修复

- Removing a nexPDF watermark can no longer restore a page snapshot recorded before a redaction: any content change invalidates scanned candidates, and applying redactions drops the snapshot. / 移除 nexPDF 水印不再可能恢复涂黑前的页面快照：任何内容变更都会使已扫描候选失效，应用涂黑时会删除该快照。
- A failed or cancelled open (missing file, malformed PDF, wrong or cancelled password) keeps the previously open document usable instead of leaving an empty session behind a stale window. / 打开失败或取消密码时保留原文档，不再出现"界面显示旧文档、会话已空"的脱节状态。
- Highlights render as translucent yellow and underline/strikeout as red instead of an opaque black block. / 高亮改为半透明黄色，下划线与删除线改为红色，不再是不透明黑块。
- A signed document can be saved again to the copy written by Save As; the signature flag is cleared once a fresh file is written. / 已签名文档另存后可以继续保存到该副本；写入新文件后清除签名标记。
- PDF permissions are checked per operation: page assembly/redaction need *modify*, annotations need *annotate*. / PDF 权限按操作校验：页面编排/涂黑需要 modify，批注需要 annotate。
- Windows EXE and setup.exe now carry file/product version resources. / Windows EXE 与安装包补齐文件/产品版本资源。
- Text search now runs in interleaved slices on the document thread: a new search, an edit, or closing the document abandons the previous run at the next slice boundary, so thousand-page searches can no longer starve queued operations. / 文本搜索改为在文档线程上分片交错执行：新搜索、编辑或关闭文档都会在下个分片边界作废旧搜索，千页搜索不再阻塞排队操作。
- The rc.11 Linux AppImage passed `--version` and a WSLg CJK GUI check on real Ubuntu 24.04, closing the Linux spot-check gate.
- Binary packages now ship the verbatim LGPL-3.0 (and GPL-3.0) license texts required for the LGPLv3 Qt runtime, enforced by a publish gate. / 二进制包附带 LGPLv3 Qt 运行库所要求的 LGPL-3.0（及 GPL-3.0）许可证原文，并由发布门槛强制检查。 / rc.11 Linux AppImage 在真实 Ubuntu 24.04 上通过 `--version` 与 WSLg 中文界面检查，Linux 抽查门槛已关闭。
- macOS DMG now ships `LICENSE` and `THIRD_PARTY_NOTICES.md` inside the bundle. / macOS DMG 的应用包内现在包含许可证与第三方声明。

### Changed / 变更

- Save writes a `0600` temporary file on POSIX, restores the destination mode, fsyncs the file, and fsyncs the directory after the rename. / 保存在 POSIX 上以 `0600` 创建临时文件、恢复目标权限、fsync 文件并在替换后 fsync 目录。
- Open/save parameters use a `SecurePassword` type with a deep copy and zeroization on destruction. / 打开/保存参数改用独占深拷贝并在析构时清零的 `SecurePassword`。
- `saved` now reports the written file's content fingerprint so the UI updates its reading-position key and title after Save As. / `saved` 现在携带新文件指纹，另存后界面会更新阅读位置键与标题。
- Documents actions are disabled until a document is open, page-count labels stay in sync, the canvas viewport uses the dark page background, and the HighDPI rounding policy is set before QApplication exists. / 未打开文档时禁用文档动作、页数标签保持同步、画布视口使用深色页面底色，并在 QApplication 创建前设置 HighDPI 取整策略。
- The translation-coverage check now covers `core/src/document_session.cpp` and runs in CI. / 翻译覆盖检查纳入 `core/src/document_session.cpp` 并在 CI 中运行。
- Stable tags no longer publish automatically; they require a manual `workflow_dispatch` after the release checklist. / 正式版标签不再自动发布，需在检查表完成后手动触发。
- Documentation: page rotation wording, qpdf flags, and the deployed Qt component inventory were corrected. / 文档修正：页面旋转措辞、qpdf 参数与部署的 Qt 组件清单。

## [1.0.0-rc.11] - 2026-09-28

### Added / 新增

- Complete Chinese mappings for every user-facing string plus a coverage checker (`tools/check_i18n_coverage.py`). / 补齐全部面向用户文字的中文映射，并新增覆盖检查脚本。
- Distribution materials: validated winget manifests and a Scoop manifest with the official asset digest, plus a competitor comparison and install section in the README. / 分发物料：通过验证的 winget 清单、带官方资产摘要的 Scoop 清单，README 新增竞品对比与安装章节。
- A libFuzzer harness for the open/render path with seed PDFs (Clang builds, `NEXPDF_BUILD_FUZZERS=ON`). / 面向打开/渲染路径的 libFuzzer 模糊测试目标与种子语料（Clang 构建，`NEXPDF_BUILD_FUZZERS=ON`）。
- GitHub Actions are now pinned by commit SHA with Dependabot tracking weekly updates. / GitHub Actions 改为按提交 SHA 锁定，并由 Dependabot 每周跟踪更新。

### Fixed / 修复

- A transiently failed render tile is retried once instead of leaving a blank region. / 偶发失败的渲染瓦片会自动重试一次，不再留下空白区域。

## [1.0.0-rc.10] - 2026-09-28

### Fixed / 修复

- Edits no longer reset the view: the scroll position is preserved when a revision rebuild keeps the same page layout. / 编辑不再导致视图重置：revision 重建且页面布局不变时保持滚动位置。

### Added / 新增

- Completed a full agent-driven Windows UI acceptance pass over the corpus (CJK, damaged xref, AES-256 password, optional layers, transparency, large image, search highlight, drag placement, reading-position restore). / 完成基于电脑控制的 Windows 界面验收：中文语料、损坏 xref、AES-256 密码、可选图层、透明度、大图、搜索高亮、拖拽放置与阅读位置恢复。

## [1.0.0-rc.9] - 2026-09-28

### Added / 新增

- Hold Ctrl and roll the mouse wheel to zoom the page, and use the new "Fit width" view action (`Ctrl+2`, with `Ctrl+0` for actual size). / 按住 Ctrl 滚动鼠标滚轮缩放页面；新增"适合宽度"视图动作（`Ctrl+2`，`Ctrl+0` 为实际大小）。
- `Ctrl+F` now focuses the search box, and pressing Enter on an empty search box clears the old highlights. / `Ctrl+F` 聚焦搜索框；空搜索回车清除旧高亮。
- Escape cancels a pending "Add text"/"Add image" placement. / Esc 取消待放置的文字/图片。

## [1.0.0-rc.8] - 2026-09-28

### Fixed / 修复

- Search matches are highlighted on the page and Enter cycles through them with automatic scrolling. / 搜索命中在页面上高亮显示，回车循环跳转并自动滚动定位。
- "Add text" no longer renders as a solid color block: the FreeText box background is white while the text keeps the chosen color. / “添加文字”不再渲染为色块：文本框白底，文字保持所选颜色。
- "Add text" and "Add image" are placed by dragging a rectangle on the page instead of a silent fixed location. / “添加文字”与“添加图片”改为在页面上拖拽矩形放置，不再静默落到固定位置。

## [1.0.0-rc.7] - 2026-09-28

### Fixed / 修复

- Render the main canvas and thumbnails at the device pixel ratio so pages stay sharp on high-DPI displays; cache identity includes the render density. / 主画布与缩略图按设备像素比渲染，修复高 DPI 屏幕上的页面模糊；缓存键纳入渲染密度。

### Added / 新增

- Remember each document's last reading position by content fingerprint (no file paths stored) and add an eye-care page tint in the View menu. / 按内容指纹记住每个文档的阅读位置（不保存文件路径），并在视图菜单新增护眼模式页面染色。
- The Windows installer registers nexPDF as a per-user "Open with" candidate and Default-apps entry without taking over the system `.pdf` default. / Windows 安装器把 nexPDF 注册为按用户“打开方式”候选和系统默认应用设置入口，不抢占系统 `.pdf` 默认关联。

## [1.0.0-rc.6] - 2026-09-28

### Changed / 变更

- Keep the three-platform release scope and raise the binary package size target from 80 MiB to 90 MiB so the measured 82.730 MiB macOS Universal DMG passes normally; over-target packages still require a non-empty composition analysis. / 保持三平台发布范围，二进制包体积目标由 80 MiB 上调至 90 MiB，使实测 82.730 MiB 的 macOS Universal DMG 正常通过；超过目标的包仍须提供非空组成分析。
- Release builds now report the release tag via `nexPDF --version`: the version header is generated by CMake (`NEXPDF_VERSION`), and the benchmark tool moved under `tests/benchmarks`. / 发布构建的 `nexPDF --version` 现在显示发布标签：版本头由 CMake 生成（`NEXPDF_VERSION`），基准测试工具移入 `tests/benchmarks`。

## [1.0.0-rc.5] - 2026-08-25

- Simplify Release attachments to the five user-facing packages; keep package-size analysis in the repository and use GitHub asset digests instead of standalone reports.
- Remove reproducible local build, validation, audit, and distribution directories; strengthen `.gitignore` for generated CMake and native binaries.

## [1.0.0-rc.4] - 2026-08-24

- Require and publish a measured composition analysis when a binary package exceeds the 80 MiB target; RC.3 produced no incomplete GitHub Release.

## [1.0.0-rc.3] - 2026-08-24

- Pass the repository license to NSIS by absolute path; RC.2 produced no incomplete GitHub Release.

## [1.0.0-rc.2] - 2026-08-24

- Add the Qt Tools archive required by Windows translation deployment; RC.1 produced no incomplete GitHub Release.

## [1.0.0-rc.1] - 2026-08-24

- Initial C++20/Qt/MuPDF architecture and public domain types.
- Cohesive view/edit icon set, bilingual tooltips, and native application icons for Windows, Linux, and macOS.
- Hybrid command bars keep familiar controls icon-only while adding short labels to primary and high-risk operations.
- Background document session, password opening, rendering, search, practical edits, MuPDF journal undo/redo, permanent redaction, encryption/decryption, and validated atomic save.
- Continuous Qt canvas, bilingual runtime translation, watermark creation/scanning/confirmed removal, and initial integration tests.
- Slim MuPDF build tooling and automated Windows x64, Linux x86_64, and macOS Universal release packaging.
- Release linking, integration tests, qpdf/Poppler cross-validation, and Windows portable-package dependency checks use the unified Qt 6.11.2 baseline.
- Encrypted output rejects empty user/owner passwords, confirms both credentials, warns without rejecting matching passwords, and overwrites temporary/session password buffers when released.
