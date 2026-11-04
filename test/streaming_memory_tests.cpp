#include <dacoexec/huffman/format.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/io/output_sink.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

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
struct checking_sink
{
  std::uint64_t count_ = 0;
  bool check_zero_ = false;
  auto write(std::span<std::byte const> bytes) -> void
  {
    if (check_zero_ && std::ranges::any_of(bytes, [](auto byte) -> bool { return byte != std::byte{ 0 }; })) {
      throw std::logic_error("Decoded content mismatch");
    }
    huffman::detail::checked_add(count_, bytes.size());
  }
};

auto exercise(std::uint64_t size) -> std::size_t
{
  auto const baseline = allocation_statistics().retained_;
  allocation_statistics().peak_ = baseline;
  auto source = std::views::iota(std::uint64_t{ 0 }, size)
                | std::views::transform([](auto) -> std::byte { return std::byte{ 0 }; });
  checking_sink compressed;
  auto const encoded = huffman::compress_stream(source, compressed);
  auto const expected_frames =
    (size / huffman::default_block_size) + (size % huffman::default_block_size != 0 ? 1U : 0U);
  if (encoded.input_bytes_ != size || encoded.output_bytes_ != compressed.count_
      || encoded.frames_ != expected_frames) {
    throw std::logic_error("Compression statistics mismatch");
  }
  auto make_frame = [](std::size_t length) -> std::vector<std::byte> {
    auto frame = huffman::encode_frame({ 0, std::vector<std::byte>(length, std::byte{ 0 }) });
    std::vector<std::byte> bytes;
    dacoexec::io::iterator_sink sink(std::back_inserter(bytes));
    huffman::write_frame(sink, frame);
    return bytes;
  };
  auto const full = make_frame(huffman::default_block_size);
  auto const tail_size = static_cast<std::size_t>(size % huffman::default_block_size);
  auto const tail = tail_size == 0 ? std::vector<std::byte>{} : make_frame(tail_size);
  auto const full_count = size / huffman::default_block_size;
  auto const full_bytes = full_count * full.size();
  auto const archive_size = huffman::archive_header_size + full_bytes + tail.size() + huffman::frame_header_size;
  auto archive = std::views::iota(std::uint64_t{ 0 }, archive_size)
                 | std::views::transform([&full, &tail, full_bytes](std::uint64_t offset) -> std::byte {
                     if (offset < huffman::archive_header_size) {
                       return huffman::archive_header_bytes.at(static_cast<std::size_t>(offset));
                     }
                     offset -= huffman::archive_header_size;
                     if (offset < full_bytes) { return full.at(static_cast<std::size_t>(offset % full.size())); }
                     offset -= full_bytes;
                     if (offset < tail.size()) { return tail.at(static_cast<std::size_t>(offset)); }
                     offset -= tail.size();
                     return offset == 0 ? static_cast<std::byte>(huffman::frame_kind::end) : std::byte{ 0 };
                   });
  checking_sink decoded{ .check_zero_ = true };
  auto const statistics = huffman::decompress_stream(archive, decoded);
  if (statistics.input_bytes_ != archive_size || statistics.output_bytes_ != size || decoded.count_ != size
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
      static_cast<void>(exercise(large_size));
    } else {
      constexpr auto smaller_size = huffman::default_block_size * 2U;
      constexpr auto larger_size = huffman::default_block_size * 16U;
      auto const smaller_peak = exercise(smaller_size);
      auto const larger_peak = exercise(larger_size);
      // Permit one extra block for implementation-dependent capacity differences.
      if (larger_peak > smaller_peak + huffman::default_block_size) {
        throw std::logic_error("Retained allocation grows with stream length");
      }
    }
    return 0;
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
