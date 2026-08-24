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
