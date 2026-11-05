# NVIDIA stdexec 0.11.0. Fetched only for the optional execution target.
set(DACOEXEC_STDEXEC_GIT_TAG
    "ead186b1d8db3ebe37a946ff84a6ce08bf795153"
    CACHE STRING "NVIDIA/stdexec source revision")

# Resolve one dependency: an existing target, then an installed package, then CPM.
function(_dacoexec_require_package)
  cmake_parse_arguments(
    ARG
    ""
    "NAME;TARGET;VERSION;GIT_TAG;GITHUB_REPOSITORY"
    "OPTIONS"
    ${ARGN})

  if(TARGET "${ARG_TARGET}")
    return()
  endif()

  if(ARG_VERSION)
    find_package(
      "${ARG_NAME}"
      ${ARG_VERSION}
      CONFIG
      QUIET)
  else()
    find_package("${ARG_NAME}" CONFIG QUIET)
  endif()

  if(TARGET "${ARG_TARGET}")
    return()
  endif()

  if(NOT COMMAND cpmaddpackage)
    include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")
  endif()

  set(package_arguments
      NAME
      "${ARG_NAME}"
      GITHUB_REPOSITORY
      "${ARG_GITHUB_REPOSITORY}"
      SYSTEM
      YES)
  if(ARG_GIT_TAG)
    list(
      APPEND
      package_arguments
      GIT_TAG
      "${ARG_GIT_TAG}")
  elseif(ARG_VERSION)
    list(
      APPEND
      package_arguments
      VERSION
      "${ARG_VERSION}")
  endif()
  if(ARG_OPTIONS)
    list(
      APPEND
      package_arguments
      OPTIONS
      ${ARG_OPTIONS})
  endif()

  cpmaddpackage(${package_arguments})
endfunction()

function(dacoexec_setup_dependencies)
  # Match CMakeLists.txt: tests are created only when this option is on and
  # CTest's BUILD_TESTING is not already OFF. An unset BUILD_TESTING is left
  # for include(CTest), which enables it without overriding a parent.
  set(dacoexec_tests_enabled FALSE)
  if(dacoexec_BUILD_TESTS AND (NOT DEFINED BUILD_TESTING OR BUILD_TESTING))
    set(dacoexec_tests_enabled TRUE)
  endif()
  set(dacoexec_tests_enabled
      "${dacoexec_tests_enabled}"
      PARENT_SCOPE)

  if(dacoexec_BUILD_EXECUTION)
    # Only install a dependency fetched for our standalone installation.
    # Existing targets and installed packages return before CPM options apply.
    set(_dacoexec_install_stdexec OFF)
    if(dacoexec_INSTALL AND PROJECT_IS_TOP_LEVEL)
      set(_dacoexec_install_stdexec ON)
    endif()
    _dacoexec_require_package(
      NAME
      stdexec
      TARGET
      STDEXEC::stdexec
      GIT_TAG
      "${DACOEXEC_STDEXEC_GIT_TAG}"
      GITHUB_REPOSITORY
      "NVIDIA/stdexec"
      OPTIONS
      "STDEXEC_BUILD_TESTS OFF"
      "STDEXEC_BUILD_EXAMPLES OFF"
      "STDEXEC_ENABLE_CUDA OFF"
      "STDEXEC_ENABLE_IO_URING OFF"
      "STDEXEC_INSTALL ${_dacoexec_install_stdexec}")
  endif()

  if(dacoexec_BUILD_CLI)
    _dacoexec_require_package(
      NAME
      CLI11
      TARGET
      CLI11::CLI11
      VERSION
      2.6.1
      GITHUB_REPOSITORY
      "CLIUtils/CLI11")
  endif()

  if(dacoexec_BUILD_BENCHMARKS)
    if(NOT dacoexec_BUILD_EXECUTION)
      message(FATAL_ERROR "Codec benchmarks require dacoexec_BUILD_EXECUTION=ON for parallel measurements")
    endif()
    _dacoexec_require_package(
      NAME
      benchmark
      TARGET
      benchmark::benchmark
      VERSION
      1.9.5
      GIT_TAG
      v1.9.5
      GITHUB_REPOSITORY
      "google/benchmark"
      OPTIONS
      "BENCHMARK_ENABLE_TESTING OFF"
      "BENCHMARK_ENABLE_INSTALL OFF"
      "BENCHMARK_ENABLE_WERROR OFF")
  endif()

  if(dacoexec_tests_enabled)
    _dacoexec_require_package(
      NAME
      Catch2
      TARGET
      Catch2::Catch2WithMain
      VERSION
      3.12.0
      GITHUB_REPOSITORY
      "catchorg/Catch2")
  endif()
endfunction()
