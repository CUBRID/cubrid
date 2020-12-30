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

/*
 * xasl_unpack_info - interface to information used during packing
 */

#ifndef _XASL_UNPACK_INFO_HPP_
#define _XASL_UNPACK_INFO_HPP_

#include "porting.h"
#include "system.h"
#include "thread_compat.hpp"

const size_t MAX_PTR_BLOCKS = 256;

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

XASL_UNPACK_INFO *get_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p);
void set_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p, XASL_UNPACK_INFO *ptr);

void free_xasl_unpack_info (THREAD_ENTRY *thread_p, REFPTR (XASL_UNPACK_INFO, xasl_unpack_info));
void free_unpack_extra_buff (THREAD_ENTRY *thread_p, XASL_UNPACK_INFO *unpack_info_ptr);

inline int xasl_stream_get_ptr_block (const void *ptr);

inline int
xasl_stream_get_ptr_block (const void *ptr)
{
  return static_cast<int> ((((UINTPTR) ptr) / sizeof (UINTPTR)) % MAX_PTR_BLOCKS);
}

#endif // !_XASL_UNPACK_INFO_HPP_
