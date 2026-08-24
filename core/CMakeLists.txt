add_library(nexpdf_core STATIC
  include/nexpdf/document_session.h
  include/nexpdf/types.h
  src/document_session.cpp
  src/mupdf_runtime.cpp
  src/mupdf_runtime.h
)

target_include_directories(nexpdf_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(nexpdf_core
  PRIVATE MuPDF::MuPDF
  PUBLIC Qt6::Core Qt6::Gui)

target_compile_definitions(nexpdf_core
  PUBLIC NEXPDF_VERSION="${PROJECT_VERSION}"
  PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

if(MSVC)
  target_compile_options(nexpdf_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(nexpdf_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
