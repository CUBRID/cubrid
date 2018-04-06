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
 * mem_buffer.hpp
 */

#ident "$Id$"

#ifndef _MEM_BUFFER_HPP_
#define _MEM_BUFFER_HPP_

#include "pinning.hpp"
#include "dbtype.h"
#include <atomic>
#include <vector>

/*
 * This should serve as storage for packing / unpacking objects
 * This is not intended to be used as character stream, but as bulk operations: users of it
 * reserve / allocate parts of it; there are objects which deal of byte level operations (see : packer)
 */
namespace mem
{

class buffer : public cubbase::pinnable
{
public:
  buffer () { storage = NULL; };
  buffer (char *ptr, const size_t buf_size) { init (ptr, buf_size, NULL); };

  ~buffer () { assert (get_pin_count () == 0); };

  char * get_buffer (void) { return storage; };

  size_t get_buffer_size (void) { return end_ptr - storage; };

  int init (char *ptr, const size_t buf_size, cubbase::pinner *referencer);
    
protected:

  /* start of allocated memory */
  char *storage;
  /* end of allocated memory */
  char *end_ptr;
};

} /* namespace mem */

#endif /* _MEM_BUFFER_HPP_ */
