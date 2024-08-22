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

#include "xasl_unpack_info.hpp"

#include "memory_alloc.h"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(SERVER_MODE)
static XASL_UNPACK_INFO *xasl_Unpack_info = NULL;
#endif /* !SERVER_MODE */

/*
 * get_xasl_unpack_info_ptr () -
 *   return:
 */
XASL_UNPACK_INFO *
get_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p)
{
#if defined(SERVER_MODE)
  return thread_p->xasl_unpack_info_ptr;
#else /* SERVER_MODE */
  return xasl_Unpack_info;
#endif /* SERVER_MODE */
}

/*
 * set_xasl_unpack_info_ptr () -
 *   return:
 *   ptr(in)    :
 */
void
set_xasl_unpack_info_ptr (THREAD_ENTRY *thread_p, XASL_UNPACK_INFO *ptr)
{
#if defined (SERVER_MODE)
  thread_p->xasl_unpack_info_ptr = ptr;
#else
  xasl_Unpack_info = ptr;
#endif
}

/*
 * free_xasl_unpack_info () -
 *   return:
 *   xasl_unpack_info(in): unpack info returned by stx_map_stream_to_xasl ()
 *
 * Note: free the memory used for unpacking the xasl tree.
 */
void
free_xasl_unpack_info (THREAD_ENTRY *thread_p, REFPTR (XASL_UNPACK_INFO, xasl_unpack_info))
{
  free_unpack_extra_buff (thread_p, xasl_unpack_info);
#if defined (SERVER_MODE)
  if (xasl_unpack_info)
    {
      (xasl_unpack_info)->thrd = NULL;
    }
#endif /* SERVER_MODE */
  db_private_free_and_init (thread_p, xasl_unpack_info);
}

/*
 * free_unpack_extra_buff () - free additional buffers allocated during
 *				 XASL unpacking
 * return : void
 * xasl_unpack_info (in) : XASL unpack info
 */
void
free_unpack_extra_buff (THREAD_ENTRY *thread_p, XASL_UNPACK_INFO *xasl_unpack_info)
{
  if (xasl_unpack_info)
    {
      UNPACK_EXTRA_BUF *add_buff = xasl_unpack_info->additional_buffers;
      UNPACK_EXTRA_BUF *temp = NULL;
      while (add_buff != NULL)
	{
	  temp = add_buff->next;
	  db_private_free_and_init (thread_p, add_buff->buff);
	  db_private_free_and_init (thread_p, add_buff);
	  add_buff = temp;
	}
    }
}
