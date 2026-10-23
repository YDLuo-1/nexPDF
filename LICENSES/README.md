# License texts for binary distribution / 二进制分发的许可证文本

nexPDF itself is AGPL-3.0-or-later (see [`LICENSE`](../LICENSE)). Binary
packages additionally contain:

- Qt 6.11.2 (LGPLv3), linked dynamically: `Qt6Core`, `Qt6Gui`, `Qt6Widgets`,
  `Qt6Svg`, platform/style/image-format plugins;
- MuPDF 1.28.2 (AGPL-3.0-or-later), linked statically;
- the Microsoft Visual C++ runtime on Windows (Microsoft redistribution terms);
- `opengl32sw.dll`, the Mesa software OpenGL fallback deployed by `windeployqt`
  (MIT).

## Required before publishing a binary release

Place the verbatim GNU Lesser General Public License v3 text at
`LICENSES/LGPL-3.0.txt`, copied from <https://www.gnu.org/licenses/lgpl-3.0.txt>
or from the Qt source package. Once the file exists, CMake installs it into
`share/nexpdf` for the Windows/Linux packages and embeds it into
`nexPDF.app/Contents/Resources` for the macOS DMG.

Do not retype the text by hand; the license must be reproduced verbatim.

LGPLv3 also requires that a user can replace the Qt libraries. nexPDF ships the
unmodified Qt DLLs/frameworks next to the executable, which keeps relinking
possible, but the copyright notice and license text still have to accompany the
package.

## Status

The LGPL text is not checked into this repository yet. CMake emits a warning at
configure time, and the release checklist keeps this as an open gate. Do not
publish a binary release that omits it.
