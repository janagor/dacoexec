//! \file
//! Lazy positive integer mapping adaptors.
#ifndef DACOEXEC_VIEWS_POSITIVE_HPP
#define DACOEXEC_VIEWS_POSITIVE_HPP

#include <dacoexec/detail/integer_coding/positive.hpp>
#include <dacoexec/detail/views/integer_mapping.hpp>

#include <cstdint>

namespace dacoexec::detail::views::encode {

[[nodiscard]] constexpr auto positive()
{ return integer_mapping_closure<std::uint64_t, integer_coding::to_positive>{}; }

}// namespace dacoexec::detail::views::encode

namespace dacoexec::detail::views::decode {

[[nodiscard]] constexpr auto positive()
{ return integer_mapping_closure<std::uint64_t, integer_coding::from_positive>{}; }

}// namespace dacoexec::detail::views::decode
#endif// DACOEXEC_VIEWS_POSITIVE_HPP
