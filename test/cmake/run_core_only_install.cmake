if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

set(core_build "${BUILD_DIR}/Testing/Temporary/core-only-install")
file(REMOVE_RECURSE "${core_build}")
set(configure_command
    ${CMAKE_COMMAND} -S "${SOURCE_DIR}" -B "${core_build}" -Ddacoexec_BUILD_EXECUTION=OFF -Ddacoexec_BUILD_TESTS=OFF
    -Ddacoexec_BUILD_CLI=OFF -Ddacoexec_INSTALL=ON -Ddacoexec_ENABLE_HARDENING=OFF -Ddacoexec_WARNINGS_AS_ERRORS=OFF
    -Ddacoexec_ENABLE_CLANG_TIDY=OFF -Ddacoexec_ENABLE_CPPCHECK=OFF -DCMAKE_DISABLE_FIND_PACKAGE_stdexec=ON
    -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF)
if(GENERATOR)
  list(
    APPEND
    configure_command
    -G
    "${GENERATOR}")
endif()
if(GENERATOR_TOOLSET)
  list(
    APPEND
    configure_command
    -T
    "${GENERATOR_TOOLSET}")
endif()
if(GENERATOR_PLATFORM)
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
if(CXX_FLAGS)
  list(APPEND configure_command "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}")
endif()
if(LINKER_FLAGS)
  list(APPEND configure_command "-DCMAKE_EXE_LINKER_FLAGS=${LINKER_FLAGS}")
endif()
execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT
   result
   EQUAL
   0)
  message(FATAL_ERROR "Core-only install configuration failed\n${output}\n${error}")
endif()
if(EXISTS "${core_build}/_deps")
  message(FATAL_ERROR "Core-only installation fetched dependencies")
endif()
set(BUILD_DIR "${core_build}")
set(EXECUTION_CONSUMER OFF)
include("${SOURCE_DIR}/test/cmake/run_install_consumer.cmake")
