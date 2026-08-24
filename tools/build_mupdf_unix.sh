#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
mupdf_root="${MUPDF_SOURCE:-$repo_root/third_party/mupdf}"
output_root="${1:-$repo_root/out/mupdf-unix}"
jobs="${NEXPDF_BUILD_JOBS:-2}"
feature_flags="-DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -DFZ_ENABLE_BARCODE=0"
arch_flags=""
if [[ "$(uname -s)" == "Darwin" && "${NEXPDF_MACOS_UNIVERSAL:-1}" == "1" ]]; then
  arch_flags="-arch arm64 -arch x86_64"
fi

make -C "$mupdf_root" -j"$jobs" \
  build=release shared=no \
  HAVE_CURL=no HAVE_GLUT=no HAVE_X11=no \
  HAVE_TESSERACT=no HAVE_LEPTONICA=no HAVE_ZXINGCPP=no \
  XCFLAGS="$feature_flags" ARCHFLAGS="$arch_flags" libs

mkdir -p "$output_root/include" "$output_root/lib"
rm -rf "$output_root/include/mupdf"
cp -R "$mupdf_root/include/mupdf" "$output_root/include/"
cp "$mupdf_root/build/release/libmupdf.a" "$output_root/lib/"
if [[ -f "$mupdf_root/build/release/libmupdf-third.a" ]]; then
  cp "$mupdf_root/build/release/libmupdf-third.a" "$output_root/lib/"
fi

echo "Slim MuPDF prefix: $output_root"
