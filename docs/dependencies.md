# Dependency and license inventory / 依赖与许可清单

This is an engineering inventory, not legal advice. It records the dependency path actually used by nexPDF v1.0.0 and the release obligations that still require verification. / 本文是工程清单，不构成法律意见；它记录 nexPDF v1.0.0 的实际依赖路径，以及正式发布前仍需核验的义务。

## Production dependencies / 生产依赖

| Dependency | Linkage and purpose / 链接方式与用途 | License path / 许可路径 |
|---|---|---|
| MuPDF 1.28.2 (`fe374acc`) | Statically linked C PDF engine: parse, render, search, encrypt/decrypt, edit, annotate, redact, watermark, journal, and save. / 静态链接的 C PDF 引擎，负责解析、渲染、搜索、加解密、编辑、批注、涂黑、水印、journal 与保存。 | GNU AGPL-3.0-or-later, or a separate Artifex commercial license. / AGPL-3.0-or-later，或另购 Artifex 商业许可。 |
| Qt 6.11.2 Core, Gui, Widgets | Dynamically linked cross-platform application framework: threads, value types, image handling, windowing, widgets, input, and localization. / 动态链接的跨平台应用框架，负责线程、值类型、图像、窗口、控件、输入与本地化。 | LGPLv3/GPLv3 or Qt commercial terms, subject to the exact Qt distribution used. / LGPLv3/GPLv3 或 Qt 商业条款，以实际 Qt 发行物为准。 |
| Microsoft Visual C++ Runtime | Dynamically distributed with the Windows package from Visual Studio's Redist directory. / Windows 包从 Visual Studio Redist 目录动态分发。 | Microsoft Visual Studio redistribution terms. / Microsoft Visual Studio 再分发条款。 |
| Operating-system APIs | Windows system libraries; equivalent native APIs are used by Qt on Linux/macOS. / Windows 系统库；Linux/macOS 由 Qt 使用对应原生 API。 | System-library exception/operating-system terms as applicable. / 适用系统库例外及操作系统条款。 |

The Windows runtime also contains Qt platform/style/image-format plugins (`qwindows`, modern Windows style, GIF/ICO/JPEG) and the ICU component deployed by the selected Qt build. These are Qt/transitive runtime components, not separate nexPDF feature engines. / Windows 运行包还包含 Qt 平台、样式和图像格式插件（`qwindows`、现代 Windows 样式、GIF/ICO/JPEG），以及该 Qt 构建部署的 ICU 组件；它们属于 Qt/传递依赖，不是第二套 PDF 引擎。

## MuPDF transitive components / MuPDF 传递组件

The slim build initializes and compiles the MuPDF-pinned revisions of `jbig2dec`, `mujs`, `freetype`, `gumbo-parser`, `harfbuzz`, `libjpeg`, `lcms2`, `openjpeg`, `zlib`, `brotli`, and `cmark-gfm`. They provide PDF image codecs, fonts, shaping, color management, compression, HTML parsing, and scripting support required by MuPDF internals. Their original license files remain in the complete MuPDF source tree and must be carried into the corresponding-source/SBOM process. / 精简构建初始化并编译 MuPDF 锁定版本的上述组件，用于 PDF 图像编解码、字体、文字塑形、色彩管理、压缩、HTML 解析及 MuPDF 内部脚本支持。其原始许可文件保留在完整 MuPDF 源码树中，必须纳入对应源码与 SBOM 流程。

OCR/Tesseract, curl/network loading, GLUT viewer, Office extraction, ZXing, and Zint are excluded from the nexPDF production build. / nexPDF 生产构建排除了 OCR/Tesseract、curl 网络加载、GLUT 查看器、Office 提取、ZXing 和 Zint。

## Validation-only dependencies / 仅验证依赖

- Qt Test is linked only by test/benchmark executables and is excluded from end-user packages. / Qt Test 仅由测试和基准程序链接，不进入最终用户包。
- qpdf and Poppler are independent CI/developer validators. They are not linked into `nexPDF.exe`. / qpdf 与 Poppler 是 CI/开发独立校验器，不链接进 `nexPDF.exe`。
- CMake, MSBuild/Ninja, NSIS, GitHub Actions, and the SPDX SBOM action are build/release tools, not runtime libraries. / 这些是构建和发布工具，不是运行库。

## Compliance boundary / 合规边界

- The current project deliberately uses `AGPL-3.0-or-later`, matching MuPDF's open-source path. Binary distribution must provide the complete corresponding source for the exact build, including MuPDF and required nested source. / 当前项目主动采用 `AGPL-3.0-or-later`，与 MuPDF 开源许可路径一致；二进制分发必须提供精确对应源码，包括 MuPDF 及所需嵌套源码。
- A modified network service must offer its interacting users the corresponding source required by AGPL section 13. / 修改版网络服务必须按 AGPL 第 13 条向交互用户提供对应源码。
- A closed-source, source-available-but-not-AGPL, or otherwise AGPL-incompatible product must not use this MuPDF build without obtaining suitable commercial terms from Artifex. / 闭源、仅源码可见但非 AGPL、或其他不兼容产品，不得继续使用此 MuPDF 构建，除非从 Artifex 获得合适的商业许可。
- Qt is dynamically linked. Releases must retain notices and LGPL text, provide the applicable Qt corresponding source or a compliant offer, and must not prohibit the user's LGPL relinking/debugging rights. / Qt 采用动态链接；发布物必须保留声明和 LGPL 文本，提供适用 Qt 对应源码或合规获取方式，且不得禁止用户为 LGPL 重链接而进行的调试权利。
- The Release workflow generates an SPDX SBOM and a full source archive, but those assets are not considered verified until the three-platform Release workflow succeeds and the archive is inspected. / Release 工作流会生成 SPDX SBOM 和完整源码包，但在三平台工作流成功且归档内容经检查前，不视为已完成验证。

Official references / 官方参考：

- [MuPDF 1.28.2 documentation](https://mupdf.readthedocs.io/en/latest/)
- [MuPDF license](https://mupdf.readthedocs.io/en/latest/license.html)
- [GNU AGPL overview](https://www.gnu.org/licenses/)
- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations)
