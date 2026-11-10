#include <array>
#include <cstddef>
#include <dacoexec/ans.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/execution.hpp>
#include <dacoexec/huffman.hpp>
#include <exec/static_thread_pool.hpp>
#include <iterator>
#include <sstream>
#include <stdexec/execution.hpp>
#include <type_traits>
#include <utility>
#include <vector>

int main()
{
  exec::static_thread_pool pool{ 2 };
  std::array<std::byte, 8> input{};
  std::vector<std::byte> output;
  auto sender = dacoexec::huffman::compress_parallel_on(
    pool.get_scheduler(), input, std::back_inserter(output), { .block_size_ = 3, .max_in_flight_ = 2 });
  auto result = stdexec::sync_wait(std::move(sender));
  static_assert(
    std::is_same_v<std::remove_cvref_t<decltype(std::get<0>(*result))>, dacoexec::huffman::stream_statistics>);
  if (!result) { return 1; }
  std::vector<std::byte> decoded;
  auto decoded_result = stdexec::sync_wait(dacoexec::huffman::decompress_parallel_on(
    pool.get_scheduler(), output, std::back_inserter(decoded), { .max_in_flight_ = 2 }));
  static_assert(
    std::is_same_v<std::remove_cvref_t<decltype(std::get<0>(*decoded_result))>, dacoexec::huffman::stream_statistics>);
  if (!decoded_result || decoded.size() != input.size()) { return 1; }
  std::ostringstream stream;
  auto ans_sender = dacoexec::ans::compress_parallel_on(pool.get_scheduler(), input, stream);
  auto ans_result = stdexec::sync_wait(std::move(ans_sender));
  static_assert(
    std::is_same_v<std::remove_cvref_t<decltype(std::get<0>(*ans_result))>, dacoexec::ans::stream_statistics>);
  auto archive = stream.str();
  std::vector<std::byte> ans_decoded;
  auto ans_decoded_result = stdexec::sync_wait(
    dacoexec::ans::decompress_parallel_on(pool.get_scheduler(), archive, std::back_inserter(ans_decoded)));
  static_assert(
    std::is_same_v<std::remove_cvref_t<decltype(std::get<0>(*ans_decoded_result))>, dacoexec::ans::stream_statistics>);
  std::vector<std::byte> arithmetic_archive;
  auto arithmetic_result = stdexec::sync_wait(
    dacoexec::arithmetic::compress_parallel_on(pool.get_scheduler(), input, std::back_inserter(arithmetic_archive)));
  std::vector<std::byte> arithmetic_decoded;
  auto arithmetic_decoded_result = stdexec::sync_wait(dacoexec::arithmetic::decompress_parallel_on(
    pool.get_scheduler(), arithmetic_archive, std::back_inserter(arithmetic_decoded)));
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::get<0>(*arithmetic_result))>,
    dacoexec::arithmetic::stream_statistics>);
  return ans_result && ans_decoded_result && ans_decoded.size() == input.size() && arithmetic_result
             && arithmetic_decoded_result && arithmetic_decoded == std::vector<std::byte>(input.begin(), input.end())
           ? 0
           : 1;
}
