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

/* Memory Block
 * - groups together memory address and its size
 * - used to allocate, deallocate and share memory
 * - could be extended with helper info: allocator, src file&line where allocation was made, ...
 */

#if !defined(_MEM_BLOCK_HPP_)
#define _MEM_BLOCK_HPP_

namespace mem
{

  struct block
  {
    size_t dim;
    char  *ptr;

    block()
      : dim (0)
      , ptr (nullptr)
    {}

    block (size_t dim, void *ptr)
      : dim (dim)
      , ptr ((char *)ptr)
    {}

    bool is_valid ()
    {
      return (dim != 0 && ptr != 0);
    }
  };

} // namespace mem

#endif // !defined(_MEM_BLOCK_HPP_)
