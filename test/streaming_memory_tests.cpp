#include <dacoexec/ans.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/huffman.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
struct alignas(std::max_align_t) allocation_header
{
  std::size_t size_;
  void *allocation_;
};

struct allocation_counters
{
  std::size_t retained_ = 0;
  std::size_t peak_ = 0;
};

auto allocation_statistics() -> allocation_counters &
{
  static allocation_counters counters;
  return counters;
}

// This replaces global new: containers/smart pointers here would recurse into
// the allocator being measured. Raw allocation and header arithmetic are local.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc,cppcoreguidelines-pro-bounds-pointer-arithmetic)
auto allocate(std::size_t size, std::size_t alignment) -> void *
{
  alignment = std::max(alignment, alignof(allocation_header));
  auto const capacity = std::max(size, std::size_t{ 1 });
  auto const overhead = sizeof(allocation_header) + alignment - 1U;
  if (capacity > std::numeric_limits<std::size_t>::max() - overhead) { throw std::bad_alloc(); }
  auto *allocation = std::malloc(capacity + overhead);
  if (allocation == nullptr) { throw std::bad_alloc(); }
  void *pointer = static_cast<std::byte *>(allocation) + sizeof(allocation_header);
  auto space = capacity + alignment - 1U;
  if (std::align(alignment, capacity, pointer, space) == nullptr) {
    std::free(allocation);
    throw std::bad_alloc();
  }
  auto *header = static_cast<allocation_header *>(pointer) - 1;
  std::construct_at(header, allocation_header{ .size_ = size, .allocation_ = allocation });
  allocation_statistics().retained_ += size;
  allocation_statistics().peak_ = std::max(allocation_statistics().peak_, allocation_statistics().retained_);
  return pointer;
}

auto deallocate(void *pointer) noexcept -> void
{
  if (pointer == nullptr) { return; }
  auto *header = static_cast<allocation_header *>(pointer) - 1;
  allocation_statistics().retained_ -= header->size_;
  std::free(header->allocation_);
}

// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc,cppcoreguidelines-pro-bounds-pointer-arithmetic)
}// namespace

// Isolated executable: track both ordinary and over-aligned heap allocations.
auto operator new(std::size_t size) -> void * { return allocate(size, alignof(std::max_align_t)); }

auto operator new[](std::size_t size) -> void * { return ::operator new(size); }

auto operator delete(void *pointer) noexcept -> void { deallocate(pointer); }

auto operator delete[](void *pointer) noexcept -> void { deallocate(pointer); }

auto operator delete(void *pointer, [[maybe_unused]] std::size_t size) noexcept -> void { deallocate(pointer); }

auto operator delete[](void *pointer, [[maybe_unused]] std::size_t size) noexcept -> void { deallocate(pointer); }

auto operator new(std::size_t size, std::align_val_t alignment) -> void *
{ return allocate(size, static_cast<std::size_t>(alignment)); }

auto operator new[](std::size_t size, std::align_val_t alignment) -> void * { return ::operator new(size, alignment); }

auto operator delete(void *pointer, [[maybe_unused]] std::align_val_t alignment) noexcept -> void
{ deallocate(pointer); }

auto operator delete[](void *pointer, [[maybe_unused]] std::align_val_t alignment) noexcept -> void
{ deallocate(pointer); }

auto operator delete(void *pointer,
  [[maybe_unused]] std::size_t size,
  [[maybe_unused]] std::align_val_t alignment) noexcept -> void
{ deallocate(pointer); }

auto operator delete[](void *pointer,
  [[maybe_unused]] std::size_t size,
  [[maybe_unused]] std::align_val_t alignment) noexcept -> void
{ deallocate(pointer); }

