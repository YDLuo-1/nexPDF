add_library(nexpdf_app_ui STATIC
  app_translator.cpp
  app_translator.h
  main_window.cpp
  main_window.h
  pdf_canvas.cpp
  pdf_canvas.h
)

target_include_directories(nexpdf_app_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nexpdf_app_ui PUBLIC nexpdf_core Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(nexpdf_app_ui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

qt_add_executable(nexPDF main.cpp)
target_link_libraries(nexPDF PRIVATE nexpdf_app_ui)

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
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

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
