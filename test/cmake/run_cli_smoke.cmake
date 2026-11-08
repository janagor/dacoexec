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
run_failure(
  compress
  --algorithm
  huffman
  "${input}"
  "${archive}")
run_failure(
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
