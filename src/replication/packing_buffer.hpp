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
 * packing_buffer.hpp
 */

#ident "$Id$"

#ifndef _PACKING_BUFFER_HPP_
#define _PACKING_BUFFER_HPP_

#include <atomic>
#include <vector>
#include "dbtype.h"
#include "common_utils.hpp"


class pinnable;
class replication_stream;

/*
 * This should serve as storage for packing / unpacking objects
 * This is not intended to be used as character stream, but as bulk operations: users of it
 * reserve / allocate parts of it; there are objects which deal of byte level operations (see : packer)
 */
class packing_buffer : public pinnable
{
public:
  packing_buffer () { storage = NULL; };
  packing_buffer (BUFFER_UNIT *ptr, const size_t buf_size) { init (ptr, buf_size, NULL); };

  ~packing_buffer () { assert (get_pin_count () == 0); };

  BUFFER_UNIT * get_buffer (void) { return storage; };

  size_t get_buffer_size (void) { return end_ptr - storage; };

  int init (BUFFER_UNIT *ptr, const size_t buf_size, pinner *referencer);
    
protected:

  /* start of allocated memory */
  BUFFER_UNIT *storage;
  /* end of allocated memory */
  BUFFER_UNIT *end_ptr;
};

#endif /* _PACKING_BUFFER_HPP_ */
