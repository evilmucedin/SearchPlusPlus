cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

project(SearchPlusPlus
  VERSION 0.1.0
  DESCRIPTION "ElasticSearch-inspired full-text search engine in C++."
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SPP_BUILD_TESTS         "Build unit + integration tests"     ON)
option(SPP_BUILD_BENCH         "Build microbenchmarks"              ON)
option(SPP_BUILD_TOOLS         "Build CLI tools (spp_index/search/serve)" ON)
option(SPP_ENABLE_SANITIZERS   "Enable AddressSanitizer + UBSan"    OFF)
option(SPP_ENABLE_LTO          "Enable link-time optimization"      OFF)
option(SPP_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"  ON)

if(SPP_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _spp_ipo_ok OUTPUT _spp_ipo_msg)
  if(_spp_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "LTO requested but not supported: ${_spp_ipo_msg}")
  endif()
endif()

include(cmake/spp_warnings.cmake)
include(cmake/spp_sanitizers.cmake)

add_subdirectory(src)

if(SPP_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(SPP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SPP_BUILD_BENCH)
  add_subdirectory(bench)
endif()

message(STATUS "SearchPlusPlus configured:")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests        : ${SPP_BUILD_TESTS}")
message(STATUS "  Benchmarks   : ${SPP_BUILD_BENCH}")
message(STATUS "  Tools        : ${SPP_BUILD_TOOLS}")
message(STATUS "  Sanitizers   : ${SPP_ENABLE_SANITIZERS}")
message(STATUS "  LTO          : ${SPP_ENABLE_LTO}")
message(STATUS "  Warn as err  : ${SPP_WARNINGS_AS_ERRORS}")
