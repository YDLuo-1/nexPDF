# nexPDF 代理执行规则

## 执行前判断

- 在执行任务前检查需求中的错误前提、逻辑漏洞、信息缺失和隐藏风险。
- 不默认接受既有方案；发现更安全、简单或可维护的方案时直接说明。
- 明确区分已确认事实、合理推测和未验证假设。
- 涉及代码、数据、版本和技术结论时先验证，不编造结果。
- 信息不足且不同选择会实质改变结果时，先向用户确认。
- 修改前评估长期维护成本，并保护用户已有的无关改动。

## Windows Qt 程序启动前置检查

2026-08-24 曾发生过两类可避免的系统弹窗：直接启动未部署 Qt 运行库的构建目录 EXE；以及把链接 `Qt6::Test` 的 `nexpdf_benchmark.exe` 复制到只部署了应用运行库的目录，导致缺少 `Qt6Test.dll`。以后必须遵守：

1. 启动任何 Windows EXE 前，先确认其运行时 DLL 已在 EXE 同目录，或通过受控测试环境提供；不得直接试运行依赖尚未部署的构建产物。
2. `nexPDF.exe` 的本地发布冒烟必须先执行 `cmake --install` 和 `windeployqt`，并确认 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll` 与 `platforms/qwindows.dll` 存在。
3. `nexpdf_core_tests.exe` 和 `nexpdf_benchmark.exe` 额外依赖 `Qt6Test.dll`。优先通过配置好测试环境的 CTest 运行测试；运行基准前必须确认 `Qt6Test.dll` 可解析，不能假定应用的部署目录包含测试模块。
4. 启动前使用 `dumpbin /dependents` 或等效只读检查核对依赖，并用文件存在性检查验证部署结果。发现缺失 DLL 时停止，不得靠试运行触发 Windows 错误对话框。
5. 自动化启动必须有明确超时和残留进程清理；出现系统模态错误框时立即停止相关进程，先修复部署环境再重试。
6. `nexpdf_benchmark` 的 Windows 构建必须通过 CMake 的 `TARGET_RUNTIME_DLLS` 把 Qt 运行库部署到自身目录；最终用户包必须排除 benchmark、测试程序和 `Qt6Test.dll`，不能把开发工具混入应用部署目录。

## Qt、MuPDF 与三平台 CI 注意事项

2026-08-24 首次建立公开仓库和三平台流水线时连续暴露了以下问题。后续不得凭“安装成功”“编译成功”或单平台通过就推断发布可用。

### 已确认问题与固定规则

1. 本地 Windows 构建基线固定为用户安装的 `D:\Qt\6.11.2\msvc2022_64`、Visual Studio 2022 和其内置 CMake。不得退回项目内私装 Qt，也不得混用其他 Qt 版本生成的缓存；切换工具链后必须全新配置构建目录。
2. GitHub Actions 必须使用同一 Qt 6.11.2。`aqtinstall` 的 v3.3.0 标签缺少 Qt 6.11 Windows 新目录兼容修复，Windows 安装需继续固定到已验证提交 `8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b`，除非新的正式版本经实际 CI 验证后再升级。
3. `install-qt-action` 的归档不能随意裁剪：Windows 使用 `qtbase`；Ubuntu 24.04 还需要 `icu`，否则 Qt 的 `rcc` 会因缺少 ICU 动态库而无法启动；macOS 使用 `qtbase`。修改归档列表后必须验证配置、编译和测试三步，而不能只看下载成功。
4. Windows CI 需要 VS2022/v143 时固定使用 `windows-2022`。不得假定 `windows-latest` 永远提供 Visual Studio 2022；切换镜像前先检查实际 MSBuild、工具集和生成器版本。
5. Chocolatey 的 `poppler` 26.6.0 包只展开源码，不提供 `pdftoppm.exe`。不得把 `choco install poppler` 的成功状态当成 Poppler 工具可用。Windows 独立渲染校验使用固定的 Poppler 26.02.0-0 预编译包，校验 SHA-256 `993E4A94376ED712FAFC7058D724EA0B943D118BBD2305CD9ED55174EB85CDA5`，确认 `pdftoppm.exe` 存在，再把其 `bin` 写入 `GITHUB_PATH`；该流程已通过 Windows Actions 实证。
6. qpdf 的退出码 3 表示存在警告但没有结构错误；不同系统仓库中的 qpdf 版本可能对同一 MuPDF 输出给出不同警告。结构门槛使用 `qpdf --warning-exit-0 --check` 保留警告文本，同时只让真实错误导致失败；不得用 `|| true` 吞掉全部错误。
7. macOS Universal 的 MuPDF 必须分别生成 arm64 与 x86_64 静态库，再用 `lipo` 合并。MuPDF Makefile 的 `ARCHFLAGS` 不会自动进入所有 C 编译命令，架构参数还必须通过 `XCFLAGS` 传入；合并前后都要用 `lipo -info` 验证每个静态库，不能只根据输出目录名判断架构。
8. MuPDF Makefile 的 `build=` 只接受其支持的构建类型，架构差异放在独立 `OUT` 目录中。不得把 `release-arm64` 一类自定义目录名误当成合法 `build` 类型。
9. 工作线程中的 `QObject` 不得在以自身为接收者的事件处理中直接 `delete`。按 Qt 推荐模式把 `QThread::finished` 连接到 `QObject::deleteLater`，退出前先阻塞完成文档关闭和渲染池清理。
10. 设置 `MUPDF_ROOT` 后，`FindMuPDF.cmake` 必须使用 `NO_DEFAULT_PATH` 查找 MuPDF 及其拆分组件。不得在指定前缀缺少可选库时回退到全系统搜索，否则会把 Homebrew 的单架构 HarfBuzz/PKCS7 混入 macOS Universal 链接。
11. MuPDF 静态库必须排在 QtGui 之前链接。macOS LLDB 已证明反向顺序会让 MuPDF 的 `FT_Load_Glyph` 跳进 QtGui 内部另一套 FreeType 实现并崩溃；调整 CMake 链接依赖后必须检查实际链接命令，不能只看 `target_link_libraries` 源码顺序。
12. GitHub HTTPS 偶发 TLS 握手中断时，只重试同一提交，可临时尝试 HTTP/1.1；不得把访问令牌写进远程 URL、命令或日志，也不得因重试重复创建内容相同的提交。

### 当前未解决项

- 截至 2026-08-24，macOS Universal 的 `nexpdf_core_tests` 曾在首次正常渲染期间 SIGSEGV。完整回溯为 `pdf_load_simple_font -> FT_Get_Advance -> FT_Load_Glyph -> QtGui`，并且链接日志确认误混入 Homebrew arm64 HarfBuzz/PKCS7。限定 MuPDF 搜索前缀并把 MuPDF 静态库移到 QtGui 前的修复尚待下一轮 macOS Actions 实证；通过前禁止跳过测试、标记允许失败或宣称 macOS 已修复。

## 发布纪律

1. 创建标签前必须确认目标提交的三平台 CI 全部通过、工作树干净、对应 `docs/release-notes/<tag>.md` 已存在。
2. 正式 `v1.0.0`/Latest 还需要计划规定的人工界面检查和完整性能验收。仅自动化和打包通过时先发布 `v1.0.0-rc.1` 预发行版，不得提前降低门槛或把 RC 标成 Latest。
3. 发布工作流必须在 Windows、Linux、macOS 和完整源码包四项都成功后再创建 Release；任何平台失败都不得手工拼凑成“完整三平台发布”。
4. Windows 最终资产应同时提供便携 ZIP 和安装程序 EXE。便携版不能是孤立单 EXE，因为 Qt DLL 与平台插件属于必需运行时；测试程序、benchmark 和 `Qt6Test.dll` 不得进入最终用户包。
