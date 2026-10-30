# nexPDF 1.0.5 maintenance acceptance / 维护验收

Date: 2026-10-30. Baseline: `v1.0.4`, commit `f1ff6f2cc6df1c6e7f898eac698533e823e1accf`. Candidate: `v1.0.5-rc.1`. The candidate commit is identified by its immutable tag; CI and Release logs are available from the repository [Actions page](https://github.com/YDLuo-1/nexPDF/actions). / 候选版准确提交由不可移动的标签标识，发布只在准确提交的检查通过后进行。

## Confirmed repairs / 已确认修复

- Repeated AES-128/AES-256 save, Keep, reopen and decrypt sequences preserve encryption/content and no longer mutate the live document. Saving preserves catalog, forms, outlines and metadata. / 连续加密、Keep、重开、解密保存保持正确结果，保留目录、表单、书签和元数据。
- Saved undo positions survive undo/redo and branch replacement; failed saves do not commit encryption state or lose the journal. / 保存点正确处理撤销、重做和新分支，失败保存不提交加密设置或丢失历史。
- Windows open-source replacement uses deletion-sharing and ReplaceFileW; replacing, undoing and saving again passes. New-file saves do not silently replace a file created by another process after the initial check. / Windows 原文件覆盖后仍可撤销并再次保存，新目标不静默覆盖竞态中出现的文件。
- Search continuations remain on the document worker; edits invalidate search slices. Anonymous watermark batches retain unselected annotations, including duplicate selection handling. / 搜索保持在工作线程，编辑使旧搜索失效；批量匿名水印删除保留未选择项。
- Copy and printing permissions are checked in the core. Reader and owner access are distinguished; restricted printing is capped at 150 DPI. / 核心执行复制与打印权限，区分用户与所有者，低分辨率打印上限 150 DPI。
- Render cancellation, stale revision, invalid geometry and failure all produce identified terminal results. Cancellation preserves queue order across edits; deadlines are checked before and after rendering. / 取消、过期版本、非法尺寸与失败均返回带标识的终态，保留编辑前后队列顺序。
- Printing to PDF verifies selected-page output, cancellation and document replacement. Real native-printer acceptance remains open. / 打印到 PDF 覆盖页范围、取消与文档变化中止，实体打印仍待验收。
- Viewport painting, tile retry bookkeeping, crash-path overflow and non-optimizable password clearing are corrected. / 修正绘制范围、瓦片重试记录、崩溃路径溢出与密码擦除。

## Local verification / 本地验证

Windows 11, Visual Studio 2022/MSVC 19.44, Qt 6.11.2. MuPDF 1.28.6 is pinned to `da5f5eb890d7b3fb76903160b5f7f48ba1301492`; no second Qt installation was introduced.

- Core Qt Test: **23 passed**, including initialization/cleanup and AES data rows; UI Qt Test: **5 passed**, including initialization/cleanup. CTest: 2/2 executables passed, 13.32 seconds in the final functional batch.
- Translation coverage: 185 source strings, no missing Chinese mappings. Workflow YAML parses successfully.
- Local application staging uses `cmake --install` then `windeployqt`; DLL preflight passes. `--version`, Windows file/product strings and numeric resources identify 1.0.5 (`1.0.5-rc.1` string). Developer test binaries and Qt6Test.dll are excluded from staging.
- Independent Poppler: 10 generated PDFs render successfully, including edits, redaction, watermark add/remove, catalog/form/metadata preservation and repeated-save plaintext. Original and watermark-restored PNG SHA-256 values match; marked differs. qpdf structural/encryption checks are part of the three-platform CI/release gates.
- UI snapshots were inspected for successful page rendering, Chinese labels and toolbar layout. This is a focused smoke check, not a complete IME/accessibility/high-DPI acceptance matrix.

The Windows core test uses Qt's offscreen backend, which reports a missing default font-directory warning on this machine. The native Windows UI test renders correctly; real CJK/font corpus checks remain separate from that warning.

## Performance / 性能

The baseline benchmark was rebuilt from the exact baseline source with MuPDF 1.28.2. The engine-only control uses the same baseline source with MuPDF 1.28.6. All use the same Qt 6.11.2 runtime and 100/300/1000-page corpus at scale 1.5. No page images are saved during timed runs. Local background load is not controlled, so these are comparison observations, not guaranteed speedups.

| Run | Pages | P50 ms | P95 ms | pages/s | Errors |
|---|---:|---:|---:|---:|---:|
| Initial baseline (old code / old engine) | 4200 | 3.2434 | 4.7048 | 274.03 | 0 |
| Initial candidate | 4200 | 3.8483 | 6.3337 | 226.68 | 0 |
| A: old code / old engine | 2800 | 2.9881 | 3.8671 | 309.36 | 0 |
| B: old code / new engine | 2800 | 4.1015 | 7.3759 | 213.30 | 0 |
| C: candidate | 2800 | 3.1428 | 5.1920 | 279.83 | 0 |
| C: candidate | 2800 | 2.6167 | 3.6478 | 346.53 | 0 |
| B: old code / new engine | 2800 | 2.8023 | 5.1829 | 296.23 | 0 |
| A: old code / old engine | 2800 | 2.9863 | 4.6764 | 296.55 | 0 |

The initial slowdown was not stable in crossover measurements. These results do not establish a causal engine cost or a reliable overall speedup. Security fixes remain included. / 首轮回退未在交叉测量中稳定复现，不能据此断言引擎开销或整体提速；保留安全更新。

Candidate stability: 50 open/render/close cycles of 100 pages = **5000 pages, zero errors**, P50 3.4542 ms, P95 5.5149 ms. RSS growth after the fifth-cycle warmup baseline: **0.602 MiB**; sampled RSS maximum 23.23 MiB. The benchmark samples RSS after work and does not measure the true instantaneous process peak.

Complex-corpus smoke: **15 pages, zero errors**, including CJK, RTL, embedded fonts, transparency, layers, object streams, damaged xref and a large image. This is neither full visual equivalence nor full production workload coverage. Saving a disposable object graph has additional temporary memory cost not measured by the rendering benchmark.

Compact raw measurements and corpus hashes are in [maintenance-1.0.5.json](maintenance-1.0.5.json). Large local build/runtime directories are task-owned temporary output and are removed after release validation; small logs and representative images remain under `out/maintenance-20261030` locally.

## Security and acceptance boundaries / 安全与验收边界

[MuPDF release history](https://mupdf.com/releases/history) documents parser, font and image fixes after 1.28.2 and the brotli 1.2.1 update in 1.28.6. Applicability is based on the code paths and pinned dependency, not a demonstrated exploit of nexPDF. Qt remains at the mandated 6.11.2 baseline.

CI builds the engine and harness with Clang ASan/UBSan plus fuzz coverage, then runs a bounded 30-second seed smoke. This verifies the formerly broken fuzz build and catches sampled failures; it does not certify the absence of vulnerabilities. Fuzz findings must be investigated rather than suppressed to make CI green.

The first CI run [40993634622](https://github.com/YDLuo-1/nexPDF/actions/runs/40993634622) passed all three platform suites but exposed a sanitizer-build alignment issue in MuPDF's display-list reader. MuPDF 1.28.6 `fitz/system.h` switches to pointer-aligned storage for GCC sanitizer macros; the CI Clang does not activate that branch. The fuzz engine build now explicitly selects the same supported `FZ_POINTER_ALIGN_MOD=FZ_MEMORY_BLOCK_ALIGN_MOD` layout. Alignment checks, leak checks and all other sanitizer diagnostics remain enabled; no suppression is used. / 首轮三平台通过，fuzz 暴露 Clang 插桩未触发 MuPDF 对齐保护，现显式采用其已有的指针对齐模式，保留全部检查。

The second run [40994790134](https://github.com/YDLuo-1/nexPDF/actions/runs/40994790134) then identified an incompatible function-pointer call in Little CMS (`cmslut.c:1447`, registered in `cmsopt.c`). The existing `broken-xref.pdf` seed reproduces both diagnostics. `tools/patch_mupdf.py` replaces the cast with a correctly typed adapter in every production/fuzz build and the full source archive. ICC processing remains enabled. The render performance figures above predate this adapter-only correction; the final platform regressions and sanitizer smoke are the acceptance gates for it. / 第二轮进一步发现 Little CMS 回调类型不匹配，现用类型一致的适配函数修复，统一进入三平台生产构建和完整源码包；保留 ICC 功能。上面的性能数据采集于此适配修复之前。

Native cancellation uses MuPDF's cooperative cookie API. It cannot force-stop all parsing, image decoding or printer-driver calls; an isolated worker process would be required for hard kill/resource guarantees. Real printer output and macOS hardware are not validated by this Windows/CI work. This batch is consequently published as an RC, retaining the existing stable Latest.

Run [40995711853](https://github.com/YDLuo-1/nexPDF/actions/runs/40995711853) next exposed null-pointer arithmetic for empty FreeType bitmaps in MuPDF `font.c`. The same patch script guards all four bitmap conversions without changing nonempty glyph handling. Seed replay collects UBSan reports and explicitly fails on any report before the strict, halt-on-error mutation run. / 随后修复四处空字形位图转换的空指针运算，非空字形处理保持原样；种子回放集中收集诊断并阻止带错误进入随机变异验证。
