# Find a MuPDF 1.28.x C library without imposing an additional PDF engine.
#
# Set MUPDF_ROOT to a prefix containing include/mupdf/fitz.h and the MuPDF
# libraries. A checked-out source tree alone is intentionally not accepted:
# nexPDF packages use a slim MuPDF build with OCR, curl and command-line tools
# disabled (see docs/building.md).

set(_mupdf_hints)
if(MUPDF_ROOT)
  list(APPEND _mupdf_hints "${MUPDF_ROOT}")
endif()
if(DEFINED ENV{MUPDF_ROOT})
  list(APPEND _mupdf_hints "$ENV{MUPDF_ROOT}")
endif()

find_path(MuPDF_INCLUDE_DIR
  NAMES mupdf/fitz.h
  HINTS ${_mupdf_hints}
  PATH_SUFFIXES include)

find_library(MuPDF_LIBRARY
  NAMES mupdf libmupdf
  HINTS ${_mupdf_hints}
  PATH_SUFFIXES lib lib64 platform/win32/x64/Release)

find_library(MuPDF_THIRD_LIBRARY
  NAMES mupdf-third thirdparty libthirdparty
  HINTS ${_mupdf_hints}
  PATH_SUFFIXES lib lib64 platform/win32/x64/Release)

set(_mupdf_extra_libraries)
foreach(_component IN ITEMS resources harfbuzz pkcs7)
  string(TOUPPER "${_component}" _component_upper)
  find_library(MuPDF_${_component_upper}_LIBRARY
    NAMES "mupdf-${_component}" "lib${_component}" "${_component}"
    HINTS ${_mupdf_hints}
    PATH_SUFFIXES lib lib64 platform/win32/x64/Release)
  if(MuPDF_${_component_upper}_LIBRARY)
    list(APPEND _mupdf_extra_libraries "${MuPDF_${_component_upper}_LIBRARY}")
  endif()
endforeach()

if(MuPDF_INCLUDE_DIR AND EXISTS "${MuPDF_INCLUDE_DIR}/mupdf/fitz/version.h")
  file(STRINGS "${MuPDF_INCLUDE_DIR}/mupdf/fitz/version.h" _mupdf_version_line
    REGEX "^#define FZ_VERSION \"[0-9]+\\.[0-9]+\\.[0-9]+\"")
  string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
    MuPDF_VERSION "${_mupdf_version_line}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MuPDF
  REQUIRED_VARS MuPDF_INCLUDE_DIR MuPDF_LIBRARY
  VERSION_VAR MuPDF_VERSION)

if(MuPDF_FOUND AND NOT TARGET MuPDF::MuPDF)
  add_library(MuPDF::MuPDF UNKNOWN IMPORTED)
  set_target_properties(MuPDF::MuPDF PROPERTIES
    IMPORTED_LOCATION "${MuPDF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MuPDF_INCLUDE_DIR}")
  if(MuPDF_THIRD_LIBRARY)
    set_property(TARGET MuPDF::MuPDF APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "${MuPDF_THIRD_LIBRARY}")
  endif()
  if(_mupdf_extra_libraries)
    set_property(TARGET MuPDF::MuPDF APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "${_mupdf_extra_libraries}")
  endif()
  if(WIN32)
    set_property(TARGET MuPDF::MuPDF APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "advapi32;crypt32;gdi32;user32")
  endif()
  if(UNIX AND NOT APPLE)
    set_property(TARGET MuPDF::MuPDF APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "m;pthread;dl")
  endif()
endif()

mark_as_advanced(MuPDF_INCLUDE_DIR MuPDF_LIBRARY MuPDF_THIRD_LIBRARY
  MuPDF_RESOURCES_LIBRARY MuPDF_HARFBUZZ_LIBRARY MuPDF_PKCS7_LIBRARY)
