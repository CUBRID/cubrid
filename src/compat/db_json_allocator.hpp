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

#ifndef _DB_JSON_PRIVATE_ALLOCATOR_
#define _DB_JSON_PRIVATE_ALLOCATOR_

// disable rapidjson compile warnings
#if defined (__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "rapidjson/allocators.h"
#if defined (__GNUC__)
#pragma GCC diagnostic pop
#endif

class JSON_PRIVATE_ALLOCATOR
{
  public:
    static const bool kNeedFree;
    void *Malloc (size_t size);
    void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
    static void Free (void *ptr);
};

typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;

#endif
