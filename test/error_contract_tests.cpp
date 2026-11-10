#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <dacoexec/error.hpp>
#include <string>
#include <system_error>

auto error_category_from_other_translation_unit() noexcept
    -> std::error_category const &;
namespace {
using dacoexec::errc;
}

TEST_CASE("Structured error values and messages are stable", "[error]") {
  constexpr std::array codes{
      errc::invalid_argument,  errc::invalid_state,
      errc::invalid_archive,   errc::truncated_archive,
      errc::invalid_frame,     errc::invalid_payload,
      errc::checksum_mismatch, errc::output_limit_exceeded,
      errc::numeric_overflow,  errc::io_error,
      errc::unsupported_coder, errc::capacity_exceeded,
      errc::truncated_input,   errc::codeword_limit_exceeded};
  constexpr std::array messages{
      "Invalid argument",  "Invalid state",          "Invalid archive",
      "Truncated archive", "Invalid frame",          "Invalid payload",
      "Checksum mismatch", "Output limit exceeded",  "Numeric overflow",
      "I/O error",         "Unsupported coder",      "Capacity exceeded",
      "Truncated input",   "Codeword limit exceeded"};
  CHECK(std::string(dacoexec::category().name()) == "dacoexec");
  CHECK(&dacoexec::category() == &error_category_from_other_translation_unit());
  for (std::size_t index = 0; index < codes.size(); ++index) {
    std::error_code const code = codes[index];
    CHECK(code.value() == static_cast<int>(index + 1));
    CHECK(code == codes[index]);
    CHECK(codes[index] == code);
    CHECK(code.message() == messages[index]);
    CHECK(code != std::make_error_code(std::errc::invalid_argument));
    dacoexec::exception const plain(codes[index]);
    CHECK(plain.code() == code);
    CHECK(std::string(plain.what()).find(messages[index]) != std::string::npos);
    try {
      throw dacoexec::exception(codes[index], "context");
    } catch (std::system_error const &error) {
      CHECK(error.code() == code);
      CHECK(std::string(error.what()).find("context") != std::string::npos);
    }
  }
  CHECK(dacoexec::category().message(0) == "Unknown dacoexec error");
  CHECK(dacoexec::category().message(-1) == "Unknown dacoexec error");
  CHECK(dacoexec::category().message(999) == "Unknown dacoexec error");
}
