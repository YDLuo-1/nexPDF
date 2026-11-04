# Release checklist / 发布检查表

## Current maintenance candidate / 当前维护候选版

Target: `v1.0.5-rc.3`, Qt 6.11.2, MuPDF 1.28.6 (`da5f5eb890d7b3fb76903160b5f7f48ba1301492`). 本轮本地验收见 [2026-11-04 记录](acceptance-20261104.md)与[原始 JSON](acceptance-20261104.json)；最终提交的 CI/资产验证见 [RC.3 发布页](https://github.com/YDLuo-1/nexPDF/releases/tag/v1.0.5-rc.3)的验证附录。旧版本通过不自动算作新版通过。

### 已完成的本地项目

- [x] 最终 Windows 核心回归 31 项通过：连续加解密保存、失败恢复、权限、撤销、水印、搜索与渲染取消。
- [x] 实际 DPR 1/1.25/1.5/1.75/2 各通过 10 项 UI 检查，生成中英文截图并检查代表图。
- [x] 修复并回归未保存标题标记及中英文无障碍字段名称。
- [x] QPrinter PDF 页码范围、取消和文档切换回归通过；不代表系统打印驱动验收。
- [x] 10 份 Poppler 独立输出渲染通过，水印移除后像素恢复一致。
- [x] 当前 2800 页性能观察、5000 页/50 轮稳定性、15 页复杂语料均零错误，已记录负载和内存采样限制。
- [x] 本地先安装/部署 Qt 运行库，再检查依赖和启动；公开 Windows 包组成不依据本地完整 Qt 目录推断。

### 正式版仍缺少的验收

- [ ] 原生微软拼音组合、候选、提交与纯键盘完整操作；本轮界面控制由用户 Esc 中止。
- [ ] 原生 Microsoft Print to PDF 驱动输出以及实体打印机出纸、页序、边距与双面结果。
- [ ] 混合 DPI 多显示器切换、完整读屏软件矩阵。
- [ ] macOS 真机界面和打印检查，以及剩余完整产品性能门槛。

本轮保持预发行版；稳定 Latest 不因上述自动化通过而升级。原生协作取消不保证硬退出时限；测试通过不证明没有漏洞。

### 每次发布必须重新满足的门槛

1. 目标提交 Windows x64、Linux x86_64、macOS Universal CI 与 qpdf/Poppler 独立检查通过；完整核心 ASan/UBSan 和限定时间 fuzz 通过。
2. 对应发布说明存在、工作树干净、准确提交 CI 通过后才创建不可移动的递增 RC 标签。
3. Windows/Linux/macOS/完整源码四项打包成功后才发布五类用户附件；核验 SHA-256、包体积和 Windows 实际部署启动，排除测试/benchmark/Qt6Test.dll。
4. 在对应发布页保留准确提交、CI/Release 运行链接和下载后验证结果；本地报告不得提前勾选尚未发生的发布。

### RC.2 已确认的远端基线

`a70f5d598866f3ce26bc2a5d2bf5dae2dfac4102`：三平台及完整核心 ASan/UBSan、限时 fuzz 的 [CI 41008486970](https://github.com/YDLuo-1/nexPDF/actions/runs/41008486970)通过；[Release 41009604769](https://github.com/YDLuo-1/nexPDF/actions/runs/41009604769)的四类打包与发布通过。RC.2 五类附件、下载后的 Windows 运行库/启动与完整对应源码已验证。这些是历史基线，不替代 RC.3 的准确提交验证。

## Historical evidence / 历史证据

The published v1.0.4 baseline is commit `f1ff6f2cc6df1c6e7f898eac698533e823e1accf`; CI run [40052792712](https://github.com/YDLuo-1/nexPDF/actions/runs/40052792712) and release run [40054308333](https://github.com/YDLuo-1/nexPDF/actions/runs/40054308333) passed. The October 29 audit nevertheless reproduced defects missed by that suite. A green older run is therefore not evidence for the new boundary cases.

v1.0.2/v1.0.3 release notes retain their Windows installer, Linux/WSLg and embedded-font acceptance statements. The September 28 synthetic render figures (P50 2.6 ms, P95 4.1 ms, 330 pages/s, 50-cycle growth +0.6 MiB) are historical measurements, not current performance guarantees. macOS remained CI-only and v1.0.4 printing did not have physical-printer acceptance.
