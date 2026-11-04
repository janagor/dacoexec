cmake_minimum_required(VERSION 3.29)
project(dacoexec_execution_subproject_consumer LANGUAGES CXX)
set(dacoexec_BUILD_EXECUTION
    ON
    CACHE BOOL "" FORCE)
set(dacoexec_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE)
set(dacoexec_INSTALL
    OFF
    CACHE BOOL "" FORCE)
add_subdirectory("${DACOEXEC_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/dacoexec")
if(NOT TARGET dacoexec::execution)
  message(FATAL_ERROR "Execution target is missing")
endif()
if(dacoexec_INSTALL OR STDEXEC_INSTALL)
  message(FATAL_ERROR "Embedded execution enabled installation")
endif()
add_executable(execution_consumer ../execution_consumer/main.cpp)
target_link_libraries(execution_consumer PRIVATE dacoexec::execution)
if(MSVC)
  target_compile_options(execution_consumer PRIVATE /Zc:__cplusplus)
endif()
enable_testing()
add_test(NAME execution_subproject.runs COMMAND execution_consumer)
