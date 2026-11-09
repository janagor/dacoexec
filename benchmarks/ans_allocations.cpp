//! Allocation probe for ordinary allocations used by single-threaded codecs.
#include "fixtures.hpp"
#include <cstddef>
#include <cstdlib>
#include <dacoexec/ans/block.hpp>
#include <dacoexec/ans/encoded_block.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/encoded_block.hpp>
#include <dacoexec/views/blocks.hpp>
#include <exception>
#include <iostream>
#include <new>
#include <utility>

namespace {
struct allocation_statistics
{
  std::size_t calls_ = 0;
  std::size_t bytes_ = 0;
};

auto active_statistics() noexcept -> allocation_statistics *&
{
  static thread_local allocation_statistics *active = nullptr;
  return active;
}
}// namespace

// Global allocation hooks must use raw storage: containers or smart pointers
// would recurse into the allocation being measured, as in streaming_memory_tests.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
auto operator new(std::size_t size) -> void *
{
  auto *memory = std::malloc(size == 0 ? 1 : size);
  if (memory == nullptr) { throw std::bad_alloc{}; }
  if (auto *statistics = active_statistics()) {
    ++statistics->calls_;
    statistics->bytes_ += size;
  }
  return memory;
}

auto operator new[](std::size_t size) -> void * { return ::operator new(size); }

auto operator delete(void *memory) noexcept -> void { std::free(memory); }

auto operator delete[](void *memory) noexcept -> void { std::free(memory); }

auto operator delete(void *memory, [[maybe_unused]] std::size_t size) noexcept -> void { std::free(memory); }

auto operator delete[](void *memory, [[maybe_unused]] std::size_t size) noexcept -> void { std::free(memory); }

// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)

namespace {
class measurement
{
public:
  explicit measurement(allocation_statistics &statistics) : previous_(active_statistics())
  { active_statistics() = &statistics; }

  ~measurement() { active_statistics() = previous_; }

  measurement(measurement const &) = delete;
  auto operator=(measurement const &) -> measurement & = delete;
  measurement(measurement &&) = delete;
  auto operator=(measurement &&) -> measurement & = delete;

private:
  allocation_statistics *previous_;
};
}// namespace

auto main() -> int
try {
  std::cout << "algorithm,operation,block_bytes,dataset,allocations,allocated_bytes\n";
  for (auto size : fixtures::block_sizes) {
    for (auto kind : fixtures::datasets) {
      auto const input = fixtures::make(size, kind);
      auto const report = [size, kind](
                            char const *algorithm, char const *operation, allocation_statistics statistics) -> void {
        std::cout << algorithm << ',' << operation << ',' << size << ','
                  << fixtures::names.at(static_cast<std::size_t>(kind)) << ',' << statistics.calls_ << ','
                  << statistics.bytes_ << '\n';
      };
      dacoexec::ans::encoded_block ans_encoded;
      dacoexec::huffman::encoded_block huffman_encoded;
      allocation_statistics statistics;
      {
        dacoexec::byte_block block{ 0, input };
        measurement const guard{ statistics };
        ans_encoded = dacoexec::ans::compress_block(std::move(block));
      }
      report("rans", "encode", statistics);
      statistics = {};
      {
        measurement const guard{ statistics };
        auto decoded = dacoexec::ans::decompress_block(std::move(ans_encoded), size);
        if (decoded.bytes_ != input) { return 1; }
      }
      report("rans", "decode", statistics);
      statistics = {};
      {
        dacoexec::byte_block block{ 0, input };
        measurement const guard{ statistics };
        huffman_encoded = dacoexec::huffman::compress_block(std::move(block));
      }
      report("huffman", "encode", statistics);
      statistics = {};
      {
        measurement const guard{ statistics };
        auto decoded = dacoexec::huffman::decompress_block(std::move(huffman_encoded), size);
        if (decoded.bytes_ != input) { return 1; }
      }
      report("huffman", "decode", statistics);
    }
  }
  return 0;
} catch (std::exception const &error) {
  std::cerr << error.what() << '\n';
  return 1;
} catch (...) {
  std::cerr << "Allocation probe failed with a non-standard exception\n";
  return 1;
}
