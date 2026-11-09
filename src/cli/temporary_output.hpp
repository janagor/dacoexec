#ifndef DACOEXEC_CLI_TEMPORARY_OUTPUT_HPP
#define DACOEXEC_CLI_TEMPORARY_OUTPUT_HPP

#include <dacoexec/error.hpp>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dacoexec::cli {
// Own the exclusively created handle until all writes and close have succeeded.
class temporary_output
{
  struct file_closer
  {
    auto operator()(std::FILE *file) const noexcept -> void
    {
      // The unique_ptr owns this C handle; destruction cannot report errors.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      static_cast<void>(std::fclose(file));
    }
  };

public:
  explicit temporary_output(std::filesystem::path const &destination)
  {
    std::random_device random;
    constexpr unsigned max_attempts = 64;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
      path_ = destination;
      path_ += ".tmp." + std::to_string(random()) + "." + std::to_string(random());
#ifdef _WIN32
      std::FILE *file = nullptr;
      static_cast<void>(_wfopen_s(&file, path_.c_str(), L"wbx"));
      file_.reset(file);
#else
      file_ = std::unique_ptr<std::FILE, file_closer>{ std::fopen(path_.c_str(), "wbx") };
#endif
      if (file_ != nullptr) { return; }
    }
    throw stream_error("Cannot create temporary output file");
  }

  temporary_output(temporary_output const &) = delete;
  auto operator=(temporary_output const &) -> temporary_output & = delete;
  temporary_output(temporary_output &&) = delete;
  auto operator=(temporary_output &&) -> temporary_output & = delete;

  ~temporary_output()
  {
    file_.reset();
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  auto write(std::span<std::byte const> bytes) -> void
  {
    if (file_ == nullptr) { throw stream_error("Temporary output is closed"); }
    if (!bytes.empty() && std::fwrite(bytes.data(), sizeof(std::byte), bytes.size(), file_.get()) != bytes.size()) {
      throw stream_error("Output file write failed");
    }
  }

  auto close() -> void
  {
    if (file_ == nullptr) { return; }
    if (std::fflush(file_.get()) != 0) { throw stream_error("Output file flush failed"); }
    auto *file = file_.release();
    // Ownership is released solely to check the final C close result.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    if (std::fclose(file) != 0) { throw stream_error("Output file close failed"); }
  }

  // Requires a trusted directory: publication uses the temporary pathname,
  // which must not be replaced by another process. Holding the write handle
  // does not provide identity guarantees for a later pathname operation.
  auto publish(std::filesystem::path const &destination, bool force) -> void
  {
    close();
    if (!force) {
      // Exclusive publication also protects a destination created during encoding.
      std::filesystem::create_hard_link(path_, destination);
    } else {
#ifdef _WIN32
      if (MoveFileExW(path_.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot publish output");
      }
#else
      std::filesystem::rename(path_, destination);
#endif
    }
  }

  [[nodiscard]] auto path() const -> std::filesystem::path const & { return path_; }

private:
  std::filesystem::path path_;
  std::unique_ptr<std::FILE, file_closer> file_;
};
}// namespace dacoexec::cli
#endif// DACOEXEC_CLI_TEMPORARY_OUTPUT_HPP
