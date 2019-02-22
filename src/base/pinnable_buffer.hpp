/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * pinnable_buffer.hpp
 */

#ifndef _PINNABLE_BUFFER_HPP_
#define _PINNABLE_BUFFER_HPP_

#ident "$Id$"

#include "dbtype.h"
#include "pinning.hpp"
#include <atomic>
#include <vector>

/*
 * This should serve as storage for packing / unpacking objects
 * This is not intended to be used as character stream, but as bulk operations: users of it
 * reserve / allocate parts of it; there are objects which deal of byte level operations (see : packer)
 */
namespace cubmem
{

  class pinnable_buffer : public cubbase::pinnable
  {
    public:
      pinnable_buffer ()
      {
	m_storage = NULL;
	m_end_ptr = NULL;
      };

      pinnable_buffer (char *ptr, const size_t buf_size)
      {
	init (ptr, buf_size, NULL);
      };

      ~pinnable_buffer ()
      {
	assert (get_pin_count () == 0);
      };

      char *get_buffer (void)
      {
	return m_storage;
      };

      size_t get_buffer_size (void)
      {
	return m_end_ptr - m_storage;
      };

      int init (char *ptr, const size_t buf_size, cubbase::pinner *referencer);

    protected:

      /* start of allocated memory */
      char *m_storage;
      /* end of allocated memory */
      char *m_end_ptr;
  };

} /* namespace mem */

#endif /* _PINNABLE_BUFFER_HPP_ */
