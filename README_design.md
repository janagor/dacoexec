# Design Choices

This template starts a C++ project with safe, modern defaults. Each choice
below explains *why*, so you can keep it, swap it, or turn it off.

## Goals

1. Catch bugs at compile time, not in production.
2. Stay portable across GCC, Clang, MSVC, and Emscripten.
3. Work the same way as a top-level project or as a subdirectory dependency.

## Layout

| File | Role |
| --- | --- |
| `CMakeLists.txt` | Top-level wiring. |
| `ProjectOptions.cmake` | All `myproject_*` options and setup macros. |
| `Dependencies.cmake` | CPM package fetch, gated by `if(NOT TARGET ...)`. |
| `cmake/*.cmake` | One concern per file (warnings, sanitizers, hardening, ...). |

`PROJECT_IS_TOP_LEVEL` flips defaults: strict when you own the build, quiet
when you are a dependency.

## C++ standard

C++23, set only if a parent project has not chosen one. `CMAKE_CXX_EXTENSIONS`
is off so the standard flag is `-std=c++23`, not `-std=gnu++23`. This avoids
`-Wpedantic` conflicts with precompiled headers.

## Warnings

`cmake/CompilerWarnings.cmake` enables a curated set per compiler — `/W4`
plus extras on MSVC, and `-Wall -Wextra -Wshadow -Wconversion -Wpedantic ...`
on GCC/Clang. Top-level builds add `-Werror` / `/WX`. Source:
[cppbestpractices](https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md).

## Sanitizers

ASan and UBSan are on by default for top-level GCC/Clang builds when a link
probe shows them working. TSan, LSan, and MSan are off — they conflict with
each other and MSan needs an instrumented standard library. Emscripten and
MSVC skip the sanitizer pass.

## Hardening

`cmake/Hardening.cmake` adds `_FORTIFY_SOURCE=3` (release builds),
`_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`, `-fcf-protection`, and
`-fstack-clash-protection` when supported. MSVC gets `/sdl /DYNAMICBASE
/guard:cf /NXCOMPAT /CETCOMPAT`. When no full sanitizer is active, the UBSan
minimal runtime is layered on top.

## Static analysis

clang-tidy and cppcheck run as part of the build, on by default at top level.
They are separate options because one tool may not be installed in every
environment.

## Link-time optimization

IPO/LTO is on by default at top level. It is gated through
`check_ipo_supported` so unsupported toolchains skip it.

## Dependencies

[CPM](https://github.com/cpm-cmake/CPM.cmake) fetches sources at configure
time. Each package is gated by `if(NOT TARGET ...)`, so a parent project can
supply its own version. `SYSTEM YES` silences warnings from third-party
headers. The test suite uses Catch2. The library target has no third-party dependencies.

## Testing

* `test/tests.cpp` — Catch2 unit tests.
* `test/constexpr_tests.cpp` — the same checks at compile time, so bugs
  become build errors.
* `fuzz_test/` — libFuzzer harness, auto-enabled when ASan/TSan/UBSan and
  libFuzzer are all available.

## Targets and packaging

`myproject_options` and `myproject_warnings` are `INTERFACE` libraries that
hold flags. Real targets link them to inherit the configuration without
touching global state. `CPack` package names embed compiler, version, and
short Git SHA, so a binary maps to one build.

## Defaults for daily use

The default build type is `RelWithDebInfo` — debuggable and fast.
`compile_commands.json` is always exported, for editors and clang tooling.

## Changing the defaults

Every knob is a CMake option named `myproject_ENABLE_<feature>`. Flip it on
the configure line, for example:

    cmake -B build -S . -Dmyproject_ENABLE_CLANG_TIDY=OFF

The `myproject_` prefix is the placeholder the rename workflow replaces, so
renaming the project is one search-and-replace.

## Public API and implementation visibility

The supported API consists only of declarations documented in the public facade
headers. All other declarations, including implementation classes, concrete
archive representations, codec bases, helper algorithms, and internal execution
facilities, are unsupported implementation details. Internal headers are
installed only to satisfy compilation requirements of the header-only library.

CMake's explicit `DACOEXEC_PUBLIC_HEADERS` inventory contains six core facades:
`dacoexec.hpp`, `huffman.hpp`, `ans.hpp`, `views.hpp`, `bit.hpp`, and `error.hpp`.
The optional execution target adds only `execution.hpp`. Every other header lives
under `include/dacoexec/detail/`, and implementation declarations live under
`dacoexec::detail`. Subsystem namespaces are directly beneath that single detail namespace.

Shared block workflows, sequential stream loops and
accounting, frame header serialization, parser lifecycle, and parallel archive
sources live in `detail/codec/` and are used by both Huffman and ANS. Format
policies retain codec metadata parsing, tag validation, encoding/decoding, archive
writes, and diagnostics.
Sequential and parallel compression share format hooks for metadata costs and
serialization; frame views retain their individual iterator ownership semantics.
The constexpr fixed-capacity priority queue lives in `detail/adt/`. Typed node ownership lives
in `detail/memory/`; it has no codec dependency. Huffman keeps its tree-specific
storage aliases beside tree construction. Byte-frequency counting, normalized frequency
models, and normalization helpers live in `detail/model/` under `dacoexec::detail::model`.
Huffman builds code lengths from the shared raw counts; ANS normalizes those counts
to a power-of-two total. Codec-specific validation and wire formats (including ANS
model metadata) stay in their respective codec directories.

Codec facades consume ordinary byte ranges and return byte vectors or public
statistics. Public option/statistics structs own their interface; they do not
alias private block or stream classes. Parallel senders map completion values to
public statistics and keep owned output-iterator adapters alive until completion.
Public bit I/O classes are defined in `dacoexec` in `bit.hpp`; only their helpers
live under `detail/bit/`. Concrete view,
iterator, and sender types are unspecified and should not be named by consumers.

Models are estimated internally. An entropy/model facade is deferred until a
consumer-facing operation needs custom probability distributions. Frame parsing,
node storage, canonical codebooks, blocks, integer primitives, sink concepts, and
generic ordered processing have no public entry points. This is an intentional
source API break, while DHF1 and DAN1 wire compatibility is preserved.

Tests of implementation algorithms can include private headers. Public-header
checks and relocated installed consumers use only the facade headers. Facade
tests exercise archive operations, output destinations, integer/bit view composition,
resource bounds, and public exceptions without using private representations.

Codec views are deferred rather than caching a complete archive on first iteration.
The public streaming functions provide incremental, frame-bounded processing.
Codec option/statistics types remain separate so algorithm-specific settings can
evolve independently. Installed consumers compile each facade in its own translation
unit, and allocation-tracked streaming tests exercise both codecs with single-pass
sources and temporary-file archives whose total size exceeds the frame size.
