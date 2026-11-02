#include <dacoexec/dacoexec.hpp>

#include <cstddef>
#include <iostream>
#include <ranges>
#include <span>

int main()
{
  // The parent project selects C++23. Embedding dacoexec must not lower it.
  // GCC 12 and Clang 16 report the provisional C++2b value, which is still
  // above the C++20 baseline of 202002L.
#if defined(_MSVC_LANG)
  static_assert(_MSVC_LANG > 202002L);
#else
  static_assert(__cplusplus > 202002L);
#endif
  static_assert(std::ranges::input_range<std::span<std::byte const>>);

  std::cout << "dacoexec subproject consumer ok\n";
  return 0;
}
