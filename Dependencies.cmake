# NVIDIA stdexec 0.11.0. Registered for a later execution target. PR 0 does not
# fetch it; the core library has no execution-framework dependency.
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
