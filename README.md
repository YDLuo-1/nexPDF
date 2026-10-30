# nexPDF

[中文](#中文) · [English](#english)

## 中文

nexPDF 是一个本地运行、跨平台的 PDF 查看与实用编辑工具，使用 C++20、Qt Widgets 和 MuPDF 构建。它不包含遥测，不上传文档，也不会自动执行 PDF JavaScript、启动嵌入文件或静默打开外部链接。

> 项目状态：已发布 1.0 系列；当前维护版本为 `1.0.5-rc.1`。本轮重点修复保存、撤销、搜索、权限与打印可靠性，并更新 PDF 引擎安全补丁。验收证据和实机边界见[发布检查表](docs/release-checklist.md)。
>
> **下载使用**：前往 [Releases](https://github.com/YDLuo-1/nexPDF/releases) 获取最新候选版的 Windows 便携 ZIP / 安装包、Linux AppImage、macOS DMG 与完整源码包；每次功能或修复合入后都会立即发布包含全部变更的候选版。

### 功能

- 查看：拖放和命令行打开、密码输入、连续滚动、页码导航、缩放、页面旋转和文本搜索；按内容指纹记住每个文档的阅读位置（不保存文件路径），并提供护眼模式页面染色。
- 加密：AES-256（默认）和 AES-128 兼容模式；加密输出要求非空用户/所有者密码，并可配置 PDF 权限。
- 解密：仅使用正确密码创建无加密副本；不包含破解、爆破或权限绕过功能。
- 编辑：空白页、删除、排序、旋转、导入页面；添加、移动、缩放、删除工具创建的文字和图片；高亮、下划线、删除线、自由文本、图形、手绘批注。
- 涂黑：先创建预览批注，再由用户确认永久应用。应用后重叠内容会被实际移除，不只是画黑框。
- 水印：文字或图片水印；精确识别本工具创建的水印；列出外部 Watermark 批注候选并要求逐项确认。
- 安全保存：写入目标目录临时文件，由 MuPDF 重新打开校验后再原子替换；默认另存为；已签名 PDF 强制保存到新文件。
- 双语：跟随系统语言，并可在应用中切换简体中文/英文。
- 界面：采用混合命令栏——打开、保存、加解密及高理解成本编辑显示图标与短文字，常见导航/批注保持紧凑图标；所有动作提供中英文 Tooltip，应用图标覆盖 Windows、Linux 和 macOS 包。

PDF 权限主要依赖阅读器遵守，不能替代真正的数据访问控制。用户密码与所有者密码可以相同，但部分阅读器会优先识别为用户密码，导致所有者权限行为不可靠，因此界面会提示风险。请使用较长、唯一且非空的密码。外部 PDF 可能把水印烘焙进正文、图片或共享 XObject；nexPDF 不保证识别所有此类水印，也不会自动删除启发式候选。请仅处理您拥有权利或已获得授权的文件，并始终保留备份。

### 安装

Windows 便携包从 [Releases](https://github.com/YDLuo-1/nexPDF/releases) 直接下载；winget 清单模板见 [`packaging/winget`](packaging/winget)（每次发布后需按当前资产哈希更新再提交 winget-pkgs），Scoop 清单模板见 [`packaging/scoop`](packaging/scoop)，可复制进自己的 bucket 使用。

### 与同类工具对比

| | nexPDF | SumatraPDF | PDF24 | 在线 PDF 工具 |
|---|---|---|---|---|
| 本地运行、不上传文档 | ✅ | ✅ | ✅ | ❌ 需上传 |
| 真涂黑（移除底层内容，非画黑框） | ✅ 两阶段确认 | ❌ 无编辑 | 部分 | 视工具而定 |
| 对象级精确去水印 | ✅ 可恢复原对象 | ❌ | ❌ | ❌ |
| AES-256/128 加密与解密 | ✅ | ❌ | ✅ | 视工具而定 |
| 开源（AGPL）、无广告、无捆绑 | ✅ | ✅ | ❌ 闭源安装器带推广 | ❌ |
| 原生双语界面（中/英） | ✅ | 一般 | ✅ | 视工具而定 |

### 设计取舍

nexPDF 的功能取舍围绕“本地、可信、可撤销”：

- **涂黑必须两阶段**：先创建预览批注，人工确认后才真正移除底层内容；只画黑框不是涂黑。
- **水印必须可精确恢复**：添加水印前保留页面原始对象，移除最后一个水印时按原样还原，而不是再叠一层遮盖。
- **保存必须是事务**：写入临时文件、用 MuPDF 重新打开校验、再原子替换目标；已签名文档强制另存为新文件。
- **隐私优先于便利**：无遥测、不上传文档、不执行 PDF JavaScript；阅读位置记忆只保存内容指纹和页码，不保存文件路径。
- **界面按理解成本分层**：高频动作保持纯图标，加密、涂黑、永久应用等高代价动作必须带文字与确认。
- **发布必须有门槛**：三平台 CI、qpdf/Poppler 独立校验和包体积目标全部通过才允许发布；人工验收未完成前只发布 RC 预发行版。

### 界面

![nexPDF Windows 主界面](docs/images/windows-main-window.png)

截图由 Windows 11、150% DPI 的自动化 UI 冒烟测试生成：真实主窗口打开合成 PDF，等待主画布完成渲染后抓取。中文字体、输入法和其他平台外观仍需按发布检查表人工验收。

### 构建

依赖版本固定为 Qt 6.11.2 和 MuPDF 1.28.6。构建脚本需要 Python 3.10 或以上版本，以应用锁定的引擎维护补丁。获取源码时必须包含子模块：

```powershell
git clone --recurse-submodules https://github.com/YDLuo-1/nexPDF.git
cd nexPDF
```

Windows 的精简 MuPDF 构建会排除 OCR、curl、OpenGL 查看器、Office 导出和条码组件：

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-MuPDFWindows.ps1 -Configuration Release
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=D:\Qt\6.11.2\msvc2022_64 `
  -DMUPDF_ROOT="$PWD\out\mupdf-windows-x64"
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

Linux/macOS 以及打包步骤见 [构建说明](docs/building.md)。架构与线程/保存约束见 [架构说明](docs/architecture.md)，引擎选型和同类项目对比见 [技术调研](docs/research.md)，运行库与许可边界见 [依赖清单](docs/dependencies.md)。

### 验证范围

当前版本的构建、测试、独立 PDF 校验和性能记录集中在[维护验收记录](docs/maintenance-1.0.5.md)，避免把旧版本计数当作当前结论。CI 覆盖 Windows x64、Linux x86_64、macOS Universal，并增加带 ASan/UBSan 的 Linux fuzz 冒烟。

针对性回归覆盖连续加密保存、保存失败恢复、跨保存的撤销/重做、匿名水印批量移除、复制/打印权限、分批搜索、渲染请求终态，以及通过 QPrinter 输出 PDF 和取消。实体打印机、macOS 实机和完整恶意 PDF 审计不由这些自动测试替代。

### 许可与免责声明

项目采用 [`AGPL-3.0-or-later`](LICENSE)。AGPL 允许复制、修改和商用，但分发修改版本或通过网络提供修改版本功能时，通常需要向相应用户提供对应源码。它不能阻止他人依法复制代码。第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

**AI 开发免责声明：** 本项目大量使用 AI 辅助设计、编码和审查。虽然建议所有变更经过人工审查和自动化测试，软件仍可能包含功能缺陷、安全问题或导致数据损坏的错误。本软件按原样提供、不附带任何担保。请保留原始文件备份，并用独立工具验证重要输出。

nexPDF 与任何其他同名或近似名称的 NexPDF 产品、公司或网站均无关联。

## English

nexPDF is a local, cross-platform PDF viewer and practical editor built with C++20, Qt Widgets, and MuPDF. It contains no telemetry, uploads no documents, and does not automatically execute PDF JavaScript, launch embedded files, or silently open external links.

> Project status: the 1.0 series has shipped; the current maintenance candidate is `1.0.5-rc.1`. This update addresses saving, undo, search, permissions, printing reliability and PDF engine security patches. See the [release checklist](docs/release-checklist.md) for evidence and hardware acceptance limits.
>
> **Download**: head to [Releases](https://github.com/YDLuo-1/nexPDF/releases) for the latest release candidate with the Windows portable ZIP / installer, Linux AppImage, macOS DMG, and the full source archive; every merged feature or fix is followed immediately by a candidate containing all changes.

### Features

- Viewing: drag-and-drop and command-line opening, password prompt, continuous scrolling, page navigation, zoom, page rotation, and text search; remembers each document's last reading position by content fingerprint (no file paths stored) and offers an eye-care page tint.
- Encryption: AES-256 by default, with AES-128 compatibility mode; encrypted output requires non-empty user/owner passwords and supports PDF permission flags.
- Decryption: creates an unencrypted copy only with a correct password; no cracking, brute force, or permission bypass.
- Editing: insert blank pages, delete, reorder, rotate, and import pages; add, move, resize, and remove tool-created text/images; highlights, underlines, strikeouts, free text, shapes, and ink annotations.
- Redaction: create preview annotations first, then explicitly apply permanent redaction. Applying redaction removes overlapping content rather than drawing a cosmetic black box.
- Watermarks: text/image watermarks, exact recognition of nexPDF-created marks, and reviewed candidates for external Watermark annotations.
- Safe saving: write a sibling temporary file, reopen it with MuPDF, then atomically replace the destination; Save As is the default, and signed PDFs must be saved to a new path.
- Bilingual UI: follows the system language and can switch between Simplified Chinese and English.
- Interface: a hybrid command bar gives Open/Save, encryption, and higher-comprehension-cost edits an icon plus a short label, while familiar navigation/annotation commands stay compact; every action has bilingual tooltips and app icons are wired into Windows, Linux, and macOS packages.

PDF permissions depend mainly on reader cooperation and are not a substitute for access control. User and owner passwords may match, but some readers try the user credential first and may not grant owner privileges, so the UI warns before continuing. Use long, unique, non-empty passwords. External PDFs may bake watermarks into page content, images, or shared XObjects. nexPDF cannot promise to detect all such marks and never auto-deletes heuristic candidates. Process only files you own or are authorized to modify, and always keep backups.

### Install

Grab the Windows portable ZIP from [Releases](https://github.com/YDLuo-1/nexPDF/releases); winget manifest templates live in [`packaging/winget`](packaging/winget) (update them to the current asset hash before submitting to winget-pkgs) and the Scoop template in [`packaging/scoop`](packaging/scoop) can be copied into your own bucket.

### How it compares

| | nexPDF | SumatraPDF | PDF24 | Online PDF tools |
|---|---|---|---|---|
| Runs locally, no upload | ✅ | ✅ | ✅ | ❌ upload required |
| True redaction (removes content, not a black box) | ✅ two-stage | ❌ no editing | partial | varies |
| Object-level exact watermark removal | ✅ restores originals | ❌ | ❌ | ❌ |
| AES-256/128 encryption and decryption | ✅ | ❌ | ✅ | varies |
| Open source (AGPL), no ads, no bundling | ✅ | ✅ | ❌ closed installer with offers | ❌ |
| Native bilingual UI (Chinese/English) | ✅ | partial | ✅ | varies |

### Design decisions

nexPDF trades features around being local, trustworthy, and reversible:

- **Redaction is always two-stage**: content is only removed after you create preview annotations and explicitly confirm; drawing a black box is not redaction.
- **Watermarks must restore exactly**: original page objects are retained before the first watermark is added, and removing the last watermark restores them verbatim instead of stacking another cover layer.
- **Saving is a transaction**: write a temporary file, reopen and validate it with MuPDF, then atomically replace the target; signed documents must be saved to a new path.
- **Privacy over convenience**: no telemetry, no document upload, no PDF JavaScript; reading-position memory stores only a content fingerprint and page numbers, never file paths.
- **The interface is layered by comprehension cost**: frequent actions stay icon-only, while high-stakes actions such as encryption, redaction, and permanent application always carry labels and confirmations.
- **Releases must pass gates**: a release goes out only after three-platform CI, independent qpdf/Poppler validation, and the package-size target all pass; RC prereleases ship until human acceptance completes.

### Interface

![nexPDF main window on Windows](docs/images/windows-main-window.png)

The screenshot is generated by the automated Windows 11 UI smoke at 150% DPI after a real main window opens and renders a synthetic PDF. Chinese fonts/IME and appearance on the other platforms remain manual release-checklist gates.

### Build

Dependencies are pinned to Qt 6.11.2 and MuPDF 1.28.6. Build scripts require Python 3.10 or newer to apply the pinned engine maintenance patches. Clone with submodules:

```powershell
git clone --recurse-submodules https://github.com/YDLuo-1/nexPDF.git
cd nexPDF
```

The slim Windows MuPDF build excludes OCR, curl, the OpenGL viewer, Office export, and barcode components:

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-MuPDFWindows.ps1 -Configuration Release
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=D:\Qt\6.11.2\msvc2022_64 `
  -DMUPDF_ROOT="$PWD\out\mupdf-windows-x64"
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

See [build documentation](docs/building.md) for Linux/macOS and packaging, [architecture](docs/architecture.md) for threading, ownership, and safe-save rules, [technical research](docs/research.md) for the engine decision and project comparison, and the [dependency inventory](docs/dependencies.md) for runtime and license boundaries.

### Verification scope

Current build, regression, independent PDF validation and benchmark evidence is recorded in the [maintenance report](docs/maintenance-1.0.5.md). CI covers Windows x64, Linux x86_64 and macOS Universal, plus a Linux ASan/UBSan fuzz smoke.

Targeted checks cover repeated encrypted saves, failure recovery, undo/redo across saves, anonymous watermark batches, copy/print permissions, sliced search, terminal render results, and QPrinter PDF output/cancellation. Physical printers, real macOS hardware and an exhaustive malicious-PDF audit remain separate acceptance boundaries.

### License and disclaimer

Licensed under [`AGPL-3.0-or-later`](LICENSE). The AGPL permits copying, modification, and commercial use, but distributing modified versions or offering modified network functionality generally requires providing corresponding source to the relevant users. It does not prevent lawful copying. See [third-party notices](THIRD_PARTY_NOTICES.md).

**AI development disclaimer:** This project makes extensive use of AI-assisted design, coding, and review. Although changes should receive human review and automated testing, the software may still contain defects, security issues, or data-loss bugs. It is provided as-is, without warranty. Keep original backups and independently validate important output.

nexPDF is not affiliated with any other product, company, or website using NexPDF or a similar name.