namespace {
namespace huffman = dacoexec::huffman;
constexpr auto memory_block_size = huffman::stream_options{}.block_size_;

// SplitMix64 gives reproducible, near-uniform bytes without retaining PRNG state.
[[nodiscard]] auto source_byte(std::uint64_t offset, bool high_entropy) -> std::byte
{
  if (!high_entropy) { return std::byte{ 0 }; }
  constexpr std::uint64_t increment = 0x9e3779b97f4a7c15ULL;
  constexpr std::uint64_t first_multiplier = 0xbf58476d1ce4e5b9ULL;
  constexpr std::uint64_t second_multiplier = 0x94d049bb133111ebULL;
  constexpr unsigned first_shift = 30;
  constexpr unsigned second_shift = 27;
  constexpr unsigned final_shift = 31;
  // Repeat one pseudorandom frame so both stream lengths exercise identical
  // frame costs. Different random frames can cross a vector growth boundary
  // during trial encoding, independently of total stream length.
  auto value = (offset % memory_block_size) + increment;
  value = (value ^ (value >> first_shift)) * first_multiplier;
  value = (value ^ (value >> second_shift)) * second_multiplier;
  value ^= value >> final_shift;
  return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

struct checking_sink
{
  std::uint64_t count_ = 0;
  bool high_entropy_ = false;

  auto write(std::span<std::byte const> bytes) -> void
  {
    std::uint64_t position = count_;
    for (auto byte : bytes) {
      if (byte != source_byte(position++, high_entropy_)) { throw std::logic_error("Decoded content mismatch"); }
    }
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - count_) {
      throw std::overflow_error("Sink count overflow");
    }
    count_ += bytes.size();
  }
};

// This source has a shared cursor and a void postfix increment: it is genuinely
// single-pass and never owns the complete input or archive.
template<class Generator> class generated_bytes : public std::ranges::view_interface<generated_bytes<Generator>>
{
  std::uint64_t size_;
  std::uint64_t position_ = 0;
  Generator generator_;
  std::optional<std::byte> current_;

public:
  generated_bytes(std::uint64_t size, Generator generator) : size_(size), generator_(std::move(generator)) {}

  class iterator
  {
    generated_bytes *source_;

  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    explicit iterator(generated_bytes *source) : source_(source) {}

    auto operator*() const -> std::byte
    {
      if (!source_->current_) { source_->current_.emplace(source_->generator_(source_->position_)); }
      return source_->current_.value();
    }

    auto operator++() -> iterator &
    {
      ++source_->position_;
      source_->current_.reset();
      return *this;
    }

    auto operator++(int) -> void { ++*this; }

    auto operator==([[maybe_unused]] std::default_sentinel_t sentinel) const -> bool
    { return source_->position_ == source_->size_; }
  };

  auto begin() -> iterator { return iterator{ this }; }

  [[nodiscard]] auto end() const -> std::default_sentinel_t { return {}; }
};

struct file_sink
{
  std::FILE *file_;
  std::uint64_t count_ = 0;

