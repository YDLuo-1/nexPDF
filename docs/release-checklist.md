# Release checklist / 发布检查表

A tag is not a formal release until every required item is checked with attached evidence. / 标签只有在所有必需项附带证据并通过后，才是正式 Release。

## Evidence status / 证据状态（2026-09-28）

- Three-platform build, tests, qpdf/Poppler validation, and the no-JS/telemetry posture: covered by every green CI run on `main` (e.g. runs 36378197419–3638906xxxx) and the Release pipeline gates. / 三平台构建、测试、qpdf/Poppler 独立校验由每次 main 上的绿色 CI 与 Release 门槛覆盖。
- Windows UI acceptance (high-DPI sharpness, search highlight, drag placement, reading position, damaged xref, AES-256 unlock, layers, transparency, large image, bilingual UI): agent-driven with screenshots, 2026-09-28. / Windows 界面验收由电脑控制完成并留存截图。
- Measured performance: render P50 2.6 ms / P95 4.1 ms, throughput 330 pages/s, peak RSS 24 MiB; 50-cycle stability +0.6 MiB after warmup (100-page corpus, 1.5 density, 5000 pages, zero errors). / 性能与内存稳定性实测数据如左。
- Package assets, digests, bilingual notes, and the AGPL source archive: verified on rc.7–rc.10. / 发布资产、摘要、双语说明与 AGPL 源码包在 rc.7–rc.10 实测验证。
- Still open for formal v1.0.0: clean-VM smoke (setup.exe on a pristine machine), Linux AppImage and macOS DMG spot checks on real systems, dedicated embedded-font corpus, and a measured print path once printing ships. / 正式 v1.0.0 前仍待完成：干净虚拟机冒烟、Linux/macOS 实机抽查、嵌入式字体专用语料，以及未来打印功能的实测。

- [ ] Exact Qt 6.11.2 and MuPDF 1.28.2 production build on Windows x64, Linux x86_64, and macOS Universal.
- [ ] Unit/integration tests, qpdf `--check`, Poppler reference rendering, and clean-machine smoke tests pass.
- [ ] AES-128/AES-256, legacy RC4 read, Unicode passwords, empty-password rejection, wrong passwords, permissions, and decryption corpus pass.
- [ ] Page operations, annotations, redaction, journal undo/redo, disk failure/cancel safety, and signature Save As gate pass.
- [ ] nexPDF watermark add/remove render equivalence and external candidate false-positive corpus pass.
- [ ] CJK, RTL, embedded fonts, transparency, layers, object streams, damaged xref, large images, and 100/300/1000-page corpus pass.
- [ ] Measured P50/P95, throughput, peak RSS, error count, 50-cycle memory stability, mutool ratios, and package-size report are published.
- [ ] High-DPI, Chinese IME, theme, scrollbar, focus/accessibility, and three-platform screenshots are reviewed.
- [ ] Portable ZIP, unsigned setup.exe, AppImage, Universal DMG, full source, and bilingual notes exist; GitHub asset digests are visible for integrity checks.
- [ ] Source archive contains MuPDF and all nested dependency source required by AGPL corresponding-source obligations.
- [ ] A clean VM runs `--version`, open, render, edit, save, reopen; unsigned-package warnings are visible in notes.

中文检查范围与以上英文逐项相同：精确依赖版本、三平台构建、独立校验、加解密、编辑/涂黑、水印、复杂语料、性能和内存、界面截图、完整源码及发布资产，缺一不可。
