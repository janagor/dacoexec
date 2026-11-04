cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(dacoexec_install_consumer LANGUAGES CXX)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

find_package(dacoexec CONFIG REQUIRED)

# Imported interface targets wrap their usage requirements in $<BUILD_INTERFACE>,
# which here means "when a consumer compiles", not "this came from the build tree".
function(dacoexec_usage_include_paths raw_includes out_var)
  set(expanded "")
  foreach(entry IN LISTS raw_includes)
    if(entry MATCHES "^\\$<(BUILD|INSTALL)_INTERFACE:(.+)>$")
      list(APPEND expanded "${CMAKE_MATCH_2}")
    else()
      list(APPEND expanded "${entry}")
    endif()
  endforeach()
  set(${out_var} "${expanded}" PARENT_SCOPE)
endfunction()

get_target_property(dacoexec_includes dacoexec::dacoexec INTERFACE_INCLUDE_DIRECTORIES)
if(NOT dacoexec_includes)
  message(FATAL_ERROR "Installed dacoexec does not publish an include directory")
endif()
dacoexec_usage_include_paths("${dacoexec_includes}" dacoexec_usage_includes)

if(DACOEXEC_SOURCE_DIR)
  cmake_path(ABSOLUTE_PATH DACOEXEC_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE dacoexec_source_root)
  set(dacoexec_source_include "${dacoexec_source_root}/include")
  foreach(include_dir IN LISTS dacoexec_usage_includes)
    cmake_path(ABSOLUTE_PATH include_dir NORMALIZE OUTPUT_VARIABLE include_norm)
    cmake_path(IS_PREFIX dacoexec_source_include "${include_norm}" NORMALIZE source_include_is_prefix)
    if(source_include_is_prefix OR include_norm STREQUAL dacoexec_source_include)
      message(FATAL_ERROR "Installed dacoexec still exposes a source-tree include: ${include_dir}")
    endif()
  endforeach()
endif()

if(DACOEXEC_INSTALL_PREFIX)
  cmake_path(ABSOLUTE_PATH DACOEXEC_INSTALL_PREFIX NORMALIZE OUTPUT_VARIABLE dacoexec_install_root)
  set(prefix_found FALSE)
  foreach(include_dir IN LISTS dacoexec_usage_includes)
    cmake_path(ABSOLUTE_PATH include_dir NORMALIZE OUTPUT_VARIABLE include_norm)
    cmake_path(IS_PREFIX dacoexec_install_root "${include_norm}" NORMALIZE install_prefix_is_prefix)
    if(install_prefix_is_prefix)
      set(prefix_found TRUE)
    endif()
  endforeach()
  if(NOT prefix_found)
    message(FATAL_ERROR
            "Installed include directories are not under ${DACOEXEC_INSTALL_PREFIX}: ${dacoexec_usage_includes}")
  endif()
endif()

get_target_property(dacoexec_type dacoexec::dacoexec TYPE)
if(NOT dacoexec_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR "dacoexec::dacoexec is ${dacoexec_type}, expected INTERFACE_LIBRARY")
endif()

add_executable(consumer main.cpp canonical_header.cpp codewords_header.cpp block_header.cpp decode_header.cpp stream_header.cpp)
target_link_libraries(consumer PRIVATE dacoexec::dacoexec)

enable_testing()
add_test(NAME consumer.runs COMMAND consumer)
