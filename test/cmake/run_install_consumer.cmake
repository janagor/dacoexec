if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

if(EXECUTION_CONSUMER)
  set(consumer_fixture execution_consumer)
else()
  set(consumer_fixture install_consumer)
endif()
set(stage "${BUILD_DIR}/Testing/Temporary/${consumer_fixture}")
set(prefix_a "${stage}/prefix-a")
set(prefix_b "${stage}/prefix-b")
set(consumer_build "${stage}/build")
file(REMOVE_RECURSE "${stage}")

set(install_command ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${prefix_a}")
if(CONFIG)
  list(
    APPEND
    install_command
    --config
    "${CONFIG}")
endif()
execute_process(
  COMMAND ${install_command}
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT
   install_result
   EQUAL
   0)
  message(FATAL_ERROR "cmake --install failed (${install_result})\n${install_output}\n${install_error}")
endif()

if(NOT EXISTS "${prefix_a}/include/dacoexec/dacoexec.hpp")
  message(FATAL_ERROR "Installed public header is missing from ${prefix_a}")
endif()
if(NOT EXISTS "${prefix_a}/include/dacoexec/huffman/frequency.hpp"
   OR NOT EXISTS "${prefix_a}/include/dacoexec/huffman/tree.hpp"
   OR NOT EXISTS "${prefix_a}/include/dacoexec/huffman/canonical.hpp"
   OR NOT EXISTS "${prefix_a}/include/dacoexec/huffman/codewords_view.hpp"
   OR NOT EXISTS "${prefix_a}/include/dacoexec/huffman/block.hpp"
   OR NOT EXISTS "${prefix_a}/include/dacoexec/huffman/decode_view.hpp")
  message(FATAL_ERROR "Installed Huffman headers are missing from ${prefix_a}")
endif()
if(EXISTS "${prefix_a}/include/dacoexec/sample_library.hpp" OR EXISTS
                                                               "${prefix_a}/include/dacoexec/sample_library_export.hpp")
  message(FATAL_ERROR "The sample library header was installed")
endif()
if(EXISTS "${prefix_a}/bin/intro" OR EXISTS "${prefix_a}/bin/intro.exe")
  message(FATAL_ERROR "The sample executable was installed")
endif()

file(
  GLOB_RECURSE package_files
  LIST_DIRECTORIES false
  "${prefix_a}/*.cmake")
set(found_config FALSE)
set(found_version FALSE)
set(found_targets FALSE)
set(targets_text "")
foreach(package_file IN LISTS package_files)
  cmake_path(
    GET
    package_file
    FILENAME
    package_name)
  if(package_name STREQUAL "dacoexecConfig.cmake")
    set(found_config TRUE)
  elseif(package_name STREQUAL "dacoexecConfigVersion.cmake")
    set(found_version TRUE)
  elseif(package_name MATCHES "^dacoexecTargets.*\\.cmake$")
    set(found_targets TRUE)
    file(READ "${package_file}" one_targets_file)
    string(APPEND targets_text "${one_targets_file}")
  endif()
endforeach()
if(NOT found_config
   OR NOT found_version
   OR NOT found_targets)
  message(FATAL_ERROR "The installed CMake package is incomplete under ${prefix_a}")
endif()

string(FIND "${targets_text}" "${SOURCE_DIR}" source_in_targets)
if(NOT
   source_in_targets
   EQUAL
   -1)
  message(FATAL_ERROR "dacoexecTargets.cmake references the source tree")
endif()
foreach(internal IN ITEMS dacoexec_options dacoexec_warnings sample_library)
  string(FIND "${targets_text}" "${internal}" internal_pos)
  if(NOT
     internal_pos
     EQUAL
     -1)
    message(FATAL_ERROR "dacoexecTargets.cmake exports internal target '${internal}'")
  endif()
endforeach()

# Consume a copy of the prefix so the package must be relocatable.
file(COPY "${prefix_a}/" DESTINATION "${prefix_b}")

set(ENV{CMAKE_PREFIX_PATH} "")
set(configure_command
    ${CMAKE_COMMAND} -S "${SOURCE_DIR}/test/fixtures/${consumer_fixture}" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${prefix_b}" -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON
    -DCMAKE_CXX_EXTENSIONS=OFF "-DDACOEXEC_SOURCE_DIR=${SOURCE_DIR}" "-DDACOEXEC_INSTALL_PREFIX=${prefix_b}")
if(EXECUTION_CONSUMER
   AND STDEXEC_PACKAGE_DIR
   AND NOT EXISTS "${prefix_b}/lib/cmake/stdexec"
   AND NOT EXISTS "${prefix_b}/lib64/cmake/stdexec")
  list(APPEND configure_command "-Dstdexec_DIR=${STDEXEC_PACKAGE_DIR}")
endif()
if(NOT EXECUTION_CONSUMER)
  list(APPEND configure_command -DCMAKE_DISABLE_FIND_PACKAGE_stdexec=ON)
endif()
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
  message(FATAL_ERROR "Consumer configure failed (${configure_result})\n${configure_output}\n${configure_error}")
endif()

set(build_command ${CMAKE_COMMAND} --build "${consumer_build}")
if(CONFIG)
  list(
    APPEND
    build_command
    --config
    "${CONFIG}")
endif()
execute_process(
  COMMAND ${build_command}
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT
   build_result
   EQUAL
   0)
  message(FATAL_ERROR "Consumer build failed (${build_result})\n${build_output}\n${build_error}")
endif()

if(CMAKE_CTEST_COMMAND)
  set(ctest "${CMAKE_CTEST_COMMAND}")
else()
  set(ctest ctest)
endif()
set(test_command ${ctest} --test-dir "${consumer_build}" --output-on-failure)
if(CONFIG)
  list(
    APPEND
    test_command
    -C
    "${CONFIG}")
endif()
execute_process(
  COMMAND ${test_command}
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_output
  ERROR_VARIABLE test_error)
if(NOT
   test_result
   EQUAL
   0)
  message(FATAL_ERROR "Consumer tests failed (${test_result})\n${test_output}\n${test_error}")
endif()

message(STATUS "Installed package consumed from ${prefix_b}")
