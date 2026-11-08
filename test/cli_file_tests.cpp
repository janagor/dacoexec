#include "../src/cli/temporary_output.hpp"
#include <catch2/catch_test_macros.hpp>

#include <dacoexec/detail/error.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;

class temporary_directory
{
public:
  temporary_directory()
  {
    std::random_device random;
    constexpr unsigned max_attempts = 64;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
      path_ = fs::temp_directory_path() / ("dacoexec-publication-" + std::to_string(random()));
      if (fs::create_directory(path_)) { return; }
    }
    throw std::runtime_error("Cannot create test directory");
  }

  temporary_directory(temporary_directory const &) = delete;
  auto operator=(temporary_directory const &) -> temporary_directory & = delete;
  temporary_directory(temporary_directory &&) = delete;
  auto operator=(temporary_directory &&) -> temporary_directory & = delete;

  ~temporary_directory()
  {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] auto path() const -> fs::path const & { return path_; }

private:
  fs::path path_;
};

auto read_file(fs::path const &path) -> std::string
{
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file.good());
  return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

auto write_file(fs::path const &path, std::string const &content) -> void
{
  std::ofstream file(path, std::ios::binary);
  file.exceptions(std::ios::badbit | std::ios::failbit);
  file << content;
  file.close();
}
}// namespace

TEST_CASE("CLI publication preserves a destination created during processing", "[cli][files]")
{
  temporary_directory const directory;
  auto const destination = directory.path() / "output.dhf";
  auto const original = std::string("created by another writer");
  fs::path temporary_path;
  {
    REQUIRE_FALSE(fs::exists(destination));
    dacoexec::cli::temporary_output output(destination);
    temporary_path = output.path();
    std::array const bytes{ std::byte{ 'n' }, std::byte{ 'e' }, std::byte{ 'w' } };
    output.write(bytes);
    // Deterministically interleave another writer after the existence check.
    write_file(destination, original);
    REQUIRE_THROWS_AS(output.publish(destination, false), fs::filesystem_error);
    REQUIRE(read_file(destination) == original);
  }
  REQUIRE_FALSE(fs::exists(temporary_path));
  REQUIRE(read_file(destination) == original);
}

TEST_CASE("CLI temporary output closes before publication and cleans up failures", "[cli][files]")
{
  temporary_directory const directory;
  auto const destination = directory.path() / "output.dhf";
  std::array const bytes{ std::byte{ 'x' } };
  fs::path temporary_path;
  {
    dacoexec::cli::temporary_output output(destination);
    temporary_path = output.path();
    output.write(bytes);
    output.publish(destination, false);
    REQUIRE(read_file(destination) == "x");
    REQUIRE_THROWS_AS(output.write(bytes), dacoexec::stream_error);
  }
  REQUIRE_FALSE(fs::exists(temporary_path));
  {
    dacoexec::cli::temporary_output output(destination);
    output.write(bytes);
    temporary_path = output.path();
    // Model an exception that abandons processing before close/publication.
  }
  REQUIRE_FALSE(fs::exists(temporary_path));
  REQUIRE(read_file(destination) == "x");
}

#ifndef _WIN32
TEST_CASE("CLI writes retain the exclusive handle after temporary-path replacement", "[cli][files]")
{
  temporary_directory const directory;
  auto const victim = directory.path() / "victim.bin";
  auto const destination = directory.path() / "output.dhf";
  write_file(victim, "preserve me");
  {
    dacoexec::cli::temporary_output output(destination);
    REQUIRE(fs::remove(output.path()));
    fs::create_symlink(victim, output.path());
    std::array const bytes{ std::byte{ 'x' } };
    output.write(bytes);
    output.close();
    REQUIRE(read_file(victim) == "preserve me");
  }
  REQUIRE(read_file(victim) == "preserve me");
}
#endif
