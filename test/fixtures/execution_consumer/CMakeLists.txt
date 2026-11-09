cmake_minimum_required(VERSION 3.29)
project(dacoexec_execution_consumer LANGUAGES CXX)
find_package(dacoexec CONFIG REQUIRED COMPONENTS execution)
add_executable(execution_consumer main.cpp)
target_link_libraries(execution_consumer PRIVATE dacoexec::execution)
if(MSVC)
  target_compile_options(execution_consumer PRIVATE /Zc:__cplusplus)
endif()
enable_testing()
add_test(NAME execution_consumer.runs COMMAND execution_consumer)

# Imported targets supply only installed include directories. No PCH or unity
# compilation can hide missing dependencies in these single-header units.
set(standalone_header_sources "")
file(
  GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/execution_hpp.cpp"
  CONTENT "#include <dacoexec/execution.hpp>\n")
list(APPEND standalone_header_sources "${CMAKE_CURRENT_BINARY_DIR}/execution_hpp.cpp")
add_library(installed_public_headers OBJECT ${standalone_header_sources})
target_link_libraries(installed_public_headers PRIVATE dacoexec::execution)
set_target_properties(installed_public_headers PROPERTIES UNITY_BUILD OFF DISABLE_PRECOMPILE_HEADERS ON)
