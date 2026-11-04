if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

set(options_build "${BUILD_DIR}/Testing/Temporary/global-options")
file(REMOVE_RECURSE "${options_build}")

set(configure_command
    ${CMAKE_COMMAND} -S "${SOURCE_DIR}" -B "${options_build}" -Ddacoexec_BUILD_TESTS=OFF -Ddacoexec_BUILD_CLI=OFF
    -Ddacoexec_BUILD_FUZZ_TESTS=OFF -Ddacoexec_ENABLE_FORMATTING=OFF -Ddacoexec_INSTALL=OFF
    -Ddacoexec_ENABLE_HARDENING=ON -Ddacoexec_ENABLE_GLOBAL_HARDENING=ON -DCMAKE_CXX_STANDARD=20
    -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF)
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
set(ipo_ready FALSE)
if(CXX_COMPILER
   AND NOT
       GENERATOR
       MATCHES
       "Visual Studio")
  list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(GENERATOR MATCHES "Visual Studio")
  set(ipo_ready TRUE)
elseif(CXX_COMPILER)
  foreach(tool_pair IN ITEMS "gcc-ar,gcc-ranlib" "llvm-ar,llvm-ranlib")
    string(
      REPLACE ","
              ";"
              tool_pair
              "${tool_pair}")
    list(
      GET
      tool_pair
      0
      ar_name)
    list(
      GET
      tool_pair
      1
      ranlib_name)
    execute_process(
      COMMAND "${CXX_COMPILER}" -print-prog-name=${ar_name}
      OUTPUT_VARIABLE printed_ar
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(printed_ar STREQUAL ar_name OR NOT EXISTS "${printed_ar}")
      find_program(resolved_ar NAMES ${ar_name})
    else()
      set(resolved_ar "${printed_ar}")
    endif()
    if(EXISTS "${resolved_ar}")
      set(ipo_ready TRUE)
      list(APPEND configure_command "-DCMAKE_CXX_COMPILER_AR=${resolved_ar}")
      execute_process(
        COMMAND "${CXX_COMPILER}" -print-prog-name=${ranlib_name}
        OUTPUT_VARIABLE printed_ranlib
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(printed_ranlib STREQUAL ranlib_name OR NOT EXISTS "${printed_ranlib}")
        find_program(resolved_ranlib NAMES ${ranlib_name})
      else()
        set(resolved_ranlib "${printed_ranlib}")
      endif()
      if(EXISTS "${resolved_ranlib}")
        list(APPEND configure_command "-DCMAKE_CXX_COMPILER_RANLIB=${resolved_ranlib}")
      endif()
      break()
    endif()
    unset(resolved_ar CACHE)
    unset(resolved_ranlib CACHE)
  endforeach()
endif()
if(ipo_ready)
  list(APPEND configure_command -Ddacoexec_ENABLE_IPO=ON)
else()
  message(STATUS "This compiler has no gcc-ar or llvm-ar, so IPO stays at its default")
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
set(configure_log "${configure_output}${configure_error}")
if(NOT
   configure_result
   EQUAL
   0)
  message(FATAL_ERROR "IPO and global hardening configure failed (${configure_result})\n${configure_log}")
endif()

if(ipo_ready)
  if(NOT
     configure_log
     MATCHES
     "dacoexec: interprocedural optimization enabled")
    message(FATAL_ERROR "dacoexec_ENABLE_IPO=ON did not enable interprocedural optimization")
  endif()
elseif(configure_log MATCHES "dacoexec: interprocedural optimization enabled")
  message(FATAL_ERROR "IPO was enabled without dacoexec_ENABLE_IPO=ON")
endif()
if(NOT
   configure_log
   MATCHES
   "Setting hardening options globally")
  message(FATAL_ERROR "dacoexec_ENABLE_GLOBAL_HARDENING=ON did not enable global hardening")
endif()
if(NOT
   configure_log
   MATCHES
   "dacoexec: global hardening enabled")
  message(FATAL_ERROR "Global hardening finished without its completion message")
endif()

if(EXISTS "${options_build}/_deps")
  message(FATAL_ERROR "Developer-option configuration fetched dependencies")
endif()

if(ipo_ready)
  message(STATUS "IPO and global hardening are active for a top-level build")
else()
  message(STATUS "Global hardening is active for a top-level build")
endif()
