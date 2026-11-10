#ifndef DACOEXEC_TEST_ERROR_TEST_UTILS_HPP
#define DACOEXEC_TEST_ERROR_TEST_UTILS_HPP
#include <catch2/matchers/catch_matchers.hpp>
#include <dacoexec/error.hpp>
#include <string>

namespace dacoexec::test {
class error_code_matcher : public Catch::Matchers::MatcherBase<exception>
{
public:
  explicit error_code_matcher(errc expected) : expected_(make_error_code(expected)) {}

  [[nodiscard]] auto match(exception const &error) const -> bool override { return error.code() == expected_; }

  [[nodiscard]] auto describe() const -> std::string override
  { return "has dacoexec error code " + std::to_string(expected_.value()) + " (" + expected_.message() + ")"; }

private:
  std::error_code expected_;
};

[[nodiscard]] inline auto error_is(errc expected) -> error_code_matcher { return error_code_matcher{ expected }; }
}// namespace dacoexec::test
#endif
