#ifndef DACOEXEC_DETAIL_ERROR_HPP
#define DACOEXEC_DETAIL_ERROR_HPP

#include <stdexcept>

namespace dacoexec {
class format_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
class stream_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
}// namespace dacoexec
#endif // DACOEXEC_DETAIL_ERROR_HPP
