cmake_minimum_required(VERSION 3.29)

# Disable modules support. C++ module scanning is not part of this library and
# currently breaks clang-tidy.
cmake_policy(SET CMP0155 OLD)

# Standalone builds default to C++20. An enclosing project may already have
# selected a language mode; do not override it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
  endif()
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

project(
  dacoexec
  VERSION 0.0.2
  DESCRIPTION "Header-only C++20 library"
  HOMEPAGE_URL "https://github.com/janagor/dacoexec"
  LANGUAGES CXX)

# Set by CI to prove the selected compiler, not merely the generator.
# CMAKE_CXX_COMPILER_VERSION is established by project() and is not written to
# CMakeCache.txt.
if(dacoexec_REQUIRE_CXX_COMPILER_VERSION)
  string(
    REPLACE "."
            "\\."
            _dacoexec_required_compiler_regex
            "${dacoexec_REQUIRE_CXX_COMPILER_VERSION}")
  if(NOT
     CMAKE_CXX_COMPILER_VERSION
     MATCHES
     "^${_dacoexec_required_compiler_regex}($|\\.)")
    message(FATAL_ERROR "CMAKE_CXX_COMPILER_VERSION is '${CMAKE_CXX_COMPILER_VERSION}' (${CMAKE_CXX_COMPILER_ID}), "
                        "expected ${dacoexec_REQUIRE_CXX_COMPILER_VERSION}")
  endif()
  message(STATUS "Verified ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

include(cmake/PreventInSourceBuilds.cmake)

include(ProjectOptions.cmake)

dacoexec_setup_options()

if(dacoexec_BUILD_FUZZ_TESTS)
  message(
    FATAL_ERROR
      "dacoexec_BUILD_FUZZ_TESTS is not implemented yet. The inherited sample harness is not a dacoexec codec test.")
endif()

if(dacoexec_BUILD_BENCHMARKS)
  message(
    FATAL_ERROR "dacoexec_BUILD_BENCHMARKS is not implemented yet. Codec benchmarks are added in a later revision.")
endif()

# IPO and directory-wide hardening must run for a standalone build, and only
# then. Fetched dependencies are configured after this call.
if(PROJECT_IS_TOP_LEVEL)
  dacoexec_global_options()
endif()

include(Dependencies.cmake)
dacoexec_setup_dependencies()

dacoexec_local_options()

add_library(dacoexec::dacoexec_options ALIAS dacoexec_options)
add_library(dacoexec::dacoexec_warnings ALIAS dacoexec_warnings)

add_library(dacoexec INTERFACE)
add_library(dacoexec::dacoexec ALIAS dacoexec)

set_target_properties(dacoexec PROPERTIES EXPORT_NAME dacoexec)
target_compile_features(dacoexec INTERFACE cxx_std_20)

file(
  GLOB_RECURSE
  DACOEXEC_HEADERS
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/include/dacoexec/*.hpp")
set(DACOEXEC_EXECUTION_HEADERS
  "${PROJECT_SOURCE_DIR}/include/dacoexec/execution/ordered_parallel.hpp"
  "${PROJECT_SOURCE_DIR}/include/dacoexec/huffman/execution.hpp")
list(REMOVE_ITEM DACOEXEC_HEADERS ${DACOEXEC_EXECUTION_HEADERS})
if(NOT DACOEXEC_HEADERS)
  message(FATAL_ERROR "No public headers found under include/dacoexec")
endif()

target_sources(
  dacoexec
  INTERFACE FILE_SET
            public_headers
            TYPE
            HEADERS
            BASE_DIRS
            "${PROJECT_SOURCE_DIR}/include"
            FILES
            ${DACOEXEC_HEADERS})

include(GNUInstallDirs)
target_include_directories(dacoexec INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(dacoexec_BUILD_EXECUTION)
  add_library(dacoexec_execution INTERFACE)
  add_library(dacoexec::execution ALIAS dacoexec_execution)
  set_target_properties(dacoexec_execution PROPERTIES EXPORT_NAME execution)
  target_compile_features(dacoexec_execution INTERFACE cxx_std_20)
  target_link_libraries(dacoexec_execution INTERFACE dacoexec::dacoexec STDEXEC::stdexec)
  target_sources(dacoexec_execution INTERFACE FILE_SET execution_headers TYPE HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include" FILES ${DACOEXEC_EXECUTION_HEADERS})
endif()

if(DEFINED CMAKE_CXX_STANDARD)
  set(_dacoexec_language_mode "C++${CMAKE_CXX_STANDARD}")
else()
  set(_dacoexec_language_mode "compiler default; public target requires C++20")
endif()
message(STATUS "dacoexec ${PROJECT_VERSION}")
message(STATUS "  compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER})")
message(STATUS "  language mode : ${_dacoexec_language_mode}")
message(STATUS "  tests         : ${dacoexec_BUILD_TESTS}")
message(STATUS "  install       : ${dacoexec_INSTALL}")

add_subdirectory(src)
if(dacoexec_BUILD_CLI AND NOT TARGET dacoexec_cli)
  message(FATAL_ERROR "dacoexec_BUILD_CLI is enabled but the CLI target was not created")
endif()

if(PROJECT_IS_TOP_LEVEL AND dacoexec_ENABLE_FORMATTING)
  include(cmake/Format.cmake)
  dacoexec_enable_formatting()
endif()

if(dacoexec_tests_enabled)
  include(CTest)
  if(BUILD_TESTING)
    message(STATUS "Building tests")
    add_subdirectory(test)
  endif()
endif()

if(MSVC AND PROJECT_IS_TOP_LEVEL)
  include(cmake/Utilities.cmake)
  get_all_installable_targets(all_targets)
  foreach(target IN LISTS all_targets)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
      set_target_properties(${target} PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
    endif()
  endforeach()
endif()

if(dacoexec_INSTALL AND NOT CMAKE_SKIP_INSTALL_RULES)
  include(CMakePackageConfigHelpers)

  set(DACOEXEC_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/dacoexec")

  install(
    TARGETS dacoexec
    EXPORT dacoexecTargets
    FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  install(
    EXPORT dacoexecTargets
    FILE dacoexecTargets.cmake
    NAMESPACE dacoexec::
    DESTINATION "${DACOEXEC_CMAKE_INSTALL_DIR}")

  if(dacoexec_BUILD_EXECUTION)
    install(TARGETS dacoexec_execution EXPORT dacoexecExecutionTargets
      FILE_SET execution_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT dacoexecExecutionTargets FILE dacoexecExecutionTargets.cmake
      NAMESPACE dacoexec:: DESTINATION "${DACOEXEC_CMAKE_INSTALL_DIR}")
  endif()

  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/dacoexecConfig.cmake.in" "${PROJECT_BINARY_DIR}/dacoexecConfig.cmake"
    INSTALL_DESTINATION "${DACOEXEC_CMAKE_INSTALL_DIR}")

  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/dacoexecConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

  install(FILES "${PROJECT_BINARY_DIR}/dacoexecConfig.cmake" "${PROJECT_BINARY_DIR}/dacoexecConfigVersion.cmake"
          DESTINATION "${DACOEXEC_CMAKE_INSTALL_DIR}")
endif()
