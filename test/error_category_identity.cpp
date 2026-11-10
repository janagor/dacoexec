#include <dacoexec/error.hpp>

auto error_category_from_other_translation_unit() noexcept -> std::error_category const &;

auto error_category_from_other_translation_unit() noexcept -> std::error_category const &
{ return dacoexec::category(); }
