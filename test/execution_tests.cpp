#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <dacoexec/execution/ordered_parallel.hpp>
#include <dacoexec/huffman/execution.hpp>
#include <dacoexec/huffman/format.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/io/output_sink.hpp>
#include <dacoexec/views/blocks.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <ios>
#include <iterator>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace huffman = dacoexec::huffman;
using bytes = std::vector<std::byte>;
constexpr std::size_t block_size = 256;
constexpr std::size_t batch_size = dacoexec::execution::default_max_in_flight;
constexpr std::array worker_counts{ 1U, 2U, 4U, 8U };
constexpr std::array block_counts{ std::size_t{ 0 },
  std::size_t{ 1 },
  batch_size,
  batch_size + 1,
  (batch_size * 2) + 1 };
constexpr std::uint32_t random_seed = 1729;
enum class input_kind : std::uint8_t { random, single_symbol, repeated_pattern };
constexpr std::array input_kinds{ input_kind::random, input_kind::single_symbol, input_kind::repeated_pattern };
constexpr std::array input_pattern{ std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } };

constexpr int value_completion = 0;
constexpr int stopped_completion = 1;
constexpr int error_completion = 2;
constexpr std::size_t completion_stress_items = 4096;
constexpr std::size_t completion_stress_iterations = 8;
constexpr std::size_t destructive_iterations = 64;
constexpr auto synchronization_timeout = std::chrono::seconds{ 10 };

class timed_gate
{
public:
  explicit timed_gate(std::size_t count) : remaining_(count) {}
  auto arrive() -> void
  {
    std::scoped_lock const lock(mutex_);
    if (remaining_ != 0) { --remaining_; }
    condition_.notify_all();
  }
  [[nodiscard]] auto wait() -> bool
  {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, synchronization_timeout, [this]() -> bool { return remaining_ == 0; });
  }

private:
  std::mutex mutex_;
  std::condition_variable condition_;
  std::size_t remaining_;
};
using erased_operation = std::unique_ptr<void, void (*)(void *)>;
struct destructive_receiver
{
  using receiver_concept = stdexec::receiver_tag;
  erased_operation *operation_;
  std::shared_ptr<std::promise<int>> promise_;
  std::atomic<std::size_t> *completions_;
  stdexec::inplace_stop_token token_;
  [[nodiscard]] auto get_env() const noexcept -> stdexec::prop<stdexec::get_stop_token_t, stdexec::inplace_stop_token>
  { return { stdexec::get_stop_token, token_ }; }
  auto complete(int outcome) const noexcept -> void
  {
    auto *operation = operation_;
    auto promise = promise_;
    auto *completions = completions_;
    completions->fetch_add(1);
    operation->reset();// destroys this receiver, its parent and child states
    promise->set_value(outcome);
  }
  template<class T> auto set_value([[maybe_unused]] T &&value) noexcept -> void { complete(value_completion); }
  auto set_error([[maybe_unused]] std::exception_ptr const &error) const noexcept -> void
  { complete(error_completion); }
  auto set_stopped() const noexcept -> void { complete(stopped_completion); }
};
template<class Sender>
auto start_destructive(Sender sender, destructive_receiver receiver, erased_operation &storage) -> void
{
  using operation_type = stdexec::connect_result_t<Sender, destructive_receiver>;
  // Immovable operation state is constructed directly in type-erased owning storage.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto *operation = new operation_type(stdexec::connect(std::move(sender), std::move(receiver)));
  // Immovable operation state is constructed directly in type-erased owning storage.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  storage = erased_operation(operation, [](void *pointer) -> void { delete static_cast<operation_type *>(pointer); });
  stdexec::start(*operation);
}

