# nexPDF architecture / 架构

## Invariants / 不可破坏的约束

1. `DocumentSession` is the only public document façade. There is no speculative engine interface or plugin ABI in v1. / `DocumentSession` 是唯一文档门面，v1 不建立预留的引擎接口或插件 ABI。
2. A document is opened, parsed, searched, modified, journaled, and saved only on its document thread. UI code never calls MuPDF directly. / 打开、解析、搜索、修改、journal 和保存仅发生在文档线程，UI 不直接调用 MuPDF。
3. Cache identity contains document revision, page, scale bucket, rotation, render density (device pixel ratio), and tile coordinates. Editing increments the revision, making stale render results unusable. / 缓存键包含 revision、页码、缩放档、旋转、渲染密度（设备像素比）和瓦片坐标；编辑递增 revision，旧渲染结果自动失效。
4. The default byte-budgeted cache total is 256 MiB: a 64 MiB MuPDF store plus a 192 MiB tile LRU. `PdfCanvas::setCacheLimitMiB` accepts a 64–1024 MiB total, but the UI does not expose a setting yet. Display lists are separately capped to the eight most recently used pages because MuPDF does not expose their byte cost. / 默认按字节计费的缓存总额为 256 MiB：64 MiB MuPDF store 加 192 MiB 瓦片 LRU。`PdfCanvas::setCacheLimitMiB` 接受 64–1024 MiB 总额，但界面尚未提供设置入口。MuPDF 不提供 display list 字节成本，因此另按最近使用的 8 页设硬上限。
5. Passwords never enter log messages, progress text, exceptions, recent-file metadata, or crash annotations. Save/open parameters use `SecurePassword` (deep copy, zeroized on destruction); temporary UTF-8 buffers, MuPDF write buffers, and the document-session password cache are overwritten when released. Copies owned by Qt widgets, allocators, and the operating system are outside this guarantee. / 密码不得进入日志、进度文本、异常、最近文件元数据或崩溃注释；保存/打开参数使用 `SecurePassword`（深拷贝、析构清零），临时 UTF-8 缓冲区、MuPDF 写入缓冲区和文档会话密码缓存在释放时覆盖清理。Qt 控件、分配器与操作系统持有的副本不在保证范围内。
6. Existing signed PDFs are never overwritten after modification. / 修改后的已签名 PDF 永不覆盖原文件。
7. A destination becomes visible only after the temporary PDF can be reopened and its first/last pages loaded. / 临时 PDF 能重新打开并加载首尾页后，才允许替换目标。

## Modules / 模块

```text
app (Qt Widgets, interaction, language, cache)
  └── core (DocumentSession, domain types, safe operations)
        └── MuPDF 1.28.6 (single production PDF engine)
```

- `core/include/nexpdf/types.h`: the public v1 source-level types. No SDK/ABI stability promise is made for v1.0. / 公共源码类型；v1.0 不承诺 SDK/ABI 稳定。
- `core/src/document_session.cpp`: document-thread queue, MuPDF journal, editing, watermark records, and safe save. / 文档线程队列、journal、编辑、水印记录和安全保存。
- `app/pdf_canvas.*`: continuous layout, visible-page requests, revision cache, and selection geometry. / 连续布局、可见页请求、revision 缓存和选区坐标。
- `tools/Build-MuPDFWindows.ps1`: reproducible slim Windows dependency build without OCR and unrelated applications. / 无 OCR 及无关应用的 Windows 精简依赖构建。

## Thread model / 线程模型

Public `DocumentSession` calls enqueue immutable value objects to a worker context on one `QThread`. Signals return implicitly shared Qt values to the UI thread. A MuPDF document/page/annotation pointer never crosses the thread boundary.

`DocumentSession` creates and caches each page display list on the document thread, then sends retained display lists to a priority `QThreadPool`. Each render task owns a cloned MuPDF context and never touches the shared `fz_document`. Every request returns one terminal RenderResult, including failures and cancellations. Render errors are carried by RenderResult.error, not the generic failed signal. A session timer requests cooperative MuPDF cookie cancellation at the request deadline (30 seconds by default); revision changes cancel old work, while later requests remain ordered on the document thread. At most four raster workers run, with a 32-million-pixel output limit. Close requests cancellation before waiting for running tasks. This preserves MuPDF's document-ownership rule while allowing independent tile rasterization in parallel.

公共调用把不可变值对象排入单个 `QThread`；信号只把 Qt 隐式共享值返回 UI。`DocumentSession` 在文档线程创建并缓存页面 display list，再把持有引用的 display list 交给优先级 `QThreadPool`。每个渲染任务独占一个克隆的 MuPDF context，且不访问共享 `fz_document`。每个请求均返回一次 RenderResult 终态，失败与取消通过 error 字段表达，不再使用通用 failed 信号。默认 30 秒期限由会话定时器发起 MuPDF cookie 协作取消；revision 变化取消旧任务，后续请求仍按文档线程顺序执行。栅格并发最多 4 个，单次输出不超过 3200 万像素；关闭前先请求取消再等待任务结束。

