# Third-party notices / 第三方声明

## MuPDF 1.28.2

- Source: https://github.com/ArtifexSoftware/mupdf
- License: GNU Affero General Public License v3 or later (commercial terms are separately available from Artifex)
- nexPDF pins commit `fe374accd98a43174a328fa7980d7675e06d5b0d` and uses a slim build without OCR/Tesseract, curl, GLUT, DOCX/ODT extraction, or barcode components.

MuPDF embeds or links additional third-party codec, font, shaping, color, compression, HTML, and scripting libraries. Their license files are preserved in the complete `third_party/mupdf` source tree and must be included in source distributions.

## Qt 6.11.2

- Source: https://code.qt.io/
- Components used: Qt Core, Gui, Widgets, Svg (pulled in by the deployed `qsvg` plugin), plus the platform/style/image-format/icon-engine plugins copied by `windeployqt`.
- Qt is available under LGPLv3/GPLv2/GPLv3 and commercial licensing terms. nexPDF's AGPL distribution must retain the applicable Qt notices, ship the LGPLv3 text, and permit user relinking where the LGPL terms require it. The unmodified Qt libraries are shipped next to the executable, so relinking remains possible.
- The LGPLv3 text belongs at `LICENSES/LGPL-3.0.txt`; see [`LICENSES/README.md`](LICENSES/README.md).
- WebEngine is not used.

## Mesa software OpenGL / Mesa 软件 OpenGL

Windows packages produced by `windeployqt` include `opengl32sw.dll`, the Mesa 3D software rasterizer used as a fallback when no usable GPU driver is present. Mesa is distributed under MIT-style licenses; its copyright and license text must remain available with the binary package.

## Fonts / 字体

nexPDF does not bundle proprietary document fonts. Text-watermark rasterization uses a font selected from the user's operating system unless a separately licensed redistributable font is added to a release. Release packaging must list every bundled font and its license.

## Microsoft Visual C++ Runtime / Microsoft Visual C++ 运行库

Windows binary packages may include the Microsoft Visual C++ Redistributable runtime DLLs required by Qt and nexPDF. These files remain subject to the Microsoft Visual Studio licensing terms and are copied only from the Visual Studio `Redist` directory; they are not part of nexPDF's AGPL-covered source code.

## Validation-only tools / 仅校验工具

qpdf and Poppler are used in CI or developer validation. They are not linked into nor distributed as the nexPDF runtime engine unless a future release explicitly says otherwise.
