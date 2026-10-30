# Release checklist / 发布检查表

## Current maintenance candidate / 当前维护候选版

Target: `v1.0.5-rc.1`, Qt 6.11.2, MuPDF 1.28.6 (`da5f5eb890d7b3fb76903160b5f7f48ba1301492`). Evidence for this batch belongs in [maintenance-1.0.5.md](maintenance-1.0.5.md). / 本轮证据集中记录，旧版验收不自动算作新版通过。

- [ ] Target commit passes Windows x64, Linux x86_64 and macOS Universal CI, including qpdf/Poppler checks. / 目标提交的三平台构建、测试与独立校验通过。
- [ ] Instrumented Linux ASan/UBSan fuzz smoke builds and runs. / Linux 插桩 fuzz 构建及限时冒烟通过。
- [x] Repeated AES-128/AES-256 saves, decryption, failed-save recovery, overwrite and saved undo branches pass. / 加解密连续保存、失败恢复、覆盖与撤销保存点回归通过。
- [x] Search slices, anonymous watermark batches, permissions, render cancellation and QPrinter PDF output pass. / 搜索、水印、权限、渲染取消与打印到 PDF 回归通过。
- [x] Same-corpus render/performance comparison and memory stability are recorded with limits. / 同语料性能与内存稳定性记录齐全并说明边界。
- [ ] Notes exist, working tree is clean and CI passes before an immutable tag is created. / 标签前发布说明、干净工作树与准确提交的 CI 均满足。
- [ ] Four packaging jobs pass; all five user assets exist with SHA-256 digests and measured sizes. / 四项打包全通过，五类附件及摘要、体积核验完成。
- [x] Windows portable runtime smoke passes; no tests, benchmark or Qt6Test.dll are shipped. / Windows 部署冒烟通过，正式包排除开发测试运行库。

This maintenance release remains a prerelease. Physical printer output, real macOS hardware and a complete high-DPI/IME/accessibility acceptance matrix have not been reaccepted by these automated tests. Cooperative native cancellation does not guarantee a hard shutdown deadline. / 本轮保持预发行版；实体打印、macOS 实机和完整高 DPI/输入法/无障碍矩阵不由自动测试替代，原生协作取消不保证硬性退出时限。

## Historical evidence / 历史证据

The published v1.0.4 baseline is commit `f1ff6f2cc6df1c6e7f898eac698533e823e1accf`; CI run [40052792712](https://github.com/YDLuo-1/nexPDF/actions/runs/40052792712) and release run [40054308333](https://github.com/YDLuo-1/nexPDF/actions/runs/40054308333) passed. The October 29 audit nevertheless reproduced defects missed by that suite. A green older run is therefore not evidence for the new boundary cases.

v1.0.2/v1.0.3 release notes retain their Windows installer, Linux/WSLg and embedded-font acceptance statements. The September 28 synthetic render figures (P50 2.6 ms, P95 4.1 ms, 330 pages/s, 50-cycle growth +0.6 MiB) are historical measurements, not current performance guarantees. macOS remained CI-only and v1.0.4 printing did not have physical-printer acceptance.
