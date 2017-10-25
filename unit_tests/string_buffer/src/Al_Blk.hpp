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
#pragma once
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif

namespace Al
{//Allocator
  struct Blk
  {
    size_t dim;//size of the memory block pointed by ptr
    char* ptr; //pointer to a memory block

    Blk (size_t dim = 0, void* ptr = 0)
      : dim (dim)
      , ptr ((char*)ptr)
    {
    }

    operator bool () { return (dim && ptr); }

    friend bool operator== (Blk blk0, Blk blk1) { return (blk0.dim == blk1.dim && blk0.ptr == blk1.ptr); }

    friend bool operator!= (Blk blk0, Blk blk1) { return (blk0.dim != blk1.dim || blk0.ptr != blk1.ptr); }
  };
}// namespace Al
