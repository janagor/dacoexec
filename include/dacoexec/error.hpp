//! \file
//! Structured errors detected by dacoexec. Client and allocation exceptions propagate unchanged.
#ifndef DACOEXEC_ERROR_HPP
#define DACOEXEC_ERROR_HPP

#include <string>
#include <system_error>
#include <type_traits>

namespace dacoexec {

// Public values are stable; append new codes rather than renumbering them.
enum class errc {
  invalid_argument = 1,
  invalid_state = 2,
  invalid_archive = 3,
  truncated_archive = 4,
  invalid_frame = 5,
  invalid_payload = 6,
  checksum_mismatch = 7,
  output_limit_exceeded = 8,
  numeric_overflow = 9,
  io_error = 10,
  unsupported_coder = 11,
  capacity_exceeded = 12,
  truncated_input = 13,
  codeword_limit_exceeded = 14,
};

[[nodiscard]] inline auto category() noexcept -> std::error_category const &
{
  class error_category final : public std::error_category
  {
  public:
    [[nodiscard]] auto name() const noexcept -> char const * override { return "dacoexec"; }

    [[nodiscard]] auto message(int value) const -> std::string override
    {
      switch (static_cast<errc>(value)) {
      case errc::invalid_argument:
        return "Invalid argument";
      case errc::invalid_state:
        return "Invalid state";
      case errc::invalid_archive:
        return "Invalid archive";
      case errc::truncated_archive:
        return "Truncated archive";
      case errc::invalid_frame:
        return "Invalid frame";
      case errc::invalid_payload:
        return "Invalid payload";
      case errc::checksum_mismatch:
        return "Checksum mismatch";
      case errc::output_limit_exceeded:
        return "Output limit exceeded";
      case errc::numeric_overflow:
        return "Numeric overflow";
      case errc::io_error:
        return "I/O error";
      case errc::unsupported_coder:
        return "Unsupported coder";
      case errc::capacity_exceeded:
        return "Capacity exceeded";
      case errc::truncated_input:
        return "Truncated input";
      case errc::codeword_limit_exceeded:
        return "Codeword limit exceeded";
      }
      return "Unknown dacoexec error";
    }
  };

  static error_category const instance;
  return instance;
}

[[nodiscard]] inline auto make_error_code(errc code) noexcept -> std::error_code
{ return { static_cast<int>(code), category() }; }

class exception : public std::system_error
{
public:
  explicit exception(errc code) : std::system_error(make_error_code(code)) {}

  exception(errc code, std::string const &context) : std::system_error(make_error_code(code), context) {}
};

}// namespace dacoexec

namespace std {
template<> struct is_error_code_enum<dacoexec::errc> : true_type
{
};
}// namespace std
#endif// DACOEXEC_ERROR_HPP
