# Instrument only these private executables (the codec is header-only).
if(NOT
   CMAKE_CXX_COMPILER_ID
   STREQUAL
   "Clang"
   OR WIN32
   OR EMSCRIPTEN)
  message(FATAL_ERROR "Codec fuzzing requires Unix Clang with libFuzzer, ASan and UBSan runtimes")
endif()
if(dacoexec_ENABLE_SANITIZER_THREAD
   OR dacoexec_ENABLE_SANITIZER_MEMORY
   OR dacoexec_ENABLE_GLOBAL_HARDENING
   OR dacoexec_ENABLE_COVERAGE
   OR dacoexec_ENABLE_IPO)
  message(
    FATAL_ERROR "Fuzzing is incompatible with TSan, MSan, global hardening, coverage and IPO; use unixlike-clang-fuzz")
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer,address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer,address,undefined")
check_cxx_source_compiles(
  "#include <cstddef>
   extern \"C\" auto LLVMFuzzerTestOneInput(unsigned char const*, std::size_t) -> int { return 0; }"
  dacoexec_HAS_LIBFUZZER)
cmake_pop_check_state()
if(NOT dacoexec_HAS_LIBFUZZER)
  message(FATAL_ERROR "Clang cannot link libFuzzer/ASan/UBSan; install compiler-rt for this toolchain")
endif()

set(dacoexec_FUZZ_SECONDS
    30
    CACHE STRING "Per-target CTest fuzz budget in seconds")
foreach(
  harness IN
  ITEMS huffman_decoder
        frame_parser
        round_trip
        arithmetic_payload_round_trip
        cross_codec_round_trip
        ans_decoder
        arithmetic_decoder
        binary_decoder)
  set(target dacoexec_fuzz_${harness})
  add_executable(${target} ${harness}.cpp)
  dacoexec_configure_linker(${target})
  target_link_libraries(${target} PRIVATE dacoexec::dacoexec dacoexec_warnings)
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
                                           -fno-sanitize-recover=all)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/corpus/${harness}"
       "${CMAKE_CURRENT_BINARY_DIR}/artifacts/${harness}")
  set(fuzz_max_len 65536)
  if(harness STREQUAL "cross_codec_round_trip")
    set(fuzz_max_len 4096)
  elseif(harness STREQUAL "arithmetic_payload_round_trip")
    set(fuzz_max_len 4097)
  endif()
  add_test(
    NAME fuzz.${harness}
    COMMAND
      ${target} "${CMAKE_CURRENT_BINARY_DIR}/corpus/${harness}" "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${harness}"
      -max_total_time=${dacoexec_FUZZ_SECONDS} -max_len=${fuzz_max_len} -timeout=5 -rss_limit_mb=512
      "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/artifacts/${harness}/" -print_final_stats=1)
  math(EXPR fuzz_timeout "${dacoexec_FUZZ_SECONDS} + 30")
  set_tests_properties(
    fuzz.${harness}
    PROPERTIES LABELS
               fuzz
               TIMEOUT
               ${fuzz_timeout})
endforeach()

add_executable(dacoexec_generate_corpus EXCLUDE_FROM_ALL generate_corpus.cpp)
dacoexec_configure_linker(dacoexec_generate_corpus)
target_link_libraries(dacoexec_generate_corpus PRIVATE dacoexec::dacoexec dacoexec_warnings)

# Replay the complete committed DAC1 corpus before the coverage-guided campaign.
# Keep a writable first corpus so libFuzzer never writes to source fixtures.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/corpus/arithmetic_replay")
add_test(
  NAME fuzz.arithmetic_corpus
  COMMAND dacoexec_fuzz_arithmetic_decoder "${CMAKE_CURRENT_BINARY_DIR}/corpus/arithmetic_replay"
          "${CMAKE_CURRENT_SOURCE_DIR}/corpus/arithmetic_decoder" -runs=0 -max_len=65536 -timeout=5 -rss_limit_mb=512)
set_tests_properties(
  fuzz.arithmetic_corpus
  PROPERTIES LABELS
             fuzz
             TIMEOUT
             30)
set_tests_properties(fuzz.arithmetic_decoder PROPERTIES DEPENDS fuzz.arithmetic_corpus)
