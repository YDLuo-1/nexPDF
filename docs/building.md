# Build and release / 构建与发布

## Versions / 版本

- CMake 3.25+
- C++20 compiler: MSVC 19.3+, GCC 13+, or Apple Clang 16+
- Qt 6.11.2 exactly: Core, Gui, Widgets, Test (plus the Svg runtime plugin that `windeployqt` ships)
- MuPDF 1.28.6 at commit `da5f5eb890d7b3fb76903160b5f7f48ba1301492`
- qpdf and Poppler are validation tools only, not runtime engines

正式构建使用上述精确版本。`find_package(Qt6 ... EXACT)` 要求安装版本与 `NEXPDF_QT_VERSION` 完全一致；开发者可以把它改成机器上实际安装的精确版本做非发布编译检查，但这样的产物不得作为正式 Release。

## MuPDF prefix / MuPDF 前缀

`FindMuPDF.cmake` intentionally rejects a source checkout without compiled libraries. Set `MUPDF_ROOT` to a prefix containing:

```text
include/mupdf/fitz.h
lib/libmupdf.lib (or libmupdf.a)
lib/libthirdparty.lib / libmupdf-third.a
lib/libresources.lib       # Windows
lib/libharfbuzz.lib        # Windows
lib/libpkcs7.lib           # Windows
```

Windows (the script discovers the latest installed Visual Studio 2022 through `vswhere` and builds the `libmupdf` solution target with the v143 toolset):

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-MuPDFWindows.ps1 -Configuration Release
```

Linux/macOS use the upstream Makefile with system libraries where practical and explicitly disable unavailable optional features. The Release CI records the exact command in its log; OCR/Tesseract, curl, GLUT, DOCX/ODT extraction, and barcode projects must not be linked.

## Configure / 配置

```bash
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.2/gcc_64 \
  -DMUPDF_ROOT=/opt/mupdf-1.28.6-slim
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

For macOS Universal, also set `-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64` and provide Universal Qt/MuPDF libraries.

## Independent validation / 独立校验

Release jobs must run:

```bash
qpdf --warning-exit-0 --check output.pdf
pdftoppm -png -f 1 -singlefile output.pdf output-page-1
```

`--warning-exit-0` keeps qpdf's warnings in the log while only real structural errors fail the build; different qpdf versions disagree about warnings on MuPDF output. MuPDF reopen validation is mandatory but not independent, so it cannot replace qpdf/Poppler checks. Passwords must be supplied through protected CI secrets or interactive test harnesses, never command output.

## Packaging / 打包

- Windows x64: portable ZIP and unsigned `setup.exe`
- Linux x86_64: AppImage
- macOS: unsigned Universal DMG
- every release: bilingual notes and corresponding source including MuPDF submodules; GitHub displays SHA-256 digests for uploaded assets

Unsigned packages must warn about SmartScreen/Gatekeeper. A source archive that omits nested MuPDF dependency contents does not satisfy the project release checklist.
