if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

set(stage "${BUILD_DIR}/Testing/Temporary/subproject-consumer")
set(consumer_build "${stage}/build")
file(REMOVE_RECURSE "${stage}")

set(configure_command
    ${CMAKE_COMMAND}
    -S
    "${SOURCE_DIR}/test/fixtures/subproject_consumer"
    -B
    "${consumer_build}"
    "-DDACOEXEC_SOURCE_DIR=${SOURCE_DIR}"
    -DCMAKE_CXX_STANDARD=23
    -DCMAKE_CXX_STANDARD_REQUIRED=ON
    -DCMAKE_CXX_EXTENSIONS=OFF
    -DPARENT_SENTINEL=dacoexec-parent-sentinel)
if(GENERATOR)
  list(APPEND configure_command -G "${GENERATOR}")
endif()
if(GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
  list(APPEND configure_command -T "${GENERATOR_TOOLSET}")
endif()
if(GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
  list(APPEND configure_command -A "${GENERATOR_PLATFORM}")
endif()
if(BUILD_TYPE)
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(CXX_COMPILER AND NOT GENERATOR MATCHES "Visual Studio")
  list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(C_COMPILER AND NOT GENERATOR MATCHES "Visual Studio")
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
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Subproject configure failed (${configure_result})\n${configure_output}\n${configure_error}")
endif()

set(configure_log "${configure_output}${configure_error}")
string(TOLOWER "${configure_log}" configure_log_lower)
foreach(package IN ITEMS "catchorg/catch2" "fmtlib/fmt" "gabime/spdlog" "arthursonzogni/ftxui" "cliutils/cli11"
                         "nvidia/stdexec")
  string(FIND "${configure_log_lower}" "${package}" package_pos)
  if(NOT package_pos EQUAL -1)
    message(FATAL_ERROR "Subproject configuration fetched ${package}")
  endif()
endforeach()

if(EXISTS "${consumer_build}/_deps" OR EXISTS "${consumer_build}/dacoexec/_deps")
  message(FATAL_ERROR "Subproject configuration populated _deps")
endif()
file(GLOB cpm_bootstrap "${consumer_build}/cmake/CPM_*.cmake" "${consumer_build}/dacoexec/cmake/CPM_*.cmake")
if(cpm_bootstrap)
  message(FATAL_ERROR "Subproject configuration downloaded CPM: ${cpm_bootstrap}")
endif()

set(build_command ${CMAKE_COMMAND} --build "${consumer_build}")
if(CONFIG)
  list(APPEND build_command --config "${CONFIG}")
endif()
execute_process(
  COMMAND ${build_command}
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Subproject build failed (${build_result})\n${build_output}\n${build_error}")
endif()

if(CMAKE_CTEST_COMMAND)
  set(ctest "${CMAKE_CTEST_COMMAND}")
else()
  set(ctest ctest)
endif()
set(test_command ${ctest} --test-dir "${consumer_build}" --output-on-failure)
if(CONFIG)
  list(APPEND test_command -C "${CONFIG}")
endif()
execute_process(
  COMMAND ${test_command}
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_output
  ERROR_VARIABLE test_error)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "Subproject consumer failed (${test_result})\n${test_output}\n${test_error}")
endif()

message(STATUS "Subproject consumer built without optional dependencies")
