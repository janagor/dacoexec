include(CTest)

# Workaround clang>=22 flagging __COUNTER__ as a C2y extension inside Catch2.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22)
  set(dacoexec_catch_compile_definitions CATCH_CONFIG_NO_COUNTER)
endif()

get_target_property(dacoexec_type dacoexec TYPE)
if(NOT
   dacoexec_type
   STREQUAL
   "INTERFACE_LIBRARY")
  message(FATAL_ERROR "dacoexec is ${dacoexec_type}, expected INTERFACE_LIBRARY")
endif()

get_target_property(dacoexec_link_libs dacoexec INTERFACE_LINK_LIBRARIES)
if(dacoexec_link_libs)
  foreach(lib IN LISTS dacoexec_link_libs)
    if(lib MATCHES "Catch2|CLI11|stdexec|benchmark|fuzzer|fmt::|spdlog|ftxui|dacoexec_options|dacoexec_warnings")
      message(FATAL_ERROR "Public target links ${lib}")
    endif()
  endforeach()
endif()

if(NOT COMMAND catch_discover_tests)
  if(DEFINED Catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  endif()
  if(DEFINED Catch2_DIR)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
  endif()
  include(Catch)
endif()

add_test(NAME cmake.sanitizers.apple_excludes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_apple_excludes_leak.cmake)
add_test(NAME cmake.sanitizers.non_apple_includes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_non_apple_includes_leak.cmake)

add_executable(
  dacoexec_tests
  tests.cpp
  error_contract_tests.cpp
  error_category_identity.cpp
  facade_tests.cpp
  archive_view_tests.cpp
  ans_tests.cpp
  arithmetic_range_tests.cpp
  arithmetic_static_tests.cpp
  model_tests.cpp
  tans_tests.cpp
  codec_base_tests.cpp
  codec_infrastructure_tests.cpp
  ranges_tests.cpp
  bit_tests.cpp
  integer_coding_tests.cpp
  integer_coding_sequence_tests.cpp
  integer_coding_view_tests.cpp
  huffman_tests.cpp
  huffman_codec_tests.cpp
  huffman_decode_view_tests.cpp
  streaming_tests.cpp
  cli_file_tests.cpp)
target_compile_definitions(dacoexec_tests PRIVATE DACOEXEC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(
  dacoexec_tests
  PRIVATE dacoexec::dacoexec
          dacoexec_options
          dacoexec_warnings
          Catch2::Catch2WithMain)
if(dacoexec_catch_compile_definitions)
  target_compile_definitions(dacoexec_tests PRIVATE ${dacoexec_catch_compile_definitions})
endif()
if(MSVC)
  target_compile_options(dacoexec_tests PRIVATE /Zc:__cplusplus)
endif()
dacoexec_configure_linker(dacoexec_tests)
catch_discover_tests(dacoexec_tests)

add_executable(dacoexec_constexpr_tests constexpr_tests.cpp)
target_link_libraries(dacoexec_constexpr_tests PRIVATE dacoexec::dacoexec dacoexec_options dacoexec_warnings)
if(MSVC)
  target_compile_options(dacoexec_constexpr_tests PRIVATE /Zc:__cplusplus)
endif()
dacoexec_configure_linker(dacoexec_constexpr_tests)
add_test(NAME constexpr.cxx20 COMMAND dacoexec_constexpr_tests)

# Infrastructure tests reconfigure the project. Give them the same compiler the
# current build selected, including extra standard-library flags.
set(dacoexec_infrastructure_arguments
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DBUILD_DIR=${PROJECT_BINARY_DIR}
    -DGENERATOR=${CMAKE_GENERATOR}
    -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
    -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
    -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DC_COMPILER=${CMAKE_C_COMPILER}
    "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DLINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    -DCONFIG=$<CONFIG>)

add_test(NAME configure.core_only COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_core_only_configure.cmake)
set_tests_properties(
  configure.core_only
  PROPERTIES TIMEOUT
             600
             RUN_SERIAL
             TRUE)

add_test(NAME configure.global_options COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                               ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_global_options.cmake)
set_tests_properties(
  configure.global_options
  PROPERTIES TIMEOUT
             600
             RUN_SERIAL
             TRUE)

if(dacoexec_TEST_SUBPROJECT_CONSUMER)
  add_test(NAME subproject.consumer COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_subproject_consumer.cmake)
  set_tests_properties(
    subproject.consumer
    PROPERTIES TIMEOUT
               900
               RUN_SERIAL
               TRUE)
endif()

if(dacoexec_TEST_INSTALL_CONSUMER AND dacoexec_INSTALL)
  add_test(NAME install.core_only COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_core_only_install.cmake)
  set_tests_properties(
    install.core_only
    PROPERTIES TIMEOUT
               900
               RUN_SERIAL
               TRUE)
  add_test(NAME install.consumer COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                         ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_install_consumer.cmake)
  set_tests_properties(
    install.consumer
    PROPERTIES TIMEOUT
               900
               RUN_SERIAL
               TRUE)
endif()

add_executable(dacoexec_streaming_memory_tests streaming_memory_tests.cpp)
target_link_libraries(dacoexec_streaming_memory_tests PRIVATE dacoexec::dacoexec dacoexec_options dacoexec_warnings)
if(MSVC)
  target_compile_options(dacoexec_streaming_memory_tests PRIVATE /Zc:__cplusplus)
endif()
dacoexec_configure_linker(dacoexec_streaming_memory_tests)
add_test(NAME streaming.memory COMMAND dacoexec_streaming_memory_tests)
option(dacoexec_TEST_LARGE_STREAMS "Run streaming tests exceeding 4 GiB" OFF)
if(dacoexec_TEST_LARGE_STREAMS)
  add_test(NAME streaming.large COMMAND dacoexec_streaming_memory_tests --large)
  set_tests_properties(
    streaming.large
    PROPERTIES LABELS
               large
               TIMEOUT
               7200)
endif()
if(dacoexec_BUILD_CLI)
  add_test(
    NAME cli.smoke
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:dacoexec_cli> -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke
            -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_cli_smoke.cmake)
endif()

# Each translation unit includes just one public header. Disable unity and PCH
# so neither can conceal missing includes, even in an enclosing build.
set(dacoexec_header_test_sources "")
foreach(header IN LISTS DACOEXEC_PUBLIC_HEADERS)
  file(
    RELATIVE_PATH
    relative_header
    "${PROJECT_SOURCE_DIR}/include"
    "${header}")
  string(MAKE_C_IDENTIFIER "${relative_header}" header_id)
  set(header_source "${CMAKE_CURRENT_BINARY_DIR}/public_headers/${header_id}.cpp")
  file(
    GENERATE
    OUTPUT "${header_source}"
    CONTENT "#include <${relative_header}> // IWYU pragma: keep\n")
  list(APPEND dacoexec_header_test_sources "${header_source}")
endforeach()
add_library(dacoexec_public_header_tests OBJECT ${dacoexec_header_test_sources})
target_link_libraries(dacoexec_public_header_tests PRIVATE dacoexec::dacoexec dacoexec_warnings)
set_target_properties(
  dacoexec_public_header_tests
  PROPERTIES CXX_STANDARD 20
             CXX_STANDARD_REQUIRED YES
             CXX_EXTENSIONS NO
             UNITY_BUILD OFF
             DISABLE_PRECOMPILE_HEADERS ON)
if(MSVC)
  target_compile_options(dacoexec_public_header_tests PRIVATE /Zc:__cplusplus)
endif()

if(dacoexec_BUILD_EXECUTION)
  if(dacoexec_TEST_SUBPROJECT_CONSUMER)
    add_test(NAME subproject.execution_consumer
             COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -DEXECUTION_SUBPROJECT=ON -P
                     ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_subproject_consumer.cmake)
    set_tests_properties(
      subproject.execution_consumer
      PROPERTIES TIMEOUT
                 900
                 RUN_SERIAL
                 TRUE)
  endif()

  add_executable(dacoexec_execution_tests execution_tests.cpp parallel_decompression_tests.cpp)
  dacoexec_suppress_stdexec_stack_escape(dacoexec_execution_tests)
  target_link_libraries(
    dacoexec_execution_tests
    PRIVATE dacoexec::execution
            dacoexec_options
            dacoexec_warnings
            Catch2::Catch2WithMain)
  if(dacoexec_catch_compile_definitions)
    target_compile_definitions(dacoexec_execution_tests PRIVATE ${dacoexec_catch_compile_definitions})
  endif()
  if(MSVC)
    target_compile_options(dacoexec_execution_tests PRIVATE /Zc:__cplusplus)
  endif()
  dacoexec_configure_linker(dacoexec_execution_tests)
  catch_discover_tests(
    dacoexec_execution_tests
    TEST_PREFIX
    execution.
    PROPERTIES
    LABELS
    execution
    TIMEOUT
    120)

  if(dacoexec_TEST_INSTALL_CONSUMER AND dacoexec_INSTALL)
    add_test(
      NAME install.execution_consumer
      COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -DEXECUTION_CONSUMER=ON
              "-DSTDEXEC_PACKAGE_DIR=${stdexec_DIR}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_install_consumer.cmake)
    set_tests_properties(
      install.execution_consumer
      PROPERTIES TIMEOUT
                 900
                 RUN_SERIAL
                 TRUE)
  endif()

  set(execution_header_sources "")
  foreach(header IN LISTS DACOEXEC_EXECUTION_HEADERS)
    file(
      RELATIVE_PATH
      relative_header
      "${PROJECT_SOURCE_DIR}/include"
      "${header}")
    string(MAKE_C_IDENTIFIER "${relative_header}" header_id)
    set(header_source "${CMAKE_CURRENT_BINARY_DIR}/public_headers/${header_id}.cpp")
    file(
      GENERATE
      OUTPUT "${header_source}"
      CONTENT "#include <${relative_header}> // IWYU pragma: keep\n")
    list(APPEND execution_header_sources "${header_source}")
  endforeach()
  add_library(dacoexec_execution_header_tests OBJECT ${execution_header_sources})
  target_link_libraries(dacoexec_execution_header_tests PRIVATE dacoexec::execution dacoexec_warnings)
  set_target_properties(
    dacoexec_execution_header_tests
    PROPERTIES CXX_STANDARD 20
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               UNITY_BUILD OFF
               DISABLE_PRECOMPILE_HEADERS ON)
  if(MSVC)
    target_compile_options(dacoexec_execution_header_tests PRIVATE /Zc:__cplusplus)
  endif()
endif()
