cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(dacoexec_subproject_consumer LANGUAGES CXX)

if(NOT DACOEXEC_SOURCE_DIR)
  message(FATAL_ERROR "DACOEXEC_SOURCE_DIR must point at the dacoexec source tree")
endif()

if(NOT DEFINED PARENT_SENTINEL)
  set(PARENT_SENTINEL "dacoexec-parent-sentinel")
endif()

set(saved_cxx_flags "${CMAKE_CXX_FLAGS}")
set(saved_build_type "${CMAKE_BUILD_TYPE}")
set(saved_sentinel "${PARENT_SENTINEL}")

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

add_subdirectory("${DACOEXEC_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/dacoexec")

if(NOT CMAKE_CXX_STANDARD EQUAL 23)
  message(FATAL_ERROR "Embedded dacoexec changed CMAKE_CXX_STANDARD to '${CMAKE_CXX_STANDARD}'")
endif()
if(NOT CMAKE_CXX_EXTENSIONS STREQUAL "OFF")
  message(FATAL_ERROR "Embedded dacoexec changed CMAKE_CXX_EXTENSIONS to '${CMAKE_CXX_EXTENSIONS}'")
endif()
if(NOT CMAKE_CXX_FLAGS STREQUAL saved_cxx_flags)
  message(FATAL_ERROR "Embedded dacoexec changed CMAKE_CXX_FLAGS")
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL saved_build_type)
  message(FATAL_ERROR "Embedded dacoexec changed CMAKE_BUILD_TYPE")
endif()
if(NOT PARENT_SENTINEL STREQUAL saved_sentinel)
  message(FATAL_ERROR "Embedded dacoexec changed PARENT_SENTINEL")
endif()

if(NOT TARGET dacoexec::dacoexec)
  message(FATAL_ERROR "dacoexec::dacoexec was not created")
endif()

get_target_property(dacoexec_type dacoexec TYPE)
if(NOT dacoexec_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR "dacoexec is ${dacoexec_type}, expected INTERFACE_LIBRARY")
endif()

get_target_property(dacoexec_link_libs dacoexec INTERFACE_LINK_LIBRARIES)
if(dacoexec_link_libs)
  message(FATAL_ERROR "The public target links dependencies: ${dacoexec_link_libs}")
endif()

foreach(unexpected IN ITEMS dacoexec_tests dacoexec_constexpr_tests intro Catch2::Catch2WithMain CLI11::CLI11 fmt::fmt
                             spdlog::spdlog ftxui::screen STDEXEC::stdexec)
  if(TARGET ${unexpected})
    message(FATAL_ERROR "Subproject unexpectedly created ${unexpected}")
  endif()
endforeach()

add_executable(subproject_consumer main.cpp)
target_link_libraries(subproject_consumer PRIVATE dacoexec::dacoexec)

enable_testing()
add_test(NAME subproject.runs COMMAND subproject_consumer)
