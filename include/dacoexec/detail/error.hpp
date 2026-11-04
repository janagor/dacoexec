//! \file
//! Public archive-format and output-stream exceptions.

#ifndef DACOEXEC_DETAIL_ERROR_HPP
#define DACOEXEC_DETAIL_ERROR_HPP

#include <stdexcept>

namespace dacoexec {
/**
 * Malformed DHF1 framing, invalid frame metadata, or a stream output-limit violation.
 *
 * The diagnostic is supplied through the inherited std::runtime_error constructors.
 */
class format_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
/**
 * Stream or file I/O failure, including errors reported by io::ostream_sink.
 *
 * The diagnostic is supplied through the inherited std::runtime_error constructors.
 */
class stream_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
}// namespace dacoexec
#endif // DACOEXEC_DETAIL_ERROR_HPP
