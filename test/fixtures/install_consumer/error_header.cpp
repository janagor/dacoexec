#include <dacoexec/error.hpp>
#include <system_error>
#include <type_traits>
static_assert(std::is_base_of_v<std::system_error, dacoexec::exception>);
static_assert(std::is_error_code_enum_v<dacoexec::errc>);

auto installed_error_code() -> std::error_code { return dacoexec::errc::invalid_archive; }