struct vector_sink
{
  bytes bytes_;
  auto write(std::span<std::byte const> data) -> void { bytes_.insert(bytes_.end(), data.begin(), data.end()); }
};
struct completion_receiver
{
  using receiver_concept = stdexec::receiver_tag;
  std::promise<int> *promise_;
  stdexec::inplace_stop_token token_;
  [[nodiscard]] auto get_env() const noexcept -> stdexec::prop<stdexec::get_stop_token_t, stdexec::inplace_stop_token>
  { return { stdexec::get_stop_token, token_ }; }
  template<class T> auto set_value([[maybe_unused]] T &&value) noexcept -> void { promise_->set_value(0); }
  auto set_error(std::exception_ptr error) const noexcept -> void { promise_->set_exception(std::move(error)); }
  auto set_stopped() const noexcept -> void { promise_->set_value(1); }
};
constexpr std::size_t environment_marker = 73;
struct marker_query
{
  template<class Environment>
  [[nodiscard]] auto operator()(Environment const &environment) const noexcept -> decltype(environment.query(*this))
  { return environment.query(*this); }
  [[nodiscard]] static constexpr auto query([[maybe_unused]] stdexec::forwarding_query_t query) noexcept -> bool
  { return true; }
};
struct marker_receiver : completion_receiver
{
  struct environment
  {
    stdexec::inplace_stop_token token_;
    [[nodiscard]] auto query([[maybe_unused]] stdexec::get_stop_token_t query) const noexcept
      -> stdexec::inplace_stop_token
    { return token_; }
    [[nodiscard]] static auto query([[maybe_unused]] marker_query query) noexcept -> std::size_t
    { return environment_marker; }
  };
  // This test deliberately replaces the inherited sender protocol customization.
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] auto get_env() const noexcept -> environment { return { token_ }; }
};
struct marker_observer
{
  std::atomic<std::size_t> *observed_;
  auto operator()(std::size_t marker) const -> void { observed_->store(marker); }
};
struct environment_scheduler
{
  using scheduler_concept = stdexec::scheduler_tag;
  std::atomic<std::size_t> *observed_;
  [[nodiscard]] auto schedule() const
    -> decltype(stdexec::then(stdexec::read_env(marker_query{}), marker_observer{ observed_ }))
  { return stdexec::then(stdexec::read_env(marker_query{}), marker_observer{ observed_ }); }
  auto operator==(environment_scheduler const &) const noexcept -> bool = default;
};

struct integer_source
{
  using input_type = std::size_t;
  using result_type = std::size_t;
  std::size_t count_;
  std::vector<std::size_t> *committed_;
  std::atomic<std::size_t> *acquired_;
  std::size_t cursor_ = 0;
  auto initialize() -> void {}
  auto next() -> std::optional<std::size_t>
  {
    if (cursor_ == count_) { return std::nullopt; }
    acquired_->fetch_add(1);
    return cursor_++;
  }
  auto commit(std::size_t value) const -> void { committed_->push_back(value); }
  [[nodiscard]] auto finish() const -> std::size_t { return committed_->size(); }
};
template<class Scheduler> struct throwing_scheduler
{
  using scheduler_concept = stdexec::scheduler_tag;
  Scheduler scheduler_;
  std::size_t *submissions_;
  std::size_t fail_at_;
  [[nodiscard]] auto schedule() const -> stdexec::schedule_result_t<Scheduler>
  {
    if ((*submissions_)++ == fail_at_) { throw std::runtime_error("submission failed"); }
    return stdexec::schedule(scheduler_);
  }
  auto operator==(throwing_scheduler const &) const noexcept -> bool = default;
};
struct schedule_failure
{
  auto operator()() const -> void { throw std::runtime_error("scheduler failed"); }
};
template<class Scheduler> struct error_scheduler
{
  using scheduler_concept = stdexec::scheduler_tag;
  Scheduler scheduler_;
  [[nodiscard]] auto schedule() const -> decltype(stdexec::then(stdexec::schedule(scheduler_), schedule_failure{}))
  { return stdexec::then(stdexec::schedule(scheduler_), schedule_failure{}); }
  auto operator==(error_scheduler const &) const noexcept -> bool = default;
};
}// namespace

TEST_CASE("Parallel DHF1 matches sequential output and round trips", "[execution]")
{
  constexpr std::array tail_sizes{ std::size_t{ 0 }, std::size_t{ 1 } };
  // Fixed seed makes failures reproducible.
  // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  std::mt19937 random(random_seed);
  for (auto const workers : worker_counts) {
    exec::static_thread_pool pool{ workers };
    for (auto const count : block_counts) {
      for (auto const tail : tail_sizes) {
        for (auto const kind : input_kinds) {
          bytes input((count * block_size) + tail, std::byte{ 'x' });
          if (kind == input_kind::random) {
            for (auto &byte : input) { byte = static_cast<std::byte>(random()); }
          } else if (kind == input_kind::repeated_pattern) {
            for (std::size_t index = 0; index < input.size(); ++index) {
              input.at(index) = input_pattern.at(index % input_pattern.size());
            }
          }
          vector_sink sequential;
          auto const expected = huffman::compress_stream(input, sequential, { block_size });
          vector_sink parallel;
          auto sender = huffman::encode_parallel_on(
            pool.get_scheduler(), input, parallel, { .block_size_ = block_size, .max_in_flight_ = batch_size });
          REQUIRE(parallel.bytes_.empty());
          auto result = stdexec::sync_wait(std::move(sender));
          REQUIRE(result.has_value());
          REQUIRE(parallel.bytes_ == sequential.bytes_);
          if (!result) {
            FAIL("Parallel encoding completed stopped");
            return;
          }
          auto const &[actual] = *result;
          REQUIRE(actual.input_bytes_ == expected.input_bytes_);
          REQUIRE(actual.output_bytes_ == expected.output_bytes_);
          REQUIRE(actual.frames_ == expected.frames_);
          vector_sink decoded;
          auto const statistics = huffman::decompress_stream(parallel.bytes_, decoded);
          REQUIRE(statistics.output_bytes_ == input.size());
          REQUIRE(decoded.bytes_ == input);
        }
      }
    }
  }
}

