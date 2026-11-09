#include "temporary_output.hpp"
#include <CLI/CLI.hpp>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/huffman.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

// Bulk reads preserve I/O failures rather than reducing them to iterator EOF.
class file_bytes : public std::ranges::view_interface<file_bytes>
{
public:
  explicit file_bytes(std::istream &stream) : stream_(&stream) {}

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    explicit iterator(file_bytes *source) : source_(source) {}

    auto operator*() const -> char { return source_->buffer_.at(source_->position_); }

    auto operator++() -> iterator &
    {
      if (++source_->position_ == source_->size_) { source_->fill(); }
      return *this;
    }

    auto operator++(int) -> void { ++*this; }

    auto operator==([[maybe_unused]] std::default_sentinel_t sentinel) const -> bool { return source_->finished_; }

  private:
    file_bytes *source_;
  };

  auto begin() -> iterator
  {
    if (!started_) {
      started_ = true;
      fill();
    }
    return iterator{ this };
  }

  [[nodiscard]] static auto end() noexcept -> std::default_sentinel_t { return {}; }

private:
  auto fill() -> void
  {
    position_ = 0;
    size_ = 0;
    if (finished_) { return; }
    try {
      if (!stream_->eof()) {
        stream_->read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        if (stream_->bad() || (stream_->fail() && !stream_->eof())) {
          throw dacoexec::stream_error("Input read failed");
        }
        size_ = static_cast<std::size_t>(stream_->gcount());
      }
      finished_ = size_ == 0;
    } catch (...) {
      finished_ = true;
      throw;
    }
  }

  static constexpr std::size_t buffer_size = 64U << 10U;
  std::istream *stream_;
  std::array<char, buffer_size> buffer_{};
  std::size_t position_ = 0;
  std::size_t size_ = 0;
  bool started_ = false;
  bool finished_ = false;
};

struct process_options
{
  std::string input_name_;
  std::string output_name_;
  bool force_;
  bool compress_;
  std::string algorithm_;
  std::size_t block_size_;
  std::uint64_t output_limit_;
  unsigned table_log_;
};

auto process(process_options const &options) -> void
{
  auto const input_path = fs::absolute(options.input_name_);
  auto const output_path = fs::absolute(options.output_name_);
  if (fs::exists(output_path) && fs::equivalent(input_path, output_path)) {
    throw dacoexec::stream_error("Input and output refer to the same file");
  }
  if (fs::exists(output_path) && !options.force_) {
    throw dacoexec::stream_error("Output exists; use --force to replace it");
  }
  std::ifstream input(input_path, std::ios::binary);
  if (!input) { throw dacoexec::stream_error("Cannot open input file"); }
  // badbit distinguishes underlying read failures from ordinary EOF.
  input.exceptions(std::ios::badbit);
  dacoexec::cli::temporary_output output(output_path);
  file_bytes source(input);
  if (options.compress_) {
    if (options.algorithm_ == "rans" || options.algorithm_ == "tans") {
      static_cast<void>(dacoexec::ans::compress_stream(source,
        output,
        { .block_size_ = options.block_size_,
          .coder_ =
            options.algorithm_ == "tans" ? dacoexec::ans::entropy_coder::tans : dacoexec::ans::entropy_coder::rans,
          .table_log_ = options.table_log_ }));
    } else {
      static_cast<void>(dacoexec::huffman::compress_stream(source, output, { .block_size_ = options.block_size_ }));
    }
  } else {
    std::array<char, dacoexec::detail::ans::archive_header_size> prefix{};
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    auto const is_ans = input.gcount() == static_cast<std::streamsize>(prefix.size())
                        && std::ranges::equal(
                          prefix, dacoexec::detail::ans::archive_header_bytes, [](char left, std::byte right) -> bool {
                            return dacoexec::detail::support::to_byte(left) == right;
                          });
    input.clear();
    input.seekg(0);
    if (!input) { throw dacoexec::stream_error("Cannot rewind input file"); }
    if (is_ans) {
      static_cast<void>(
        dacoexec::ans::decompress_stream(source, output, { .max_output_bytes_ = options.output_limit_ }));
    } else {
      static_cast<void>(
        dacoexec::huffman::decompress_stream(source, output, { .max_output_bytes_ = options.output_limit_ }));
    }
  }
  if (input.bad()) { throw dacoexec::stream_error("Input read failed"); }
  output.close();
  input.close();
  output.publish(output_path, options.force_);
}
}// namespace

auto main(int argc, char **argv) -> int
try {
  CLI::App app{ "Sequential Huffman, rANS and tANS file compression" };
  app.require_subcommand(1);
  std::string input;
  std::string output;
  std::string algorithm;
  bool force = false;
  auto table_log = dacoexec::ans::stream_options{}.table_log_;
  auto block_size = dacoexec::huffman::stream_options{}.block_size_;
  auto output_limit = std::numeric_limits<std::uint64_t>::max();
  auto *compress = app.add_subcommand("compress", "Compress a file to DHF1 or DAN1");
  compress->add_option("--algorithm", algorithm)->required()->check(CLI::IsMember({ "huffman", "rans", "tans" }));
  auto *table_log_option =
    compress->add_option("--table-log", table_log, "tANS precision (8-12); requires --algorithm tans");
  compress->add_option("--block-size", block_size)
    ->check(CLI::Range(std::size_t{ 1 }, dacoexec::huffman::decode_options{}.max_frame_size_));
  compress->add_option("input", input)->required();
  compress->add_option("output", output)->required();
  compress->add_flag("--force", force);
  auto *decompress = app.add_subcommand("decompress", "Decompress a DHF1 or DAN1 file");
  decompress->add_option("--max-output-size", output_limit);
  decompress->add_option("input", input)->required();
  decompress->add_option("output", output)->required();
  decompress->add_flag("--force", force);
  CLI11_PARSE(app, argc, argv);
  if (table_log_option->count() != 0 && algorithm != "tans") {
    throw std::invalid_argument("--table-log requires --algorithm tans");
  }
  process({ .input_name_ = input,
    .output_name_ = output,
    .force_ = force,
    .compress_ = compress->parsed(),
    .algorithm_ = algorithm,
    .block_size_ = block_size,
    .output_limit_ = output_limit,
    .table_log_ = table_log });
  return 0;
} catch (std::exception const &error) {
  std::cerr << "dacoexec: " << error.what() << '\n';
  return 1;
} catch (...) {
  std::cerr << "dacoexec: Unknown failure\n";
  return 1;
}
