# PDF engine research / PDF 引擎技术调研

Research snapshot / 调研快照：2026-08-22. Release metadata and upstream architecture can change; verify upstream before revising dependency policy. / Release 元数据和上游架构会变化，调整依赖策略前必须重新核验。

| Project / 项目 | Main language and license / 主要语言与许可 | Relevant capability / 相关能力 | nexPDF decision / 结论 |
|---|---|---|---|
| [MuPDF](https://github.com/ArtifexSoftware/mupdf) | C, AGPL-3.0 | Fitz document layer, PDF object model, rendering devices, encryption, annotations, redaction, and low-level content changes | The only production PDF engine; pinned to 1.28.2. / 唯一生产 PDF 引擎，固定 1.28.2。 |
| [qpdf](https://github.com/qpdf/qpdf) | C++, Apache-2.0 | Mature encryption, structural transformation, validation, and command façades; no renderer | Validation-only independent checker and an architectural reference. / 仅作独立校验器和架构参考。 |
| [PoDoFo](https://github.com/podofo/podofo) | C++17, LGPL/MPL | Parsing, creation, signing, and structural edits without a complete renderer | Do not add a second production engine. / 不引入第二生产引擎。 |
| [PDFium](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/README.md) | C++20, BSD | Chromium-grade rendering with a GN/Ninja toolchain; limited high-level editing | Too heavy for the first lightweight release. / 首版构建链和体积过重。 |
| [Apache PDFBox](https://github.com/apache/pdfbox) | Java, Apache-2.0 | Broad creation, extraction, encryption, and manipulation | JVM startup and distribution size do not fit the native target. / JVM 启动和发布体积不符。 |
| [PDF.js](https://github.com/mozilla/pdf.js) | JavaScript, Apache-2.0 | Worker/display/annotation/editor separation | Layering reference only; no browser runtime in nexPDF. / 只参考分层，不引入浏览器运行时。 |
| [pdfcpu](https://github.com/pdfcpu/pdfcpu) | Go, Apache-2.0 | Clear encryption, watermark, and batch-command taxonomy | Parameter and command naming reference only. / 仅参考参数和命令分类。 |
| [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf) | C/C++, (A)GPL | Native desktop shell, MuPDF integration, portable packaging | Reference for lightweight native distribution, not copied architecture. / 参考轻量发布，不复制历史架构。 |

## Decision / 决策

nexPDF uses C++20, Qt Widgets, and a single MuPDF engine. `DocumentSession` is the only document façade; v1 deliberately has no speculative plugin ABI or generic multi-engine layer. qpdf and Poppler remain test-only validators so production behavior cannot silently diverge between two PDF engines.

nexPDF 固定使用 C++20、Qt Widgets 和单一 MuPDF 引擎。`DocumentSession` 是唯一文档门面；v1 不预设插件 ABI，也不建立无实际需求的多引擎抽象。qpdf 与 Poppler 仅用于测试校验，避免生产路径出现双引擎语义分叉。

The AGPL permits lawful copying, modification, and commercial use under its terms; it does not make copying impossible. Its network-source obligations are a stronger reciprocity choice, not an anti-copy mechanism. / AGPL 在其条款下允许复制、修改和商用，不能从法律或技术上“禁止抄走”；选择它是为了更强的源码回馈义务，而不是把开源代码变成不可复制。