TEST_CASE("Parallel sender owns move-only input and supports single-pass input", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  vector_sink output;
  auto sender = huffman::encode_parallel_on(
    pool.get_scheduler(), std::ranges::owning_view{ bytes(block_size, std::byte{ 'a' }) }, output);
  auto const completed = stdexec::sync_wait(std::move(sender));
  REQUIRE(completed.has_value());

  std::size_t reads = 0;
  auto source =
    std::views::iota(std::size_t{ 0 }, block_size) | std::views::transform([&reads](std::size_t value) -> std::byte {
      ++reads;
      return static_cast<std::byte>(value);
    });
  vector_sink second;
  auto lazy = huffman::encode_parallel_on(pool.get_scheduler(), source, second);
  REQUIRE(reads == 0);
  auto const second_completed = stdexec::sync_wait(std::move(lazy));
  REQUIRE(second_completed.has_value());
  REQUIRE(reads == block_size);
}

TEST_CASE("Ordered batches bound acquisition and tolerate reversed completion", "[execution]")
{
  exec::static_thread_pool pool{ batch_size };
  std::atomic<std::size_t> acquired{ 0 };
  std::vector<std::size_t> committed;
  std::latch entered(batch_size);
  std::latch release(1);
  std::latch later_finished(batch_size - 1);
  auto process = [&](std::size_t index) -> std::size_t {
    if (index < batch_size) {
      entered.count_down();
      release.wait();
      if (index == 0) {
        later_finished.wait();
      } else {
        later_finished.count_down();
      }
    }
    return index;
  };
  constexpr auto count = (batch_size * 2) + 1;
  auto sender = dacoexec::execution::ordered_parallel_on(pool.get_scheduler(),
    integer_source{ .count_ = count, .committed_ = &committed, .acquired_ = &acquired },
    process,
    batch_size);
  auto future = std::async(std::launch::async,
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    [sender = std::move(sender)]() mutable -> void { static_cast<void>(stdexec::sync_wait(std::move(sender))); });
  entered.wait();
  auto const first_batch_reads = acquired.load();
  release.count_down();
  future.get();
  REQUIRE(first_batch_reads == batch_size);
  std::vector<std::size_t> expected(count);
  std::ranges::copy(std::views::iota(std::size_t{ 0 }, count), expected.begin());
  REQUIRE(committed == expected);
}

TEST_CASE("Worker failures drain the batch and select its lowest failing index", "[execution]")
{
  exec::static_thread_pool pool{ batch_size };
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> finished{ 0 };
  std::vector<std::size_t> committed;
  std::latch entered(batch_size);
  auto process = [&](std::size_t index) -> std::size_t {
    entered.count_down();
    entered.wait();
    finished.fetch_add(1);
    if (index == 0) { throw std::runtime_error("first block"); }
    throw std::runtime_error("later block");
  };
  auto sender = dacoexec::execution::ordered_parallel_on(pool.get_scheduler(),
    integer_source{ .count_ = batch_size + 1, .committed_ = &committed, .acquired_ = &acquired },
    process,
    batch_size);
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  REQUIRE_THROWS_WITH(stdexec::sync_wait(std::move(sender)), "first block");
  REQUIRE(finished.load() == batch_size);
  REQUIRE(acquired.load() == batch_size);
  REQUIRE(committed.empty());
}

TEST_CASE("Cancellation before start has no observable input or output", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  stdexec::inplace_stop_source stop;
  stop.request_stop();
  vector_sink sink;
  std::size_t reads = 0;
  auto input =
    std::views::iota(std::size_t{ 0 }, block_size) | std::views::transform([&reads](std::size_t value) -> std::byte {
      ++reads;
      return static_cast<std::byte>(value);
    });
  std::promise<int> promise;
  auto future = promise.get_future();
  auto operation = stdexec::connect(huffman::encode_parallel_on(pool.get_scheduler(), input, sink),
    completion_receiver{ .promise_ = &promise, .token_ = stop.get_token() });
  stdexec::start(operation);
  REQUIRE(future.get() == 1);
  REQUIRE(reads == 0);
  REQUIRE(sink.bytes_.empty());
}

