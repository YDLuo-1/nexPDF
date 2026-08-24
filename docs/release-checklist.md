# Release checklist / 发布检查表

A tag is not a formal release until every required item is checked with attached CI evidence. / 标签只有在所有必需项附带 CI 证据并通过后，才是正式 Release。

- [ ] Exact Qt 6.11.2 and MuPDF 1.28.2 production build on Windows x64, Linux x86_64, and macOS Universal.
- [ ] Unit/integration tests, qpdf `--check`, Poppler reference rendering, and clean-machine smoke tests pass.
- [ ] AES-128/AES-256, legacy RC4 read, Unicode passwords, empty-password rejection, wrong passwords, permissions, and decryption corpus pass.
- [ ] Page operations, annotations, redaction, journal undo/redo, disk failure/cancel safety, and signature Save As gate pass.
- [ ] nexPDF watermark add/remove render equivalence and external candidate false-positive corpus pass.
- [ ] CJK, RTL, embedded fonts, transparency, layers, object streams, damaged xref, large images, and 100/300/1000-page corpus pass.
- [ ] Measured P50/P95, throughput, peak RSS, error count, 50-cycle memory stability, mutool ratios, and package-size report are published.
- [ ] High-DPI, Chinese IME, theme, scrollbar, focus/accessibility, and three-platform screenshots are reviewed.
- [ ] Portable ZIP, unsigned setup.exe, AppImage, Universal DMG, full source, SPDX SBOM, SHA256SUMS, and bilingual notes exist.
- [ ] Source archive contains MuPDF and all nested dependency source required by AGPL corresponding-source obligations.
- [ ] A clean VM runs `--version`, open, render, edit, save, reopen; unsigned-package warnings are visible in notes.

中文检查范围与以上英文逐项相同：精确依赖版本、三平台构建、独立校验、加解密、编辑/涂黑、水印、复杂语料、性能和内存、界面截图、完整源码及发布资产，缺一不可。
