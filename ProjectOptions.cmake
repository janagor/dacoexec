include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)

include(CheckCXXSourceCompiles)

macro(dacoexec_supports_sanitizers)
  # Emscripten doesn't support sanitizers
  if(EMSCRIPTEN)
    set(SUPPORTS_UBSAN OFF)
    set(SUPPORTS_ASAN OFF)
  elseif((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)

    message(STATUS "Sanity checking UndefinedBehaviorSanitizer, it should be supported on this platform")
    set(TEST_PROGRAM "int main() { return 0; }")

    # Check if UndefinedBehaviorSanitizer works at link time
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("${TEST_PROGRAM}" HAS_UBSAN_LINK_SUPPORT)

    if(HAS_UBSAN_LINK_SUPPORT)
      message(STATUS "UndefinedBehaviorSanitizer is supported at both compile and link time.")
      set(SUPPORTS_UBSAN ON)
    else()
      message(WARNING "UndefinedBehaviorSanitizer is NOT supported at link time.")
      set(SUPPORTS_UBSAN OFF)
    endif()
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    if(NOT WIN32)
      message(STATUS "Sanity checking AddressSanitizer, it should be supported on this platform")
      set(TEST_PROGRAM "int main() { return 0; }")

      # Check if AddressSanitizer works at link time
      set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
      set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
      check_cxx_source_compiles("${TEST_PROGRAM}" HAS_ASAN_LINK_SUPPORT)

      if(HAS_ASAN_LINK_SUPPORT)
        message(STATUS "AddressSanitizer is supported at both compile and link time.")
        set(SUPPORTS_ASAN ON)
      else()
        message(WARNING "AddressSanitizer is NOT supported at link time.")
        set(SUPPORTS_ASAN OFF)
      endif()
    else()
      set(SUPPORTS_ASAN ON)
    endif()
  endif()
endmacro()

macro(dacoexec_setup_options)
  option(dacoexec_ENABLE_HARDENING "Enable hardening" ON)
  option(dacoexec_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    dacoexec_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    dacoexec_ENABLE_HARDENING
    OFF)

  dacoexec_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR dacoexec_PACKAGING_MAINTAINER_MODE)
    option(dacoexec_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(dacoexec_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(dacoexec_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(dacoexec_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(dacoexec_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(dacoexec_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(dacoexec_ENABLE_PCH "Enable precompiled headers" OFF)
    option(dacoexec_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(dacoexec_ENABLE_IPO "Enable IPO/LTO" ON)
    option(dacoexec_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(dacoexec_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(dacoexec_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(dacoexec_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(dacoexec_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(dacoexec_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(dacoexec_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
    option(dacoexec_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(dacoexec_ENABLE_PCH "Enable precompiled headers" OFF)
    option(dacoexec_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      dacoexec_ENABLE_IPO
      dacoexec_WARNINGS_AS_ERRORS
      dacoexec_ENABLE_SANITIZER_ADDRESS
      dacoexec_ENABLE_SANITIZER_LEAK
      dacoexec_ENABLE_SANITIZER_UNDEFINED
      dacoexec_ENABLE_SANITIZER_THREAD
      dacoexec_ENABLE_SANITIZER_MEMORY
      dacoexec_ENABLE_UNITY_BUILD
      dacoexec_ENABLE_CLANG_TIDY
      dacoexec_ENABLE_CPPCHECK
      dacoexec_ENABLE_COVERAGE
      dacoexec_ENABLE_PCH
      dacoexec_ENABLE_CACHE)
  endif()

  dacoexec_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED
     AND (dacoexec_ENABLE_SANITIZER_ADDRESS
          OR dacoexec_ENABLE_SANITIZER_THREAD
          OR dacoexec_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(dacoexec_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(dacoexec_global_options)
  if(dacoexec_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    dacoexec_enable_ipo()
  endif()

  dacoexec_supports_sanitizers()

  if(dacoexec_ENABLE_HARDENING AND dacoexec_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN
       OR dacoexec_ENABLE_SANITIZER_UNDEFINED
       OR dacoexec_ENABLE_SANITIZER_ADDRESS
       OR dacoexec_ENABLE_SANITIZER_THREAD
       OR dacoexec_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${dacoexec_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${dacoexec_ENABLE_SANITIZER_UNDEFINED}")
    dacoexec_enable_hardening(dacoexec_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(dacoexec_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(dacoexec_warnings INTERFACE)
  add_library(dacoexec_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  dacoexec_set_project_warnings(
    dacoexec_warnings
    ${dacoexec_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  include(cmake/Linker.cmake)
  # Must configure each target with linker options, we're avoiding setting it globally for now

  if(NOT EMSCRIPTEN)
    include(cmake/Sanitizers.cmake)
    dacoexec_enable_sanitizers(
      dacoexec_options
      ${dacoexec_ENABLE_SANITIZER_ADDRESS}
      ${dacoexec_ENABLE_SANITIZER_LEAK}
      ${dacoexec_ENABLE_SANITIZER_UNDEFINED}
      ${dacoexec_ENABLE_SANITIZER_THREAD}
      ${dacoexec_ENABLE_SANITIZER_MEMORY})
  endif()

  set_target_properties(dacoexec_options PROPERTIES UNITY_BUILD ${dacoexec_ENABLE_UNITY_BUILD})

  if(dacoexec_ENABLE_PCH)
    target_precompile_headers(
      dacoexec_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(dacoexec_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    dacoexec_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(dacoexec_ENABLE_CLANG_TIDY)
    dacoexec_enable_clang_tidy(dacoexec_options ${dacoexec_WARNINGS_AS_ERRORS})
  endif()

  if(dacoexec_ENABLE_CPPCHECK)
    dacoexec_enable_cppcheck(${dacoexec_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()

  if(dacoexec_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    dacoexec_enable_coverage(dacoexec_options)
  endif()

  if(dacoexec_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(dacoexec_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(dacoexec_ENABLE_HARDENING AND NOT dacoexec_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN
       OR dacoexec_ENABLE_SANITIZER_UNDEFINED
       OR dacoexec_ENABLE_SANITIZER_ADDRESS
       OR dacoexec_ENABLE_SANITIZER_THREAD
       OR dacoexec_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    dacoexec_enable_hardening(dacoexec_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