## Editing and undo / 编辑与撤销

Each user-visible edit is wrapped in `pdf_begin_operation` / `pdf_end_operation`; failure calls `pdf_abandon_operation`. Undo/redo uses MuPDF journal state and a separate saved position; truncating a redo branch invalidates a saved position on that branch. Saving itself does not add a journal step. Tool-created text and images are PDF annotations with stable `nexPDF:object:*` names, allowing move, resize, and deletion without pretending to support Word-style paragraph reflow.

每个用户编辑用 MuPDF operation 包裹，失败时回滚。文字和图片使用具有稳定 `nexPDF:object:*` 名称的 PDF 批注，支持移动、缩放和删除，但不宣称 Word 式段落重排或无损替换原字体。

## Watermarks / 水印

Tool-created watermarks use one reusable Form XObject, an opacity ExtGState, and a named OCG. Before the first nexPDF watermark touches a page, its original `Resources` and `Contents` objects are retained in private page metadata. Each affected page gets a separate content stream marked as `/Artifact <</Subtype /Watermark>>` with private `nexPDFWatermarkId` and resource metadata. Removing the final nexPDF watermark restores those original objects exactly and deletes the private marker; unreachable watermark objects are reclaimed by garbage collection on save. Standard Watermark annotations and explicitly labelled external annotations are review-required candidates. Repeated external page content is reported as `Unsupported` because page-local isolation cannot be proven safely.

本工具水印使用一个可复用 Form XObject、透明度 ExtGState 和命名 OCG。页面首次添加 nexPDF 水印前，会在私有页面元数据中保留原始 `Resources` 和 `Contents` 对象。每个目标页拥有独立的水印内容流，以 `/Artifact <</Subtype /Watermark>>` 标记，并记录私有 `nexPDFWatermarkId` 及资源元数据；删除该页最后一个 nexPDF 水印时精确恢复原对象并清除私有标记，不可达水印对象在保存垃圾回收时清理。标准 Watermark 批注及明确标注的外部批注仍需人工确认；外部重复正文因无法证明页面隔离而标为 `Unsupported`。

## Save transaction / 保存事务

```text
graft a complete disposable PDF graph (including catalog and metadata)
  → write sibling temporary file (owner-only at creation)
  → apply destination permissions
  → flush, fsync, and close
  → reopen with expected password
  → count pages and load first/last
  → atomic replace (ReplaceFileW / MoveFileExW or POSIX rename / exclusive link)
  → fsync the directory (POSIX)
  → report saved
```

Overwrite requires an explicit UI confirmation. A crash or cancellation before the replace leaves the original target intact. The sibling temporary path is a UUID name opened with `QIODevice::NewOnly`; a scope guard removes it unless validation and replacement both succeed. On POSIX the temporary file starts as `0600` and inherits the destination's mode before the rename, so a decrypted edit is never world-readable.

覆盖已有文件必须由界面二次确认。替换发生前崩溃或取消不会破坏原目标。临时文件使用目标目录中的 UUID 文件名并以 `QIODevice::NewOnly` 打开；只有校验和替换都成功时才解除作用域清理。POSIX 上临时文件以 `0600` 创建，并在替换前恢复目标文件原有权限，避免解密后的内容被同机其他用户读取。

The live document and its journal are never passed to the mutating PDF writer. A disposable graph retains catalog, forms, outlines, metadata and shared references; save memory therefore includes this temporary graph. Keep uses the source encryption key until output encryption changes; after a successful change, session-owned SecurePassword values retain the requested encryption settings for subsequent saves and are wiped on close. A failed save never commits those settings. Windows source handles permit deletion-sharing so ReplaceFileW can replace the path while undo continues reading the original file data. New destinations are created without silently replacing a file that appeared after the initial existence check.

保存时复制完整可达对象图供写入器使用，保留目录、表单、书签、元数据与共享引用，临时图会增加保存峰值内存。当前文档及 journal 不交给会修改对象的写入器。成功更改加密后，会话使用可清理的 SecurePassword 保存后续 Keep 所需设置，并在关闭时清除；失败保存不提交这些设置。Windows 源文件句柄允许共享删除，让原子替换后仍可读取旧数据完成撤销。对于原先不存在的目标，保存时不会静默覆盖在检查后才出现的文件。

Cancellation is cooperative, not a process sandbox or a hard kill deadline. Native parsing, image decoding or driver calls that do not poll a cookie can still delay shutdown. Printing has its own bounded UI wait and document-revision cancellation; physical printer behavior still requires hardware acceptance.

取消属于协作机制，不是进程沙箱或强制终止期限。不检查 cookie 的原生解析、图片解码和驱动调用仍可能延迟退出。打印另设界面等待超时及文档版本变化取消，实体打印行为仍须硬件验收。
