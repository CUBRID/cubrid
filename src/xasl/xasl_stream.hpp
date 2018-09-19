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

//
//  XASL stream - common interface for xasl_to_stream and stream_to_xasl
//

#ifndef _XASL_STREAM_HPP_
#define _XASL_STREAM_HPP_

#include "thread_compat.hpp"
#include "xasl.h"

// forward def
struct db_value;
struct regu_variable_node;

const std::size_t MAX_PTR_BLOCKS = 256;
const std::size_t OFFSETS_PER_BLOCK = 4096;
const std::size_t START_PTR_PER_BLOCK = 15;
/*
 * the linear byte stream for store the given XASL tree is allocated
 * and expanded dynamically on demand by the following amount of bytes
 */
const std::size_t STREAM_EXPANSION_UNIT = OFFSETS_PER_BLOCK * sizeof (int);

const int XASL_STREAM_ALIGN_UNIT = sizeof (double);
const int XASL_STREAM_ALIGN_MASK = XASL_STREAM_ALIGN_UNIT - 1;

/* structure of a visited pointer constant */
typedef struct visited_ptr STX_VISITED_PTR;
struct visited_ptr
{
  const void *ptr;		/* a pointer constant */
  void *str;			/* where the struct pointed by 'ptr' is stored */
};

/* structure for additional memory during filtered predicate unpacking */
typedef struct unpack_extra_buf UNPACK_EXTRA_BUF;
struct unpack_extra_buf
{
  char *buff;
  UNPACK_EXTRA_BUF *next;
};

/* structure to hold information needed during packing */
typedef struct xasl_unpack_info XASL_UNPACK_INFO;
struct xasl_unpack_info
{
  char *packed_xasl;		/* ptr to packed xasl tree */
#if defined (SERVER_MODE)
  THREAD_ENTRY *thrd;		/* used for private allocation */
#endif				/* SERVER_MODE */

  /* blocks of visited pointer constants */
  STX_VISITED_PTR *ptr_blocks[MAX_PTR_BLOCKS];

  char *alloc_buf;		/* alloced buf */

  int packed_size;		/* packed xasl tree size */

  /* low-water-mark of visited pointers */
  int ptr_lwm[MAX_PTR_BLOCKS];

  /* max number of visited pointers */
  int ptr_max[MAX_PTR_BLOCKS];

  int alloc_size;		/* alloced buf size */

  /* list of additional buffers allocated during xasl unpacking */
  UNPACK_EXTRA_BUF *additional_buffers;
  /* 1 if additional buffers should be tracked */
  int track_allocated_bufers;

  bool use_xasl_clone;		/* true, if uses xasl clone */
};

inline int xasl_stream_make_align (int x);
inline int xasl_stream_get_ptr_block (const void *ptr);

int stx_get_xasl_errcode (THREAD_ENTRY *thread_p);
void stx_set_xasl_errcode (THREAD_ENTRY *thread_p, int errcode);
XASL_UNPACK_INFO *stx_get_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p);
void stx_set_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p, XASL_UNPACK_INFO *ptr);
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
char *stx_build_db_value (THREAD_ENTRY *thread_p, char *tmp, DB_VALUE *ptr);
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
int
xasl_stream_make_align (int x)
{
  return (((x) & ~XASL_STREAM_ALIGN_MASK) + (((x) & XASL_STREAM_ALIGN_MASK) ? XASL_STREAM_ALIGN_UNIT : 0));
}

int
xasl_stream_get_ptr_block (const void *ptr)
{
  return static_cast<int> ((((UINTPTR) ptr) / sizeof (UINTPTR)) % MAX_PTR_BLOCKS);
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
      char *bufptr = &stx_get_xasl_unpack_info_ptr (thread_p)->packed_xasl[offset];
      target = (T *) stx_get_struct_visited_ptr (thread_p, bufptr);
      if (target != NULL)
	{
	  return;
	}
      if (stx_mark_struct_visited (thread_p, bufptr, target) != NO_ERROR)
	{
	  assert (false);
	  return;
	}
      target = (T *) stx_alloc_struct (thread_p, (int) sizeof (T));
      if (target == NULL)
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