TEST_CASE("Cancellation drains active workers without committing their batch", "[execution]")
{
  exec::static_thread_pool pool{ batch_size };
  stdexec::inplace_stop_source stop;
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> finished{ 0 };
  std::vector<std::size_t> committed;
  std::latch entered(batch_size);
  std::latch release(1);
  auto process = [&](std::size_t index) -> std::size_t {
    entered.count_down();
    release.wait();
    finished.fetch_add(1);
    return index;
  };
  auto sender = dacoexec::execution::ordered_parallel_on(pool.get_scheduler(),
    integer_source{ .count_ = batch_size + 1, .committed_ = &committed, .acquired_ = &acquired },
    process,
    batch_size);
  std::promise<int> promise;
  auto future = promise.get_future();
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  auto operation =
    stdexec::connect(std::move(sender), completion_receiver{ .promise_ = &promise, .token_ = stop.get_token() });
  stdexec::start(operation);
  entered.wait();
  stop.request_stop();
  release.count_down();
  REQUIRE(future.get() == 1);
  REQUIRE(finished.load() == batch_size);
  REQUIRE(acquired.load() == batch_size);
  REQUIRE(committed.empty());
}

TEST_CASE("Invalid parallel options fail lazily", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  bytes input(block_size);
  for (auto options : { huffman::parallel_options{ .block_size_ = 0 },
         huffman::parallel_options{ .block_size_ = huffman::max_frame_size + 1 },
         huffman::parallel_options{ .max_in_flight_ = 0 } }) {
    vector_sink sink;
    auto sender = huffman::encode_parallel_on(pool.get_scheduler(), input, sink, options);
    REQUIRE(sink.bytes_.empty());
    REQUIRE_THROWS_AS(stdexec::sync_wait(std::move(sender)), std::invalid_argument);
    REQUIRE(sink.bytes_.empty());
  }
}

TEST_CASE("Source and sink exceptions end parallel encoding", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  auto source = std::views::iota(std::size_t{ 0 }, block_size * batch_size)
                | std::views::transform([](std::size_t index) -> std::byte {
                    if (index == block_size) { throw std::runtime_error("source failed"); }
                    return std::byte{ 'a' };
                  });
  vector_sink sink;
  auto sender = huffman::encode_parallel_on(
    pool.get_scheduler(), source, sink, { .block_size_ = block_size, .max_in_flight_ = batch_size });
  REQUIRE_THROWS_WITH(stdexec::sync_wait(std::move(sender)), "source failed");
  REQUIRE(sink.bytes_.size() == huffman::archive_header_size);

  struct failing_sink
  {
    std::size_t writes_ = 0;
    auto write([[maybe_unused]] std::span<std::byte const> data) -> void
    {
      ++writes_;
      if (writes_ > 1) { throw std::runtime_error("sink failed"); }
    }
  } failing;
  auto output_failure = huffman::encode_parallel_on(pool.get_scheduler(), bytes(block_size), failing);
  REQUIRE_THROWS_WITH(stdexec::sync_wait(std::move(output_failure)), "sink failed");
  constexpr std::size_t header_and_failed_frame = 2;
  REQUIRE(failing.writes_ == header_and_failed_frame);
}

TEST_CASE("Submission failures retain and drain previously started operations", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  std::size_t submissions = 0;
  constexpr std::size_t failing_submission = 1;
  throwing_scheduler const scheduler{
    .scheduler_ = pool.get_scheduler(), .submissions_ = &submissions, .fail_at_ = failing_submission
  };
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> finished{ 0 };
  std::vector<std::size_t> committed;
  std::latch entered(1);
  std::latch release(1);
  auto process = [&](std::size_t index) -> std::size_t {
    entered.count_down();
    release.wait();
    finished.fetch_add(1);
    return index;
  };
  auto sender = dacoexec::execution::ordered_parallel_on(scheduler,
    integer_source{ .count_ = batch_size, .committed_ = &committed, .acquired_ = &acquired },
    process,
    batch_size);
  stdexec::inplace_stop_source const stop;
  std::promise<int> promise;
  auto future = promise.get_future();
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  auto operation =
    stdexec::connect(std::move(sender), completion_receiver{ .promise_ = &promise, .token_ = stop.get_token() });
  stdexec::start(operation);
  entered.wait();
  auto const before_release = future.wait_for(std::chrono::seconds::zero());
  release.count_down();
  REQUIRE_THROWS_WITH(future.get(), "submission failed");
  REQUIRE(before_release == std::future_status::timeout);
  REQUIRE(finished.load() == failing_submission);
  REQUIRE(committed.empty());
}

