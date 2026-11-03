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
    if(lib MATCHES "Catch2|CLI11|stdexec|fmt::|spdlog|ftxui|dacoexec_options|dacoexec_warnings")
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

add_executable(dacoexec_tests tests.cpp ranges_tests.cpp bit_tests.cpp huffman_tests.cpp)
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
  add_test(NAME install.consumer COMMAND ${CMAKE_COMMAND} ${dacoexec_infrastructure_arguments} -P
                                         ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_install_consumer.cmake)
  set_tests_properties(
    install.consumer
    PROPERTIES TIMEOUT
               900
               RUN_SERIAL
               TRUE)
endif()
