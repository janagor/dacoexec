add_executable(dacoexec_benchmarks huffman_bench.cpp)
dacoexec_configure_linker(dacoexec_benchmarks)
dacoexec_suppress_stdexec_stack_escape(dacoexec_benchmarks)
target_link_libraries(
  dacoexec_benchmarks
  PRIVATE dacoexec::dacoexec
          dacoexec::execution
          benchmark::benchmark
          dacoexec_warnings)
option(dacoexec_BENCHMARK_FULL_MATRIX "Register every worker/in-flight combination" OFF)
if(dacoexec_BENCHMARK_FULL_MATRIX)
  target_compile_definitions(dacoexec_benchmarks PRIVATE DACOEXEC_BENCHMARK_FULL_MATRIX)
endif()

add_executable(dacoexec_stream_memory stream_memory.cpp)
dacoexec_configure_linker(dacoexec_stream_memory)
dacoexec_suppress_stdexec_stack_escape(dacoexec_stream_memory)
target_link_libraries(dacoexec_stream_memory PRIVATE dacoexec::execution dacoexec_warnings)

add_executable(dacoexec_ans_stream_memory ans_stream_memory.cpp)
dacoexec_configure_linker(dacoexec_ans_stream_memory)
dacoexec_suppress_stdexec_stack_escape(dacoexec_ans_stream_memory)
target_link_libraries(dacoexec_ans_stream_memory PRIVATE dacoexec::execution dacoexec_warnings)

add_executable(dacoexec_ans_allocations ans_allocations.cpp)
dacoexec_configure_linker(dacoexec_ans_allocations)
target_link_libraries(dacoexec_ans_allocations PRIVATE dacoexec::dacoexec dacoexec_warnings)

add_executable(dacoexec_ans_benchmarks ans_bench.cpp)
dacoexec_configure_linker(dacoexec_ans_benchmarks)
target_link_libraries(dacoexec_ans_benchmarks PRIVATE dacoexec::dacoexec benchmark::benchmark dacoexec_warnings)

add_executable(dacoexec_arithmetic_model_benchmarks arithmetic_model_bench.cpp)
dacoexec_configure_linker(dacoexec_arithmetic_model_benchmarks)
target_link_libraries(dacoexec_arithmetic_model_benchmarks PRIVATE dacoexec::dacoexec benchmark::benchmark
                                                                   dacoexec_warnings)

add_executable(dacoexec_arithmetic_adaptive_model_benchmarks arithmetic_adaptive_model_bench.cpp)
dacoexec_configure_linker(dacoexec_arithmetic_adaptive_model_benchmarks)
target_link_libraries(dacoexec_arithmetic_adaptive_model_benchmarks PRIVATE dacoexec::dacoexec benchmark::benchmark
                                                                            dacoexec_warnings)

add_executable(dacoexec_arithmetic_benchmarks arithmetic_bench.cpp)
dacoexec_configure_linker(dacoexec_arithmetic_benchmarks)
target_link_libraries(dacoexec_arithmetic_benchmarks PRIVATE dacoexec::dacoexec benchmark::benchmark dacoexec_warnings)