TEST_CASE("Huffman block staging does not read into an active batch's successor", "[execution]")
{
  exec::static_thread_pool pool{ batch_size };
  std::atomic<std::size_t> reads{ 0 };
  constexpr auto input_size = block_size * (batch_size + 1);
  auto input =
    std::views::iota(std::size_t{ 0 }, input_size) | std::views::transform([&reads](std::size_t index) -> std::byte {
      reads.fetch_add(1);
      return static_cast<std::byte>(index);
    });
  vector_sink output;
  std::latch entered(batch_size);
  std::latch release(1);
  auto process = [&](dacoexec::byte_block block) -> huffman::encoded_frame {
    if (block.sequence_ < batch_size) {
      entered.count_down();
      release.wait();
    }
    return huffman::encode_frame(std::move(block));
  };
  using source_type = huffman::detail::parallel_source<decltype(input), vector_sink>;
  auto sender = dacoexec::execution::ordered_parallel_on(pool.get_scheduler(),
    source_type{ std::move(input), &output, { .block_size_ = block_size, .max_in_flight_ = batch_size } },
    process,
    batch_size);
  auto future = std::async(std::launch::async,
    [sender = std::move(sender)]() mutable -> void { static_cast<void>(stdexec::sync_wait(std::move(sender))); });
  entered.wait();
  auto const active_batch_reads = reads.load();
  release.count_down();
  future.get();
  REQUIRE(active_batch_reads == block_size * batch_size);
  REQUIRE(reads.load() == input_size);
}

TEST_CASE("Synchronous scheduling advances batches without recursive stack growth", "[execution]")
{
  constexpr std::size_t many_batches = 10000;
  std::atomic<std::size_t> acquired{ 0 };
  std::vector<std::size_t> committed;
  auto sender = dacoexec::execution::ordered_parallel_on(
    stdexec::inline_scheduler{},
    integer_source{ .count_ = many_batches, .committed_ = &committed, .acquired_ = &acquired },
    [](std::size_t value) -> std::size_t { return value; },
    1);
  // This sender API consumes an rvalue, including trivially copyable specializations.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  auto const completed = stdexec::sync_wait(std::move(sender));
  REQUIRE(completed.has_value());
  REQUIRE(committed.size() == many_batches);
}

TEST_CASE("Parallel compression consumes an actual single-pass stream range", "[execution]")
{
  std::string const text(block_size * (batch_size + 1), 'z');
  std::istringstream stream(text);
  stream >> std::noskipws;
  exec::static_thread_pool pool{ 1 };
  vector_sink output;
  auto sender = huffman::encode_parallel_on(pool.get_scheduler(),
    std::ranges::istream_view<char>(stream),
    output,
    { .block_size_ = block_size, .max_in_flight_ = batch_size });
  auto const completed = stdexec::sync_wait(std::move(sender));
  REQUIRE(completed.has_value());
  vector_sink decoded;
  auto const statistics = huffman::decompress_stream(output.bytes_, decoded);
  REQUIRE(statistics.output_bytes_ == text.size());
  REQUIRE(std::ranges::all_of(decoded.bytes_, [](std::byte byte) -> bool { return byte == std::byte{ 'z' }; }));
}

TEST_CASE("Cancellation between batches prevents further acquisition", "[execution]")
{
  stdexec::inplace_stop_source stop;
  struct stopping_source : integer_source
  {
    stdexec::inplace_stop_source *stop_;
    // This test deliberately replaces the inherited sender protocol customization.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    auto commit(std::size_t value) -> void
    {
      integer_source::commit(value);
      if (committed_->size() == batch_size) { stop_->request_stop(); }
    }
  };
  std::atomic<std::size_t> acquired{ 0 };
  std::vector<std::size_t> committed;
  auto sender = dacoexec::execution::ordered_parallel_on(
    stdexec::inline_scheduler{},
    stopping_source{
      integer_source{ .count_ = batch_size + 1, .committed_ = &committed, .acquired_ = &acquired }, &stop },
    [](std::size_t value) -> std::size_t { return value; },
    batch_size);
  std::promise<int> promise;
  auto future = promise.get_future();
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  auto operation =
    stdexec::connect(std::move(sender), completion_receiver{ .promise_ = &promise, .token_ = stop.get_token() });
  stdexec::start(operation);
  REQUIRE(future.get() == 1);
  REQUIRE(acquired.load() == batch_size);
  REQUIRE(committed.size() == batch_size);
}

