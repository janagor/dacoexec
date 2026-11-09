#include <dacoexec/error.hpp>
#include <stdexcept>
#include <type_traits>
static_assert(std::is_base_of_v<std::runtime_error, dacoexec::format_error>);
static_assert(std::is_base_of_v<std::runtime_error, dacoexec::stream_error>);
static_assert(std::is_base_of_v<std::runtime_error, dacoexec::huffman::decode_error>);
static_assert(std::is_base_of_v<std::runtime_error, dacoexec::ans::decode_error>);
