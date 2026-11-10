//! \file
//! Lazy zigzag integer mapping adaptors.
#ifndef DACOEXEC_VIEWS_ZIGZAG_HPP
#define DACOEXEC_VIEWS_ZIGZAG_HPP

#include <dacoexec/detail/integer_coding/zigzag.hpp>
#include <dacoexec/detail/views/integer_mapping.hpp>

#include <cstdint>

namespace dacoexec::detail::views::encode {

[[nodiscard]] constexpr auto zigzag() { return integer_mapping_closure<std::int64_t, integer_coding::zigzag_encode>{}; }

}// namespace dacoexec::detail::views::encode

namespace dacoexec::detail::views::decode {

[[nodiscard]] constexpr auto zigzag()
{ return integer_mapping_closure<std::uint64_t, integer_coding::zigzag_decode>{}; }

}// namespace dacoexec::detail::views::decode
#endif// DACOEXEC_VIEWS_ZIGZAG_HPP
