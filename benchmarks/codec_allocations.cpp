//! Allocation probe for ordinary allocations used by single-threaded codecs.
#include "fixtures.hpp"
#include <cstddef>
#include <cstdlib>
#include <dacoexec/ans.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/huffman.hpp>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

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
  std::cout << "algorithm,operation,block_bytes,dataset,archive_bytes,allocations,allocated_bytes\n";
  for (auto size : { fixtures::block_sizes.front(), fixtures::mebibyte }) {
    for (auto kind : fixtures::datasets) {
      auto const input = fixtures::make(size, kind);
      auto const probe = [&](char const *name, auto encode, auto decode) {
        allocation_statistics encoding, decoding;
        std::vector<std::byte> archive, restored;
        {
          measurement const guard{ encoding };
          archive = encode();
        }
        {
          measurement const guard{ decoding };
          restored = decode(archive);
        }
        if (restored != input) { throw std::runtime_error("Allocation probe round trip failed"); }
        for (bool decoding_operation : { false, true }) {
          auto const statistics = decoding_operation ? decoding : encoding;
          std::cout << name << ',' << (decoding_operation ? "decode" : "encode") << ',' << size << ','
                    << fixtures::names.at(static_cast<std::size_t>(kind)) << ',' << archive.size() << ','
                    << statistics.calls_ << ',' << statistics.bytes_ << '\n';
        }
      };
      using coder_type = dacoexec::arithmetic::entropy_coder;
      constexpr std::array configurations{ std::pair{ "static_range", coder_type::static_range },
        std::pair{ "adaptive_range", coder_type::adaptive_range },
        std::pair{ "binary", coder_type::binary },
        std::pair{ "context_binary", coder_type::context_binary } };
      for (auto const &[name, coder] : configurations) {
        probe(
          name,
          [&] { return dacoexec::arithmetic::compress(input, { .block_size_ = size, .coder_ = coder }); },
          [](auto const &archive) { return dacoexec::arithmetic::decompress(archive); });
      }
      for (auto coder : { dacoexec::ans::entropy_coder::rans, dacoexec::ans::entropy_coder::tans }) {
        probe(
          coder == dacoexec::ans::entropy_coder::rans ? "rans" : "tans",
          [&] { return dacoexec::ans::compress(input, { .block_size_ = size, .coder_ = coder }); },
          [](auto const &archive) { return dacoexec::ans::decompress(archive); });
      }
      probe(
        "huffman",
        [&] { return dacoexec::huffman::compress(input, { .block_size_ = size }); },
        [](auto const &archive) { return dacoexec::huffman::decompress(archive); });
    }
  }
  return 0;
} catch (std::exception const &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
