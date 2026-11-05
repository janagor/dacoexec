# Ordered execution lifetime review

This is a source review against NVIDIA/stdexec revision
`ead186b1d8db3ebe37a946ff84a6ce08bf795153`, not a sanitizer result.
The implementation intentionally allows terminal completion to destroy the
currently completing child operation and its parent operation.

## Pinned completion paths

The upstream files below are relative to that revision's source tree:

- `include/exec/static_thread_pool.hpp`: `_static_thread_pool::run()` removes
  the task from its queue and calls `task->execute_()`. It does not touch that
  task after the call. `_opstate::do_execute_()` retrieves the receiver's stop
  token before dispatching value or stopped completion. Each branch ends with
  the terminal receiver call; it does not access `op` afterward.
- `include/stdexec/__detail/__basic_sender.hpp`: `__rcvr::set_value()`,
  `set_error()`, and `set_stopped()` delegate to the sender's completion
  implementation and perform no subsequent state access.
- `include/stdexec/__detail/__then.hpp`: `__then_impl::__complete_fn` delegates
  value completion to `__set_value_from()` and forwards error/stopped directly.
  Neither path reads the operation state after the receiver completes.
- `include/stdexec/__detail/__receivers.hpp`: `__set_value_from()` finishes
  invoking the processing function before calling downstream `set_value()`.
  Processing exceptions become downstream `set_error()`. No sender-state
  access follows either terminal call. A result temporary can be destroyed
  afterward, so result destructors must not depend on the destroyed operation.
- `include/stdexec/__detail/__inline_scheduler.hpp`: the inline operation's
  `start()` ends with `set_value()` and does not access its state afterward.

Upstream source: [pinned stdexec tree](https://github.com/NVIDIA/stdexec/tree/ead186b1d8db3ebe37a946ff84a6ce08bf795153).

## Local invariants

Every worker completion finishes writing its slot before decrementing
`remaining_`. The dispatch sentinel keeps the count nonzero until every
started child has been stored. The final acquire/release decrement synchronizes
slot writes before inspection. Each receiver tail-calls `settled()` and never
uses its receiver or parent afterward.

`driving_` permits only one coordinator to advance at a time. Inline child
completion queues another iteration rather than entering the next batch
recursively. The terminal path returns immediately after delivering completion;
the caller may destroy the entire parent from within that completion.

Custom schedulers must likewise permit their operation state to be destroyed
inside a terminal receiver invocation. The executor forwards the original
receiver environment to child operations, including its cancellation token.

Internal failure stops further dispatch and drains every started sibling
without requesting sibling cancellation. The lowest acquired index with a
recorded error wins. A recorded error takes precedence over external stop;
when no error was recorded, external stop or a stopped child produces stopped
completion. A cancellation that prevents a worker from running cannot produce
that worker's hypothetical error.

## Verification still required

`test/execution_tests.cpp` includes concurrent short-operation stress, inline
and asynchronous destructive receivers, queued stopped children, and mixed
stop/failure races. New synchronization gates have deadlines so a missed worker
can be reported without an indefinitely blocked gate. Existing latch-based
cases retain the CTest timeout as their final guard.

The execution target participates in the compiler matrix, ASan/UBSan job, and
TSan job. These tests and jobs have not been run during this source review:
compilation, formatting, and lint checks were explicitly prohibited by the user.
The static trace supports retaining the architecture, but sanitizer and CI
success remain merge prerequisites.

## Static-analysis scope

Clang 22 reports `core.StackAddressEscape` in stdexec's operation-state return
machinery when the ordered pipeline is instantiated. These immovable results
are constructed directly in their destination through guaranteed copy elision;
the call-site suppression attribute does not suppress diagnostics originating
inside stdexec. The CMake helper `dacoexec_suppress_stdexec_stack_escape` disables
only this checker on the execution-test and benchmark executables. It does not
disable the checker for other test targets, nor any other analysis or runtime
instrumentation. Lifetime and race validation still require the destructive
receiver tests and separate ASan/UBSan and TSan runs.

The test and benchmark directories inherit the root clang-tidy configuration,
with cognitive-complexity checks disabled locally so complete validation
scenarios and measured workloads can remain together.

## PR 9 production changes

No runtime execution defect has been demonstrated during this source-only work.
The `commit_batch()` extraction is retained specifically because the supplied
validation log rejected `advance()` at cognitive complexity 45 (limit 25). It
separates ordered commit/error policy from dispatch without changing the batch
or wire-format contract. Existing reversed-completion, failed-batch, cancellation
and retained-result tests cover those boundaries.

Completion preparation is kept separate from delivery because cppcheck reported
`accessMoved` on the error callback following the moved value receiver. Only
`source_.finish()` and preparation of its result belong inside that exception
handler; terminal callbacks may destroy the operation and must never be
followed by another completion attempt. The focused test “Finish failure delivers
one error to a destructive receiver” exercises this catch path, verifies one
terminal completion, and checks that destruction is safe. It remains unexecuted
until an authorized C++ build is available.
