/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * byte_span_writer.hpp - bounded append-only cursor over a cubbase::span
 *
 * Invariant: 0 <= written() <= capacity(). append() is the only mutator;
 * it bounds-checks before memcpy and refuses to write past the end. There
 * is no way to advance the cursor without copying, and no way to copy
 * without also advancing the cursor — so "wrote past the end" is
 * unrepresentable.
 *
 * Error reporting: append() returns bool. Callers emit context-rich
 * errors (er_set / oos_error / etc.) — the writer stays free of any
 * specific error vocabulary.
 */

#ifndef _BYTE_SPAN_WRITER_HPP_
#define _BYTE_SPAN_WRITER_HPP_

#ident "$Id$"

#include "span.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace cubbase
{
  class byte_span_writer
  {
    public:
      explicit byte_span_writer (span<char> dest) noexcept
	: _dest (dest), _written (0)
      {
      }

      std::size_t written () const noexcept
      {
	return _written;
      }

      std::size_t capacity () const noexcept
      {
	return _dest.size ();
      }

      std::size_t remaining () const noexcept
      {
	return _dest.size () - _written;
      }

      bool full () const noexcept
      {
	return _written == _dest.size ();
      }

      /* Append n bytes from src. Returns false if the write would overrun
       * capacity; cursor is unchanged and no bytes are written in that case.
       * n == 0 is always legal (no-op success), even when the writer is full. */
      bool append (const char *src, std::size_t n) noexcept
      {
	assert (n == 0 || src != nullptr);
	if (n > remaining ())
	  {
	    return false;
	  }
	if (n > 0)
	  {
	    std::memcpy (_dest.data () + _written, src, n);
	    _written += n;
	  }
	return true;
      }

    private:
      span<char> _dest;
      std::size_t _written;
  };
}

#endif /* _BYTE_SPAN_WRITER_HPP_ */
