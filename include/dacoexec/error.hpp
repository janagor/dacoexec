//! \file
//! Public archive-format and output-stream exceptions.

#ifndef DACOEXEC_ERROR_HPP
#define DACOEXEC_ERROR_HPP

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

namespace dacoexec::huffman {
/**
 * Malformed Huffman block metadata or payload, including output-bound violations.
 *
 * Diagnostics use the inherited std::runtime_error constructors.
 * @see decompress_block
 */
class decode_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};


}// namespace dacoexec::huffman

namespace dacoexec::ans {

class decode_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

}// namespace dacoexec::ans

#endif// DACOEXEC_ERROR_HPP
