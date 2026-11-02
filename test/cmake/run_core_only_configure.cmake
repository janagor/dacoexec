if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

function(
  dacoexec_assert_no_optional_downloads
  build_dir
  log_text
  label)
  string(TOLOWER "${log_text}" configure_log_lower)
  foreach(
    package IN
    ITEMS "catchorg/catch2"
          "fmtlib/fmt"
          "gabime/spdlog"
          "arthursonzogni/ftxui"
          "cliutils/cli11"
          "nvidia/stdexec"
          "cpm: adding package"
          "interprocedural optimization enabled"
          "global hardening enabled")
    string(FIND "${configure_log_lower}" "${package}" package_pos)
    if(NOT
       package_pos
       EQUAL
       -1)
      message(FATAL_ERROR "${label} fetched ${package}")
    endif()
  endforeach()

  if(EXISTS "${build_dir}/_deps")
    message(FATAL_ERROR "${label} populated _deps")
  endif()
  file(GLOB cpm_bootstrap "${build_dir}/cmake/CPM_*.cmake")
  if(cpm_bootstrap)
    message(FATAL_ERROR "${label} downloaded CPM: ${cpm_bootstrap}")
  endif()

  if(NOT EXISTS "${build_dir}/CMakeCache.txt")
    message(FATAL_ERROR "${label} did not write a cache")
  endif()
  file(READ "${build_dir}/CMakeCache.txt" cache_text)
  foreach(
    fetched IN
    ITEMS Catch2_SOURCE_DIR
          fmt_SOURCE_DIR
          spdlog_SOURCE_DIR
          FTXUI_SOURCE_DIR
          CLI11_SOURCE_DIR
          stdexec_SOURCE_DIR)
    string(FIND "${cache_text}" "${fetched}" fetched_pos)
    if(NOT
       fetched_pos
       EQUAL
       -1)
      message(FATAL_ERROR "${label} cache contains ${fetched}")
    endif()
  endforeach()
endfunction()

function(dacoexec_configure_without_downloads build_dir)
  file(REMOVE_RECURSE "${build_dir}")
  set(configure_command
      ${CMAKE_COMMAND} -S "${SOURCE_DIR}" -B "${build_dir}" ${ARGN}
      -Ddacoexec_BUILD_CLI=OFF -Ddacoexec_BUILD_FUZZ_TESTS=OFF -Ddacoexec_BUILD_BENCHMARKS=OFF
      -Ddacoexec_ENABLE_FORMATTING=OFF -Ddacoexec_INSTALL=OFF -Ddacoexec_ENABLE_CLANG_TIDY=OFF
      -Ddacoexec_ENABLE_CPPCHECK=OFF -Ddacoexec_ENABLE_SANITIZER_ADDRESS=OFF -Ddacoexec_ENABLE_SANITIZER_UNDEFINED=OFF
      -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF)
  if(GENERATOR)
    list(
      APPEND
      configure_command
      -G
      "${GENERATOR}")
  endif()
  if(GENERATOR_TOOLSET
     AND NOT
         GENERATOR_TOOLSET
         STREQUAL
         "")
    list(
      APPEND
      configure_command
      -T
      "${GENERATOR_TOOLSET}")
  endif()
  if(GENERATOR_PLATFORM
     AND NOT
         GENERATOR_PLATFORM
         STREQUAL
         "")
    list(
      APPEND
      configure_command
      -A
      "${GENERATOR_PLATFORM}")
  endif()
  if(BUILD_TYPE)
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
  endif()
  if(CXX_COMPILER
     AND NOT
         GENERATOR
         MATCHES
         "Visual Studio")
    list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
  endif()
  if(C_COMPILER
     AND NOT
         GENERATOR
         MATCHES
         "Visual Studio")
    list(APPEND configure_command "-DCMAKE_C_COMPILER=${C_COMPILER}")
  endif()
  if(CXX_FLAGS)
    list(APPEND configure_command "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}")
  endif()
  if(LINKER_FLAGS)
    list(APPEND configure_command "-DCMAKE_EXE_LINKER_FLAGS=${LINKER_FLAGS}")
  endif()

  execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
  if(NOT
     configure_result
     EQUAL
     0)
    message(FATAL_ERROR "Configure failed (${configure_result})\n${configure_output}\n${configure_error}")
  endif()
  set(configure_log
      "${configure_output}${configure_error}"
      PARENT_SCOPE)
endfunction()

dacoexec_configure_without_downloads("${BUILD_DIR}/Testing/Temporary/core-only" -Ddacoexec_BUILD_TESTS=OFF)
dacoexec_assert_no_optional_downloads("${BUILD_DIR}/Testing/Temporary/core-only" "${configure_log}"
                                      "Core-only configuration")

# Requesting tests must not fetch Catch2 when CTest is already switched off.
dacoexec_configure_without_downloads("${BUILD_DIR}/Testing/Temporary/tests-suppressed" -Ddacoexec_BUILD_TESTS=ON
                                     -DBUILD_TESTING=OFF)
dacoexec_assert_no_optional_downloads("${BUILD_DIR}/Testing/Temporary/tests-suppressed" "${configure_log}"
                                      "Tests suppressed by BUILD_TESTING=OFF")
if(configure_log MATCHES "Building tests")
  message(FATAL_ERROR "BUILD_TESTING=OFF still created the test tree")
endif()

message(STATUS "Core-only configuration did not fetch optional dependencies")