TEST_CASE("Failed batches leave only previously committed batches", "[execution]")
{
  constexpr std::array failing_indices{ std::size_t{ 0 }, batch_size - 1, batch_size + 1 };
  for (auto const failing_index : failing_indices) {
    std::atomic<std::size_t> acquired{ 0 };
    std::vector<std::size_t> committed;
    auto sender = dacoexec::execution::ordered_parallel_on(
      stdexec::inline_scheduler{},
      integer_source{ .count_ = batch_size * 3, .committed_ = &committed, .acquired_ = &acquired },
      [failing_index](std::size_t index) -> std::size_t {
        if (index == failing_index) { throw std::runtime_error("encoding failed"); }
        return index;
      },
      batch_size);
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    REQUIRE_THROWS_WITH(stdexec::sync_wait(std::move(sender)), "encoding failed");
    auto const successful_batches = failing_index / batch_size;
    REQUIRE(committed.size() == successful_batches * batch_size);
    REQUIRE(acquired.load() == (successful_batches + 1) * batch_size);
  }
}

TEST_CASE("Encoded results are released before acquiring the next batch", "[execution]")
{
  using result = std::shared_ptr<std::size_t>;
  std::vector<std::weak_ptr<std::size_t>> previous_results;
  bool released_before_next_batch = true;
  struct source
  {
    using input_type = std::size_t;
    using result_type = result;
    std::vector<std::weak_ptr<std::size_t>> *previous_;
    bool *released_;
    std::size_t cursor_ = 0;
    auto initialize() -> void {}
    auto next() -> std::optional<std::size_t>
    {
      if (cursor_ == batch_size) {
        *released_ = std::ranges::all_of(*previous_, [](auto const &weak) -> bool { return weak.expired(); });
      }
      if (cursor_ == batch_size + 1) { return std::nullopt; }
      return cursor_++;
    }
    auto commit([[maybe_unused]] result const &value) const -> void {}
    [[nodiscard]] auto finish() const -> bool { return *released_; }
  };
  auto sender = dacoexec::execution::ordered_parallel_on(
    stdexec::inline_scheduler{},
    source{ .previous_ = &previous_results, .released_ = &released_before_next_batch },
    [&previous_results](std::size_t value) -> result {
      auto encoded = std::make_shared<std::size_t>(value);
      if (value < batch_size) { previous_results.push_back(encoded); }
      return encoded;
    },
    batch_size);
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  auto completed = stdexec::sync_wait(std::move(sender));
  REQUIRE(completed.has_value());
  if (!completed) {
    FAIL("Result lifetime test completed stopped");
    return;
  }
  auto const &[released] = *completed;
  REQUIRE(released);
}

TEST_CASE("Scheduler error completions are recorded before joining the batch", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> processed{ 0 };
  std::vector<std::size_t> committed;
  auto sender = dacoexec::execution::ordered_parallel_on(
    error_scheduler{ pool.get_scheduler() },
    integer_source{ .count_ = batch_size + 1, .committed_ = &committed, .acquired_ = &acquired },
    [&processed](std::size_t value) -> std::size_t {
      processed.fetch_add(1);
      return value;
    },
    batch_size);
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  REQUIRE_THROWS_WITH(stdexec::sync_wait(std::move(sender)), "scheduler failed");
  REQUIRE(acquired.load() == batch_size);
  REQUIRE(processed.load() == 0);
  REQUIRE(committed.empty());
}

TEST_CASE("Concurrent short completions survive destructive terminal receivers", "[execution]")
{
  for (auto const workers : worker_counts) {
    exec::static_thread_pool pool{ workers };
    for (std::size_t iteration = 0; iteration < completion_stress_iterations; ++iteration) {
      std::atomic<std::size_t> acquired{ 0 };
      std::atomic<std::size_t> completions{ 0 };
      std::vector<std::size_t> committed;
      stdexec::inplace_stop_source const stop;
      auto promise = std::make_shared<std::promise<int>>();
      auto future = promise->get_future();
      erased_operation storage(nullptr, []([[maybe_unused]] void *pointer) -> void {});
      auto sender = dacoexec::execution::ordered_parallel_on(
        pool.get_scheduler(),
        integer_source{ .count_ = completion_stress_items, .committed_ = &committed, .acquired_ = &acquired },
        [](std::size_t value) -> std::size_t { return value; },
        batch_size);
      // Senders are consumed as rvalues even when this specialization is trivially copyable.
      // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
      start_destructive(std::move(sender),
        { .operation_ = &storage, .promise_ = promise, .completions_ = &completions, .token_ = stop.get_token() },
        storage);
      REQUIRE(future.get() == value_completion);
      REQUIRE(completions.load() == 1);
      REQUIRE(storage == nullptr);
      REQUIRE(committed.size() == completion_stress_items);
    }
  }
}

