cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project(nexPDF VERSION 1.0.0 LANGUAGES CXX)

option(NEXPDF_BUILD_APP "Build the Qt desktop application" ON)
option(NEXPDF_BUILD_TESTS "Build tests" ON)
option(NEXPDF_BUILD_BENCHMARKS "Build the corpus benchmark tool" OFF)
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
  add_subdirectory(benchmarks)
endif()

include(GNUInstallDirs)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/nexpdf)
