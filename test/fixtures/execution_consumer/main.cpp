#include <dacoexec/huffman/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

auto main() -> int
{
  exec::static_thread_pool pool{ 1 };
  std::vector<std::byte> input;
  std::vector<std::byte> output;
  dacoexec::io::iterator_sink sink(std::back_inserter(output));
  auto sender = dacoexec::huffman::encode_parallel_on(pool.get_scheduler(), std::move(input), sink);
  return stdexec::sync_wait(std::move(sender)).has_value() ? 0 : 1;
}
