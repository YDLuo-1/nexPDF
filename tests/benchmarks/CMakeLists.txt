add_executable(nexpdf_benchmark render_benchmark.cpp)
target_link_libraries(nexpdf_benchmark PRIVATE nexpdf_core Qt6::Core Qt6::Test)

if(WIN32)
  add_custom_command(TARGET nexpdf_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:nexpdf_benchmark>
      $<TARGET_FILE_DIR:nexpdf_benchmark>
    COMMAND_EXPAND_LISTS
    COMMENT "Deploying benchmark runtime DLLs")
endif()

if(MSVC)
  target_compile_options(nexpdf_benchmark PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(nexpdf_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
