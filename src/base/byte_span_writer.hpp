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
 * byte_span_writer.hpp - bounded append-only cursor over a cubbase::span.
 * append() is the only mutator; it bounds-checks before memcpy, returns
 * false on overflow, and never advances the cursor without copying.
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

      /* Returns false on capacity overrun; cursor unchanged. n == 0 is a no-op success. */
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
