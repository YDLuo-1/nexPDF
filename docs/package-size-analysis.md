# Package size analysis / 发布包体积分析

## 中文

`v1.0.0-rc.3` 的 macOS Universal 未签名 DMG 实测为 86,748,987 字节（82.730 MiB），比 80 MiB 目标高 2.730 MiB。Windows 和 Linux Actions 资产压缩归档分别约 72.1 MiB 和 56.6 MiB；本次超标仅发生在同时包含 arm64 与 x86_64 代码的 macOS Universal 包。

使用 7-Zip 24.08 只读解析 DMG 中的 APFS 内容，主要逻辑组成如下：

| 组件 | 逻辑大小（MiB） | 说明 |
|---|---:|---|
| `Contents/MacOS/nexPDF` | 82.450 | 同时包含两种架构的 nexPDF 与静态链接 MuPDF，是主要来源 |
| `QtGui.framework` | 17.080 | Universal Qt GUI 运行库 |
| `QtWidgets.framework` | 12.170 | Universal Qt Widgets 运行库 |
| `QtCore.framework` | 12.140 | Universal Qt Core 运行库 |
| Qt 插件 | 3.910 | Cocoa 平台、macOS 样式及图片格式插件 |
| `QtDBus.framework` | 1.410 | macOS 部署工具解析出的运行时依赖 |
| 资源与元数据 | 0.150 | 应用图标、配置和签名元数据 |

逻辑内容会由 UDZO/ZLIB 压缩，因此各项逻辑大小之和不等于 DMG 物理大小。没有通过删除中文翻译、CJK 支持或用户功能来降低体积。RC 阶段接受该 2.730 MiB 偏差并公开实测报告；正式 v1.0.0 前将评估不改变功能的 Release 符号裁剪、MuPDF 链接裁剪及可证明无用的部署组件，并在 macOS arm64/x86_64 冒烟和渲染回归通过后才采用。

## English

The unsigned macOS Universal DMG from `v1.0.0-rc.3` measures 86,748,987 bytes (82.730 MiB), which is 2.730 MiB above the 80 MiB target. The compressed Windows and Linux Actions artifacts are approximately 72.1 MiB and 56.6 MiB respectively; only the macOS package containing both arm64 and x86_64 code exceeded the target.

A read-only 7-Zip 24.08 inspection of the APFS payload produced the component measurements above. The 82.450 MiB Universal nexPDF executable, including statically linked MuPDF for both architectures, is the dominant component, followed by the Universal Qt frameworks. UDZO/ZLIB compresses the logical contents, so their sum is not the physical DMG size.

No Chinese translation, CJK support, or user feature was removed to reduce the package. This 2.730 MiB deviation is documented for the release-candidate phase. Before final v1.0.0, the project will evaluate function-preserving Release symbol stripping, MuPDF link trimming, and demonstrably unused deployment components; any change must pass arm64/x86_64 macOS smoke and rendering regression tests.