TEST_CASE("Inline terminal completions can destroy their owning operation", "[execution]")
{
  for (auto const outcome : { value_completion, stopped_completion, error_completion }) {
    std::atomic<std::size_t> acquired{ 0 };
    std::atomic<std::size_t> completions{ 0 };
    std::vector<std::size_t> committed;
    stdexec::inplace_stop_source stop;
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();
    erased_operation storage(nullptr, []([[maybe_unused]] void *pointer) -> void {});
    if (outcome == stopped_completion) { stop.request_stop(); }
    auto sender = dacoexec::execution::ordered_parallel_on(
      stdexec::inline_scheduler{},
      integer_source{ .count_ = batch_size, .committed_ = &committed, .acquired_ = &acquired },
      [outcome](std::size_t value) -> std::size_t {
        if (outcome == error_completion) { throw std::runtime_error("destructive error"); }
        return value;
      },
      batch_size);
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    start_destructive(std::move(sender),
      { .operation_ = &storage, .promise_ = promise, .completions_ = &completions, .token_ = stop.get_token() },
      storage);
    REQUIRE(future.get() == outcome);
    REQUIRE(completions.load() == 1);
    REQUIRE(storage == nullptr);
  }
}

TEST_CASE("Recorded failures win when external cancellation races active workers", "[execution]")
{
  exec::static_thread_pool pool{ batch_size };
  for (std::size_t iteration = 0; iteration < destructive_iterations; ++iteration) {
    std::atomic<std::size_t> acquired{ 0 };
    std::atomic<std::size_t> completions{ 0 };
    std::atomic<bool> timed_out{ false };
    std::vector<std::size_t> committed;
    stdexec::inplace_stop_source stop;
    timed_gate entered(batch_size);
    timed_gate release(1);
    auto process = [&](std::size_t value) -> std::size_t {
      entered.arrive();
      if (!release.wait()) { timed_out.store(true); }
      if (value == 0) { throw std::runtime_error("mixed failure"); }
      return value;
    };
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();
    erased_operation storage(nullptr, []([[maybe_unused]] void *pointer) -> void {});
    auto sender = dacoexec::execution::ordered_parallel_on(pool.get_scheduler(),
      integer_source{ .count_ = batch_size + 1, .committed_ = &committed, .acquired_ = &acquired },
      process,
      batch_size);
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    start_destructive(std::move(sender),
      { .operation_ = &storage, .promise_ = promise, .completions_ = &completions, .token_ = stop.get_token() },
      storage);
    bool const all_started = entered.wait();
    // Release and request stop concurrently to exercise both arrival orders.
    auto cancellation = std::async(std::launch::async, [&stop]() -> void { stop.request_stop(); });
    release.arrive();
    cancellation.get();
    REQUIRE(future.get() == error_completion);
    REQUIRE(all_started);
    REQUIRE_FALSE(timed_out.load());
    REQUIRE(completions.load() == 1);
    REQUIRE(storage == nullptr);
    REQUIRE(committed.empty());
  }
}

TEST_CASE("Queued stopped children can destroy their owning operation", "[execution]")
{
  exec::static_thread_pool pool{ 1 };
  timed_gate entered(1);
  timed_gate release(1);
  std::atomic<bool> timed_out{ false };
  auto blocker = std::async(std::launch::async, [&]() -> void {
    auto work = stdexec::schedule(pool.get_scheduler()) | stdexec::then([&]() -> void {
      entered.arrive();
      if (!release.wait()) { timed_out.store(true); }
    });
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    static_cast<void>(stdexec::sync_wait(std::move(work)));
  });
  bool const blocked = entered.wait();
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> completions{ 0 };
  std::vector<std::size_t> committed;
  stdexec::inplace_stop_source stop;
  auto promise = std::make_shared<std::promise<int>>();
  auto future = promise->get_future();
  erased_operation storage(nullptr, []([[maybe_unused]] void *pointer) -> void {});
  auto sender = dacoexec::execution::ordered_parallel_on(
    pool.get_scheduler(),
    integer_source{ .count_ = batch_size, .committed_ = &committed, .acquired_ = &acquired },
    [](std::size_t value) -> std::size_t { return value; },
    batch_size);
  // Senders are consumed as rvalues even when this specialization is trivially copyable.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  start_destructive(std::move(sender),
    { .operation_ = &storage, .promise_ = promise, .completions_ = &completions, .token_ = stop.get_token() },
    storage);
  stop.request_stop();
  release.arrive();
  blocker.get();
  REQUIRE(future.get() == stopped_completion);
  REQUIRE(blocked);
  REQUIRE_FALSE(timed_out.load());
  REQUIRE(completions.load() == 1);
  REQUIRE(storage == nullptr);
  REQUIRE(committed.empty());
}

