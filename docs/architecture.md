# nexPDF architecture / 架构

## Invariants / 不可破坏的约束

1. `DocumentSession` is the only public document façade. There is no speculative engine interface or plugin ABI in v1. / `DocumentSession` 是唯一文档门面，v1 不建立预留的引擎接口或插件 ABI。
2. A document is opened, parsed, searched, modified, journaled, and saved only on its document thread. UI code never calls MuPDF directly. / 打开、解析、搜索、修改、journal 和保存仅发生在文档线程，UI 不直接调用 MuPDF。
3. Cache identity contains document revision, page, scale bucket, rotation, and tile coordinates. Editing increments the revision, making stale render results unusable. / 缓存键包含 revision、页码、缩放档、旋转和瓦片坐标；编辑递增 revision，旧渲染结果自动失效。
4. The default byte-budgeted cache total is 256 MiB: a 64 MiB MuPDF store plus a 192 MiB tile LRU, configurable to a 64–1024 MiB total. Display lists are separately capped to the eight most recently used pages because MuPDF does not expose their byte cost. / 默认按字节计费的缓存总额为 256 MiB：64 MiB MuPDF store 加 192 MiB 瓦片 LRU，总额可设置为 64–1024 MiB。MuPDF 不提供 display list 字节成本，因此另按最近使用的 8 页设硬上限。
5. Passwords never enter log messages, progress text, exceptions, recent-file metadata, or crash annotations. Temporary UTF-8 buffers, MuPDF write buffers, and the document-session password cache are overwritten when released. / 密码不得进入日志、进度文本、异常、最近文件元数据或崩溃注释；临时 UTF-8 缓冲区、MuPDF 写入缓冲区和文档会话密码缓存在释放时覆盖清理。
6. Existing signed PDFs are never overwritten after modification. / 修改后的已签名 PDF 永不覆盖原文件。
7. A destination becomes visible only after the temporary PDF can be reopened and its first/last pages loaded. / 临时 PDF 能重新打开并加载首尾页后，才允许替换目标。

## Modules / 模块

```text
app (Qt Widgets, interaction, language, cache)
  └── core (DocumentSession, domain types, safe operations)
        └── MuPDF 1.28.2 (single production PDF engine)
```

- `core/include/nexpdf/types.h`: the public v1 source-level types. No SDK/ABI stability promise is made for v1.0. / 公共源码类型；v1.0 不承诺 SDK/ABI 稳定。
- `core/src/document_session.cpp`: document-thread queue, MuPDF journal, editing, watermark records, and safe save. / 文档线程队列、journal、编辑、水印记录和安全保存。
- `app/pdf_canvas.*`: continuous layout, visible-page requests, revision cache, and selection geometry. / 连续布局、可见页请求、revision 缓存和选区坐标。
- `tools/Build-MuPDFWindows.ps1`: reproducible slim Windows dependency build without OCR and unrelated applications. / 无 OCR 及无关应用的 Windows 精简依赖构建。

## Thread model / 线程模型

Public `DocumentSession` calls enqueue immutable value objects to a worker context on one `QThread`. Signals return implicitly shared Qt values to the UI thread. A MuPDF document/page/annotation pointer never crosses the thread boundary.

`DocumentSession` creates and caches each page display list on the document thread, then sends retained display lists to a priority `QThreadPool`. Each render task owns a cloned MuPDF context and never touches the shared `fz_document`. Queued work is cleared on revision changes, stale results are discarded by request/revision identity, and close waits for running tasks before dropping the document. This preserves MuPDF's document-ownership rule while allowing independent tile rasterization in parallel.

公共调用把不可变值对象排入单个 `QThread`；信号只把 Qt 隐式共享值返回 UI。`DocumentSession` 在文档线程创建并缓存页面 display list，再把持有引用的 display list 交给优先级 `QThreadPool`。每个渲染任务独占一个克隆的 MuPDF context，且不访问共享 `fz_document`。revision 变化会清理排队任务，过期结果按 request/revision 丢弃；关闭文档前等待正在执行的任务结束。

## Editing and undo / 编辑与撤销

Each user-visible edit is wrapped in `pdf_begin_operation` / `pdf_end_operation`; failure calls `pdf_abandon_operation`. Undo/redo uses MuPDF journal state. Tool-created text and images are PDF annotations with stable `nexPDF:object:*` names, allowing move, resize, and deletion without pretending to support Word-style paragraph reflow.

每个用户编辑用 MuPDF operation 包裹，失败时回滚。文字和图片使用具有稳定 `nexPDF:object:*` 名称的 PDF 批注，支持移动、缩放和删除，但不宣称 Word 式段落重排或无损替换原字体。

## Watermarks / 水印

Tool-created watermarks use one reusable Form XObject, an opacity ExtGState, and a named OCG. Before the first nexPDF watermark touches a page, its original `Resources` and `Contents` objects are retained in private page metadata. Each affected page gets a separate content stream marked as `/Artifact <</Subtype /Watermark>>` with private `nexPDFWatermarkId` and resource metadata. Removing the final nexPDF watermark restores those original objects exactly and deletes the private marker; unreachable watermark objects are reclaimed by garbage collection on save. Standard Watermark annotations and explicitly labelled external annotations are review-required candidates. Repeated external page content is reported as `Unsupported` because page-local isolation cannot be proven safely.

本工具水印使用一个可复用 Form XObject、透明度 ExtGState 和命名 OCG。页面首次添加 nexPDF 水印前，会在私有页面元数据中保留原始 `Resources` 和 `Contents` 对象。每个目标页拥有独立的水印内容流，以 `/Artifact <</Subtype /Watermark>>` 标记，并记录私有 `nexPDFWatermarkId` 及资源元数据；删除该页最后一个 nexPDF 水印时精确恢复原对象并清除私有标记，不可达水印对象在保存垃圾回收时清理。标准 Watermark 批注及明确标注的外部批注仍需人工确认；外部重复正文因无法证明页面隔离而标为 `Unsupported`。

## Save transaction / 保存事务

```text
write sibling temporary file
  → flush and close
  → reopen with expected password
  → count pages and load first/last
  → atomic replace (MoveFileExW or POSIX rename)
  → report saved
```

Overwrite requires an explicit UI confirmation. A crash or cancellation before the replace leaves the original target intact. The sibling temporary path is a UUID name opened with `QIODevice::NewOnly`; a scope guard removes it unless validation and replacement both succeed.

覆盖已有文件必须由界面二次确认。替换发生前崩溃或取消不会破坏原目标。临时文件使用目标目录中的 UUID 文件名并以 `QIODevice::NewOnly` 打开；只有校验和替换都成功时才解除作用域清理。
