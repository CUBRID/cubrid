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

#include "xasl_unpack_info.hpp"

#include "memory_alloc.h"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#endif

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