  auto write(std::span<std::byte const> bytes) -> void
  {
    if (std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size()) {
      throw dacoexec::stream_error("Temporary archive write failed");
    }
    count_ += bytes.size();
  }
};

template<bool use_ans> auto exercise(std::uint64_t size, bool high_entropy = false) -> std::size_t
{
  auto const baseline = allocation_statistics().retained_;
  allocation_statistics().peak_ = baseline;
  generated_bytes source(
    size, [high_entropy](std::uint64_t offset) -> std::byte { return source_byte(offset, high_entropy); });
  static_assert(std::ranges::input_range<decltype(source)>);
  static_assert(!std::ranges::forward_range<decltype(source)>);
  std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::tmpfile(), &std::fclose);
  if (!file) { throw dacoexec::stream_error("Cannot create temporary archive"); }
  file_sink compressed{ .file_ = file.get() };
  auto const encoded = [&]() -> auto {
    if constexpr (use_ans) {
      return dacoexec::ans::compress_stream(source, compressed, { .block_size_ = memory_block_size });
    } else {
      return huffman::compress_stream(source, compressed, { .block_size_ = memory_block_size });
    }
  }();
  auto const expected_frames = (size / memory_block_size) + (size % memory_block_size != 0 ? 1U : 0U);
  if (encoded.input_bytes_ != size || encoded.output_bytes_ != compressed.count_
      || encoded.frames_ != expected_frames) {
    throw std::logic_error("Compression statistics mismatch");
  }
  if (high_entropy) {
    // DHF1/DAN1 RAW frames add only the fixed 13-byte frame header. Verify
    // this workload actually selects RAW for every frame, rather than merely
    // assuming random bytes exercise that path.
    constexpr std::uint64_t frame_header_bytes = 13;
    constexpr std::uint64_t empty_archive_bytes = 21;
    if (encoded.output_bytes_ != size + empty_archive_bytes + (expected_frames * frame_header_bytes)) {
      throw std::logic_error("High-entropy fixture did not select RAW frames");
    }
  }
  if (std::fseek(file.get(), 0, SEEK_SET) != 0) { throw dacoexec::stream_error("Cannot rewind temporary archive"); }
  generated_bytes archive(compressed.count_, [&file](std::uint64_t) -> std::byte {
    auto const value = std::fgetc(file.get());
    if (value == EOF) { throw dacoexec::stream_error("Temporary archive read failed"); }
    return static_cast<std::byte>(static_cast<unsigned char>(value));
  });
  checking_sink decoded{ .high_entropy_ = high_entropy };
  auto const statistics = [&]() -> auto {
    if constexpr (use_ans) {
      return dacoexec::ans::decompress_stream(archive, decoded);
    } else {
      return huffman::decompress_stream(archive, decoded);
    }
  }();
  if (statistics.input_bytes_ != compressed.count_ || statistics.output_bytes_ != size || decoded.count_ != size
      || statistics.frames_ != expected_frames) {
    throw std::logic_error("Decompression statistics mismatch");
  }
  return allocation_statistics().peak_ - baseline;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    constexpr std::size_t probe_alignment = 64;
    constexpr std::size_t probe_size = 257;
    auto const baseline = allocation_statistics().retained_;
    auto *probe = ::operator new(probe_size, std::align_val_t{ probe_alignment });
    auto const tracked = allocation_statistics().retained_ == baseline + probe_size;
    auto *aligned_probe = probe;
    auto probe_space = probe_size;
    auto const aligned = std::align(probe_alignment, probe_size, aligned_probe, probe_space) == probe;
    ::operator delete(probe, std::align_val_t{ probe_alignment });
    if (!tracked || !aligned || allocation_statistics().retained_ != baseline) {
      throw std::logic_error("Aligned allocation tracking failed");
    }
    auto const arguments = std::span(argv, static_cast<std::size_t>(argc));
    if (arguments.size() > 1 && std::string_view(arguments.subspan(1).front()) == "--large") {
      constexpr auto large_size = (std::uint64_t{ 1 } << 32U) + 4096U;
      static_cast<void>(exercise<false>(large_size));
      static_cast<void>(exercise<true>(large_size));
    } else {
      constexpr auto smaller_size = memory_block_size * 2U;
      constexpr auto larger_size = memory_block_size * 16U;
      for (bool const high_entropy : { false, true }) {
        auto const smaller_huffman_peak = exercise<false>(smaller_size, high_entropy);
        auto const larger_huffman_peak = exercise<false>(larger_size, high_entropy);
        auto const smaller_ans_peak = exercise<true>(smaller_size, high_entropy);
        auto const larger_ans_peak = exercise<true>(larger_size, high_entropy);
        // Permit one extra frame for implementation-dependent capacity differences.
        if (larger_huffman_peak > smaller_huffman_peak + memory_block_size
            || larger_ans_peak > smaller_ans_peak + memory_block_size) {
          std::cerr << "high_entropy=" << high_entropy << " huffman peaks=" << smaller_huffman_peak << ','
                    << larger_huffman_peak << " ans peaks=" << smaller_ans_peak << ',' << larger_ans_peak << '\n';
          throw std::logic_error("Retained allocation grows with stream length");
        }
      }
    }
    return 0;
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
