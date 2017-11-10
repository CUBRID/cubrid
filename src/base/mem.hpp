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
 */

/* Memory Management Functionality
 */

#if !defined(_MEM_HPP_)
#define _MEM_HPP_

#include "mem_block.hpp"
#include <memory.h>

namespace mem
{

  inline void default_realloc(mem::block& block, size_t len)
  {
#if 0 //realloc based
    if(block.dim == 0)
    {
      block.dim = 1;
    }
    for (size_t n=block.dim; block.dim < n+len; block.dim*=2); // calc next power of 2 >= block.dim
    block.ptr = (char *) realloc (block.ptr, block.dim);
#else //new + memcopy + delete
    size_t dim = block.dim ? block.dim : 1;
    for(; dim < block.dim+len; dim*=2); // calc next power of 2 >= block.dim
    mem::block b{dim, new char[dim]};
    memcpy(b.ptr, block.ptr, block.dim);
    delete block.ptr;
    block = b;
#endif
  }

  inline void default_dealloc(mem::block& block)
  {
#if 0 //free based
    free(block.ptr);
    block = {};
#else //delete based
    delete block.ptr;
    block = {};
#endif
  }

} // namespace mem

#endif // !defined(_MEM_HPP_)
