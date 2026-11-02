#include <dacoexec/dacoexec.hpp>

#include <cstddef>
#include <iostream>
#include <ranges>
#include <span>

int main()
{
#if defined(_MSVC_LANG)
  static_assert(_MSVC_LANG >= 202002L);
#else
  static_assert(__cplusplus >= 202002L);
#endif
  static_assert(std::ranges::input_range<std::span<std::byte const>>);

  std::cout << "dacoexec install consumer ok\n";
  return 0;
}
