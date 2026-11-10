if(NOT DEFINED CLI OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "CLI and WORK_DIR are required")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
set(input "${WORK_DIR}/input.bin")
set(archive "${WORK_DIR}/output.dhf")
set(restored "${WORK_DIR}/restored.bin")
file(COPY_FILE "${FIXTURE_DIR}/cli-input.bin" "${input}")
file(REMOVE "${archive}" "${restored}")
function(run_success)
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE result
    ERROR_VARIABLE diagnostic)
  if(NOT
     result
     EQUAL
     0)
    message(FATAL_ERROR "CLI failed: ${diagnostic}")
  endif()
endfunction()
function(run_failure)
  execute_process(COMMAND "${CLI}" ${ARGN} RESULT_VARIABLE result)
  if(result EQUAL 0)
    message(FATAL_ERROR "CLI unexpectedly succeeded: ${ARGN}")
  endif()
endfunction()
function(run_failure_with_code expected_code)
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE result
    ERROR_VARIABLE diagnostic)
  if(result EQUAL 0
     OR NOT
        diagnostic
        MATCHES
        "\\[dacoexec:${expected_code}\\]")
    message(FATAL_ERROR "Expected CLI error code ${expected_code}: ${ARGN}\n${diagnostic}")
  endif()
endfunction()
run_success(
  compress
  --algorithm
  huffman
  "${input}"
  "${archive}")
run_success(decompress "${archive}" "${restored}")
file(SHA256 "${input}" expected)
file(SHA256 "${restored}" actual)
if(NOT
   expected
   STREQUAL
   actual)
  message(FATAL_ERROR "CLI round trip differs")
endif()
# errc::invalid_argument = 1: path-policy rejection happens before opening files.
run_failure_with_code(
  1
  compress
  --algorithm
  huffman
  "${input}"
  "${archive}")
run_failure_with_code(
  1
  compress
  --algorithm
  huffman
  --force
  "${input}"
  "${input}")
run_failure(
  decompress
  --force
  --max-output-size
  0
  "${archive}"
  "${restored}")
file(SHA256 "${restored}" preserved)
if(NOT
   expected
   STREQUAL
   preserved)
  message(FATAL_ERROR "Failed operation replaced existing output")
endif()
file(WRITE "${archive}" "corrupt")
run_failure(
  decompress
  --force
  "${archive}"
  "${restored}")
file(SHA256 "${restored}" preserved)
if(NOT
   expected
   STREQUAL
   preserved)
  message(FATAL_ERROR "Corrupt input replaced existing output")
endif()
run_success(
  compress
  --algorithm
  huffman
  --force
  "${input}"
  "${archive}")
run_success(
  decompress
  --force
  "${archive}"
  "${restored}")
run_failure(
  compress
  --algorithm
  unsupported
  "${input}"
  "${archive}")
file(GLOB leftovers "${WORK_DIR}/*.tmp.*")
if(leftovers)
  message(FATAL_ERROR "Temporary outputs leaked: ${leftovers}")
endif()

set(ans_archive "${WORK_DIR}/output.dan")
set(ans_restored "${WORK_DIR}/ans-restored.bin")
run_success(
  compress
  --algorithm
  rans
  --force
  "${input}"
  "${ans_archive}")
run_success(
  decompress
  --force
  "${ans_archive}"
  "${ans_restored}")
file(SHA256 "${input}" ans_expected)
file(SHA256 "${ans_restored}" ans_actual)
if(NOT
   ans_expected
   STREQUAL
   ans_actual)
  message(FATAL_ERROR "ANS CLI round trip differs")
endif()
run_failure(
  decompress
  --force
  --max-output-size
  0
  "${ans_archive}"
  "${ans_restored}")
file(SHA256 "${ans_restored}" ans_preserved)
if(NOT
   ans_expected
   STREQUAL
   ans_preserved)
  message(FATAL_ERROR "Failed ANS operation replaced existing output")
endif()

foreach(table_log 8 10 12)
  run_success(
    compress
    --algorithm
    tans
    --table-log
    ${table_log}
    --force
    "${input}"
    "${ans_archive}")
  run_success(
    decompress
    --force
    "${ans_archive}"
    "${ans_restored}")
  file(SHA256 "${ans_restored}" tans_actual)
  if(NOT
     ans_expected
     STREQUAL
     tans_actual)
    message(FATAL_ERROR "tANS CLI round trip differs at table log ${table_log}")
  endif()
endforeach()
run_failure(
  compress
  --algorithm
  tans
  --table-log
  7
  --force
  "${input}"
  "${ans_archive}")
run_failure(
  compress
  --algorithm
  tans
  --table-log
  13
  --force
  "${input}"
  "${ans_archive}")

foreach(other_algorithm rans huffman arithmetic)
  run_failure(
    compress
    --algorithm
    ${other_algorithm}
    --table-log
    10
    --force
    "${input}"
    "${ans_archive}")
endforeach()

set(arithmetic_archive "${WORK_DIR}/output.dac")
set(arithmetic_restored "${WORK_DIR}/arithmetic-restored.bin")
run_success(
  compress
  --algorithm
  arithmetic
  --block-size
  4096
  --force
  "${input}"
  "${arithmetic_archive}")
run_success(
  decompress
  --force
  "${arithmetic_archive}"
  "${arithmetic_restored}")
file(SHA256 "${arithmetic_restored}" arithmetic_actual)
if(NOT
   expected
   STREQUAL
   arithmetic_actual)
  message(FATAL_ERROR "Arithmetic CLI round trip differs")
endif()
run_failure(
  decompress
  --force
  --max-output-size
  0
  "${arithmetic_archive}"
  "${arithmetic_restored}")
file(SHA256 "${arithmetic_restored}" arithmetic_preserved)
if(NOT
   expected
   STREQUAL
   arithmetic_preserved)
  message(FATAL_ERROR "Failed arithmetic operation replaced existing output")
endif()
file(
  READ
  "${arithmetic_archive}"
  arithmetic_bytes
  HEX)
if(NOT
   arithmetic_bytes
   MATCHES
   "^44414331")
  message(FATAL_ERROR "Arithmetic CLI did not write DAC1")
endif()
