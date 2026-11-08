#include "temporary_output.hpp"
#include <CLI/CLI.hpp>
#include <dacoexec/dacoexec.hpp>

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

    auto operator==(std::default_sentinel_t) const -> bool { return source_->finished_; }

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

  auto end() const -> std::default_sentinel_t { return {}; }

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

auto process(std::string const &input_name,
  std::string const &output_name,
  bool force,
  bool compress,
  std::string const &algorithm,
  std::size_t block_size,
  std::uint64_t output_limit) -> void
{
  auto const input_path = fs::absolute(input_name);
  auto const output_path = fs::absolute(output_name);
  if (fs::exists(output_path) && fs::equivalent(input_path, output_path)) {
    throw dacoexec::stream_error("Input and output refer to the same file");
  }
  if (fs::exists(output_path) && !force) { throw dacoexec::stream_error("Output exists; use --force to replace it"); }
  std::ifstream input(input_path, std::ios::binary);
  if (!input) { throw dacoexec::stream_error("Cannot open input file"); }
  // badbit distinguishes underlying read failures from ordinary EOF.
  input.exceptions(std::ios::badbit);
  dacoexec::cli::temporary_output output(output_path);
  file_bytes source(input);
  if (compress) {
    if (algorithm == "rans") {
      static_cast<void>(dacoexec::ans::compress_stream(source, output, { .block_size_ = block_size }));
    } else {
      static_cast<void>(dacoexec::huffman::compress_stream(source, output, { .block_size_ = block_size }));
    }
  } else {
    std::array<char, dacoexec::ans::archive_header_size> prefix{};
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    auto const is_ans = input.gcount() == static_cast<std::streamsize>(prefix.size())
                        && std::ranges::equal(prefix,
                          dacoexec::ans::archive_header_bytes,
                          [](char left, std::byte right) -> bool { return dacoexec::detail::to_byte(left) == right; });
    input.clear();
    input.seekg(0);
    if (!input) { throw dacoexec::stream_error("Cannot rewind input file"); }
    if (is_ans) {
      static_cast<void>(dacoexec::ans::decompress_stream(
        source, output, { .max_frame_size_ = dacoexec::ans::max_frame_size, .max_output_bytes_ = output_limit }));
    } else {
      static_cast<void>(dacoexec::huffman::decompress_stream(
        source, output, { .max_frame_size_ = dacoexec::huffman::max_frame_size, .max_output_bytes_ = output_limit }));
    }
  }
  if (input.bad()) { throw dacoexec::stream_error("Input read failed"); }
  output.close();
  input.close();
  output.publish(output_path, force);
}
}// namespace

auto main(int argc, char **argv) -> int
{
  CLI::App app{ "Sequential Huffman and rANS file compression" };
  app.require_subcommand(1);
  std::string input;
  std::string output;
  std::string algorithm;
  bool force = false;
  auto block_size = dacoexec::huffman::default_block_size;
  auto output_limit = std::numeric_limits<std::uint64_t>::max();
  auto *compress = app.add_subcommand("compress", "Compress a file to DHF1 or DAN1");
  compress->add_option("--algorithm", algorithm)->required()->check(CLI::IsMember({ "huffman", "rans" }));
  compress->add_option("--block-size", block_size)
    ->check(CLI::Range(std::size_t{ 1 }, dacoexec::huffman::max_frame_size));
  compress->add_option("input", input)->required();
  compress->add_option("output", output)->required();
  compress->add_flag("--force", force);
  auto *decompress = app.add_subcommand("decompress", "Decompress a DHF1 or DAN1 file");
  decompress->add_option("--max-output-size", output_limit);
  decompress->add_option("input", input)->required();
  decompress->add_option("output", output)->required();
  decompress->add_flag("--force", force);
  CLI11_PARSE(app, argc, argv);
  try {
    process(input, output, force, compress->parsed(), algorithm, block_size, output_limit);
    return 0;
  } catch (std::exception const &error) {
    std::cerr << "dacoexec: " << error.what() << '\n';
    return 1;
  }
}
