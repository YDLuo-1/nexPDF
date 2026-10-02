cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project(nexPDF VERSION 1.0.0 LANGUAGES CXX)

set(NEXPDF_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string reported by nexPDF --version; release packaging passes the release tag")
configure_file(app/version.h.in "${CMAKE_BINARY_DIR}/generated/nexpdf/version.h" @ONLY)

option(NEXPDF_BUILD_APP "Build the Qt desktop application" ON)
option(NEXPDF_BUILD_TESTS "Build tests" ON)
option(NEXPDF_BUILD_BENCHMARKS "Build the corpus benchmark tool" OFF)
option(NEXPDF_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)
option(NEXPDF_ENABLE_SANITIZERS "Enable supported compiler sanitizers" OFF)
set(NEXPDF_QT_VERSION "6.11.2" CACHE STRING "Exact Qt version used to build nexPDF")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 ${NEXPDF_QT_VERSION} EXACT REQUIRED COMPONENTS Core Gui Widgets)
if(NEXPDF_BUILD_TESTS OR NEXPDF_BUILD_BENCHMARKS)
  find_package(Qt6 ${NEXPDF_QT_VERSION} EXACT REQUIRED COMPONENTS Test)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(MuPDF 1.28.2 REQUIRED)

if(NEXPDF_ENABLE_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

if(NEXPDF_BUILD_APP)
  add_subdirectory(app)
endif()

if(NEXPDF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NEXPDF_BUILD_BENCHMARKS)
  add_subdirectory(tests/benchmarks)
endif()

if(NEXPDF_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NEXPDF_BUILD_FUZZERS requires a Clang toolchain (libFuzzer)")
  endif()
  add_executable(nexpdf_fuzz_open tests/fuzz/document_open_fuzzer.cpp)
  target_compile_features(nexpdf_fuzz_open PRIVATE cxx_std_20)
  target_compile_options(nexpdf_fuzz_open PRIVATE -fsanitize=fuzzer -fno-omit-frame-pointer)
  target_link_options(nexpdf_fuzz_open PRIVATE -fsanitize=fuzzer)
  target_link_libraries(nexpdf_fuzz_open PRIVATE nexpdf_core Qt6::Core)
endif()

include(GNUInstallDirs)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/nexpdf)