TEST_CASE("Child operations preserve forwarding receiver environment queries", "[execution]")
{
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> observed{ 0 };
  std::vector<std::size_t> committed;
  stdexec::inplace_stop_source const stop;
  std::promise<int> promise;
  auto future = promise.get_future();
  auto sender = dacoexec::execution::ordered_parallel_on(
    environment_scheduler{ &observed },
    integer_source{ .count_ = batch_size, .committed_ = &committed, .acquired_ = &acquired },
    [](std::size_t value) -> std::size_t { return value; },
    batch_size);
  auto operation =
    // Senders are consumed as rvalues even when this specialization is trivially copyable.
    // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
    stdexec::connect(
      std::move(sender), marker_receiver{ completion_receiver{ .promise_ = &promise, .token_ = stop.get_token() } });
  stdexec::start(operation);
  REQUIRE(future.get() == 0);
  REQUIRE(observed.load() == environment_marker);
}

TEST_CASE("Independent pipelines share a scheduler without shared codec state", "[execution][hardening]")
{
  constexpr std::size_t pipeline_count = 4;
  constexpr std::size_t source_blocks = 257;
  exec::static_thread_pool pool{ worker_counts.back() };
  std::array<std::future<bytes>, pipeline_count> results;
  for (std::size_t index = 0; index < pipeline_count; ++index) {
    results.at(index) = std::async(std::launch::async, [&, index]() -> bytes {
      bytes input((block_size * source_blocks) + index, static_cast<std::byte>(index));
      bytes reference;
      dacoexec::io::iterator_sink sequential{ std::back_inserter(reference) };
      static_cast<void>(huffman::compress_stream(input, sequential, { block_size }));
      bytes output;
      dacoexec::io::iterator_sink sink{ std::back_inserter(output) };
      auto const result = stdexec::sync_wait(huffman::encode_parallel_on(
        pool.get_scheduler(), input, sink, { .block_size_ = block_size, .max_in_flight_ = batch_size }));
      if (!result || output != reference) { throw std::runtime_error("Concurrent pipeline differs"); }
      return output;
    });
  }
  for (auto &result : results) {
    REQUIRE(result.wait_for(synchronization_timeout) == std::future_status::ready);
    REQUIRE_FALSE(result.get().empty());
  }
}

TEST_CASE("Finish failure delivers one error to a destructive receiver", "[execution][hardening]")
{
  struct failing_finish_source : integer_source
  {
    // Deliberately inject a failure at this source protocol boundary.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    [[nodiscard]] static auto finish() -> std::size_t { throw std::runtime_error("finish failed"); }
  };
  std::atomic<std::size_t> acquired{ 0 };
  std::atomic<std::size_t> completions{ 0 };
  std::vector<std::size_t> committed;
  stdexec::inplace_stop_source const stop;
  auto promise = std::make_shared<std::promise<int>>();
  auto future = promise->get_future();
  erased_operation storage(nullptr, []([[maybe_unused]] void *pointer) -> void {});
  auto sender = dacoexec::execution::ordered_parallel_on(
    stdexec::inline_scheduler{},
    failing_finish_source{ integer_source{ .count_ = batch_size, .committed_ = &committed, .acquired_ = &acquired } },
    [](std::size_t value) -> std::size_t { return value; },
    batch_size);
  // Sender connection consumes an rvalue even for trivially copyable specializations.
  // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg)
  start_destructive(std::move(sender),
    { .operation_ = &storage, .promise_ = promise, .completions_ = &completions, .token_ = stop.get_token() },
    storage);
  REQUIRE(future.wait_for(synchronization_timeout) == std::future_status::ready);
  REQUIRE(future.get() == error_completion);
  REQUIRE(completions.load() == 1);
  REQUIRE(storage == nullptr);
  REQUIRE(committed.size() == batch_size);
}
