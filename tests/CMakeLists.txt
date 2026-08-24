add_executable(nexpdf_core_tests test_document_session.cpp)
target_link_libraries(nexpdf_core_tests PRIVATE nexpdf_core Qt6::Test)

add_test(NAME nexpdf_core_tests COMMAND nexpdf_core_tests
  -o ${CMAKE_BINARY_DIR}/Testing/nexpdf-core-test.txt,txt)
set_tests_properties(nexpdf_core_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(WIN32)
  set_property(TEST nexpdf_core_tests PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

if(TARGET nexpdf_app_ui)
  add_executable(nexpdf_ui_tests test_main_window.cpp)
  target_link_libraries(nexpdf_ui_tests PRIVATE nexpdf_app_ui Qt6::Test)
  if(MSVC)
    target_compile_options(nexpdf_ui_tests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(nexpdf_ui_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME nexpdf_ui_tests COMMAND nexpdf_ui_tests
    -o ${CMAKE_BINARY_DIR}/Testing/nexpdf-ui-test.txt,txt)
  if(WIN32)
    set_tests_properties(nexpdf_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    set_property(TEST nexpdf_ui_tests PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  else()
    set_tests_properties(nexpdf_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
