add_library(nexpdf_app_ui STATIC
  app_icons.cpp
  app_icons.h
  app_translator.cpp
  app_translator.h
  crash_handler.cpp
  crash_handler.h
  main_window.cpp
  main_window.h
  pdf_canvas.cpp
  pdf_canvas.h
)

qt_add_resources(nexpdf_app_ui "nexpdf_assets"
  PREFIX "/nexpdf"
  BASE assets
  FILES assets/nexpdf-256.png)

target_include_directories(nexpdf_app_ui
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(nexpdf_app_ui PUBLIC nexpdf_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
target_compile_definitions(nexpdf_app_ui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

qt_add_executable(nexPDF main.cpp)
target_link_libraries(nexPDF PRIVATE nexpdf_app_ui)

if(WIN32)
  target_link_libraries(nexPDF PRIVATE dbghelp)
  # The resource template carries the file/product version, so Windows Explorer
  # and the installer show the same version as `nexPDF --version`. Forward
  # slashes keep the absolute icon path free of .rc escaping.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/nexpdf.ico" NEXPDF_ICON_PATH)
  configure_file(assets/nexpdf.rc "${CMAKE_BINARY_DIR}/generated/nexpdf.rc" @ONLY)
  target_sources(nexPDF PRIVATE "${CMAKE_BINARY_DIR}/generated/nexpdf.rc")
endif()

if(APPLE)
  set_source_files_properties(assets/nexpdf.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(nexPDF PRIVATE assets/nexpdf.icns)
  # A DMG built from nexPDF.app alone would otherwise ship without the project
  # and third-party notices. The LGPL text is optional at configure time but is
  # required for a compliant binary release.
  set(_nexpdf_notice_files
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
  if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-3.0.txt")
    list(APPEND _nexpdf_notice_files "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-3.0.txt")
  endif()
  set_source_files_properties(${_nexpdf_notice_files} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(nexPDF PRIVATE ${_nexpdf_notice_files})
endif()

if(MSVC)
  target_compile_options(nexpdf_app_ui PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(nexPDF PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(nexpdf_app_ui PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(nexPDF PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(nexPDF PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "nexPDF"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.nexpdf.desktop"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_ICON_FILE "nexpdf.icns")

include(GNUInstallDirs)
install(TARGETS nexPDF
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
  install(FILES assets/org.nexpdf.desktop.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME org.nexpdf.desktop.svg)
  install(FILES ../packaging/linux/org.nexpdf.desktop.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
