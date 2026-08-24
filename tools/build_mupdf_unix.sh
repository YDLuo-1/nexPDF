#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
mupdf_root="${MUPDF_SOURCE:-$repo_root/third_party/mupdf}"
output_root="${1:-$repo_root/out/mupdf-unix}"
jobs="${NEXPDF_BUILD_JOBS:-2}"
feature_flags="-DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -DFZ_ENABLE_BARCODE=0"
arch_flags=""

mkdir -p "$output_root/include" "$output_root/lib"
rm -rf "$output_root/include/mupdf"
cp -R "$mupdf_root/include/mupdf" "$output_root/include/"

build_mupdf() {
  local build_name="$1"
  local build_arch_flags="$2"
  make -C "$mupdf_root" -j"$jobs" \
    build="$build_name" shared=no \
    HAVE_CURL=no HAVE_GLUT=no HAVE_X11=no \
    HAVE_TESSERACT=no HAVE_LEPTONICA=no HAVE_ZXINGCPP=no \
    XCFLAGS="$feature_flags" ARCHFLAGS="$build_arch_flags" libs
}

if [[ "$(uname -s)" == "Darwin" && "${NEXPDF_MACOS_UNIVERSAL:-1}" == "1" ]]; then
  build_mupdf release-arm64 "-arch arm64"
  build_mupdf release-x86_64 "-arch x86_64"
  for library in libmupdf.a libmupdf-third.a; do
    arm_library="$mupdf_root/build/release-arm64/$library"
    x86_library="$mupdf_root/build/release-x86_64/$library"
    if [[ -f "$arm_library" && -f "$x86_library" ]]; then
      lipo -create "$arm_library" "$x86_library" -output "$output_root/lib/$library"
    elif [[ -f "$arm_library" || -f "$x86_library" ]]; then
      echo "MuPDF library is missing one macOS architecture: $library" >&2
      exit 1
    fi
  done
else
  build_mupdf release "$arch_flags"
  cp "$mupdf_root/build/release/libmupdf.a" "$output_root/lib/"
  if [[ -f "$mupdf_root/build/release/libmupdf-third.a" ]]; then
    cp "$mupdf_root/build/release/libmupdf-third.a" "$output_root/lib/"
  fi
fi

echo "Slim MuPDF prefix: $output_root"
