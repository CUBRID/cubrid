/*
 * Copyright 2008 Search Solution Corporation
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

//
//  XASL stream - common interface for xasl_to_stream and stream_to_xasl
//

#ifndef _XASL_STREAM_HPP_
#define _XASL_STREAM_HPP_

#include "json_table_def.h"
#include "thread_compat.hpp"
#include "xasl_unpack_info.hpp"

#include <cstddef>

// forward def
struct db_value;
class regu_variable_node;

namespace cubxasl
{
  namespace json_table
  {
    struct column;
    struct node;
    struct spec_node;
  } // namespace json_table
} // namespace cubxasl

const size_t OFFSETS_PER_BLOCK = 4096;
const size_t START_PTR_PER_BLOCK = 15;
/*
 * the linear byte stream for store the given XASL tree is allocated
 * and expanded dynamically on demand by the following amount of bytes
 */
const size_t STREAM_EXPANSION_UNIT = OFFSETS_PER_BLOCK * sizeof (int);

const int XASL_STREAM_ALIGN_UNIT = sizeof (double);
const int XASL_STREAM_ALIGN_MASK = XASL_STREAM_ALIGN_UNIT - 1;

inline int xasl_stream_make_align (int x);

int stx_get_xasl_errcode (THREAD_ENTRY *thread_p);
void stx_set_xasl_errcode (THREAD_ENTRY *thread_p, int errcode);
int stx_init_xasl_unpack_info (THREAD_ENTRY *thread_p, char *xasl_stream, int xasl_stream_size);

int stx_mark_struct_visited (THREAD_ENTRY *thread_p, const void *ptr, void *str);
void *stx_get_struct_visited_ptr (THREAD_ENTRY *thread_p, const void *ptr);
void stx_free_visited_ptrs (THREAD_ENTRY *thread_p);
char *stx_alloc_struct (THREAD_ENTRY *thread_p, int size);

// all stx_build overloads
char *stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::spec_node &jts);
char *stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::column &jtc);
char *stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::node &jtn);
// next stx_build functions are not ported to xasl_stream.cpp and cannot be used for debug checks
char *stx_build (THREAD_ENTRY *thread_p, char *ptr, db_value &val);
char *stx_build (THREAD_ENTRY *thread_p, char *ptr, regu_variable_node &reguvar);

// dependencies not ported
char *stx_build_db_value (THREAD_ENTRY *thread_p, char *tmp, db_value *ptr);
char *stx_build_string (THREAD_ENTRY *thread_p, char *tmp, char *ptr);

// restore string; return restored string, updates stream pointer
char *stx_restore_string (THREAD_ENTRY *thread_p, char *&ptr);

// all stx_unpack overloads; equivalent to stx_build, but never used for stx_restore
char *stx_unpack (THREAD_ENTRY *thread_p, char *tmp, json_table_column_behavior &behavior);

// xasl stream compare functions - used for debugging to compare originals and packed/unpacked objects
bool xasl_stream_compare (const cubxasl::json_table::column &first, const cubxasl::json_table::column &second);
bool xasl_stream_compare (const cubxasl::json_table::node &first, const cubxasl::json_table::node &second);
bool xasl_stream_compare (const cubxasl::json_table::spec_node &first, const cubxasl::json_table::spec_node &second);

template <typename T>
static void stx_alloc (THREAD_ENTRY *thread_p, T *&ptr);
template <typename T>
static void stx_alloc_array (THREAD_ENTRY *thread_p, T *&ptr, std::size_t count);

template <typename T>
void stx_restore (THREAD_ENTRY *thread_p, char *&ptr, T *&target);

//////////////////////////////////////////////////////////////////////////
// Template and inline implementation
//////////////////////////////////////////////////////////////////////////

#include "object_representation.h"
#include "system.h"

#include <cassert>

inline int
xasl_stream_make_align (int x)
{
  return (((x) & ~XASL_STREAM_ALIGN_MASK) + (((x) & XASL_STREAM_ALIGN_MASK) ? XASL_STREAM_ALIGN_UNIT : 0));
}

// restore from stream buffer
//
// template T should have an overload of stx_build.
//
// if you want to prevent saving to and restoring from stream buffer, use stx_unpack instead of stx_build.
//
template <typename T>
static void
stx_restore (THREAD_ENTRY *thread_p, char *&ptr, T *&target)
{
#if !defined (CS_MODE)
  int offset;

  ptr = or_unpack_int (ptr, &offset);
  if (offset == 0)
    {
      target = NULL;
    }
  else
    {
      char *bufptr = &get_xasl_unpack_info_ptr (thread_p)->packed_xasl[offset];
      target = (T *) stx_get_struct_visited_ptr (thread_p, bufptr);
      if (target != NULL)
	{
	  return;
	}
      target = (T *) stx_alloc_struct (thread_p, (int) sizeof (T));
      if (target == NULL)
	{
	  assert (false);
	  return;
	}
      if (stx_mark_struct_visited (thread_p, bufptr, target) != NO_ERROR)
	{
	  assert (false);
	  return;
	}
      if (stx_build (thread_p, bufptr, *target) == NULL)
	{
	  assert (false);
	}
    }
#else // CS_MODE
  // NOTE - in CS_MODE, we only need to do some debug checks and we don't have to do actual restoring
  int dummy;
  ptr = or_unpack_int (ptr, &dummy);
  target = NULL;
#endif  // CS_MODE
}

template <typename T>
void stx_alloc (THREAD_ENTRY *thread_p, T *&ptr)
{
  ptr = (T *) stx_alloc_struct (thread_p, (int) sizeof (T));
}

template <typename T>
static void stx_alloc_array (THREAD_ENTRY *thread_p, T *&ptr, std::size_t count)
{
  ptr = (T *) stx_alloc_struct (thread_p, (int) (count * sizeof (T)));
}

#endif // _XASL_STREAM_HPP_
