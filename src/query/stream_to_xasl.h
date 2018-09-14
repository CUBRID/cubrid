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

#ifndef _STREAM_TO_XASL_H_
#define _STREAM_TO_XASL_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "thread_compat.hpp"
#include "xasl.h"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

extern int stx_map_stream_to_xasl (THREAD_ENTRY * thread_p, XASL_NODE ** xasl_tree, bool use_xasl_clone,
				   char *xasl_stream, int xasl_stream_size, void **xasl_unpack_info_ptr);
extern int stx_map_stream_to_filter_pred (THREAD_ENTRY * thread_p, PRED_EXPR_WITH_CONTEXT ** pred_expr_tree,
					  char *pred_stream, int pred_stream_size);
extern int stx_map_stream_to_func_pred (THREAD_ENTRY * thread_p, FUNC_PRED ** xasl, char *xasl_stream,
					int xasl_stream_size, void **xasl_unpack_info_ptr);
extern int stx_map_stream_to_xasl_node_header (THREAD_ENTRY * thread_p, XASL_NODE_HEADER * xasl_header_p,
					       char *xasl_stream);
extern void stx_free_xasl_unpack_info (void *unpack_info_ptr);
extern void stx_free_additional_buff (THREAD_ENTRY * thread_p, void *unpack_info_ptr);
extern void stx_init_analytic_type_unserialized_fields (ANALYTIC_TYPE * analytic);


template < typename T > static T *
stx_restore_json_table_struct (THREAD_ENTRY * thread_p, char *ptr, int nelements,
			       std::function < char *(THREAD_ENTRY *, char *, T *) >unpack_func)
{
  T *result = (T *) stx_alloc_struct (thread_p, sizeof (T) * nelements);
  XASL_UNPACK_INFO *xasl_unpack_info = stx_get_xasl_unpack_info_ptr (thread_p);
  int offset;
  if (result == NULL)
    {
      stx_set_xasl_errcode (thread_p, ER_OUT_OF_VIRTUAL_MEMORY);
      return NULL;
    }

  for (int i = 0; i < nelements; ++i)
    {

      ptr = or_unpack_int (ptr, &offset);

      ptr = unpack_func (thread_p, &xasl_unpack_info->packed_xasl[offset], &result[i]);
    }

  return result;
}

#endif /* _STREAM_TO_XASL_H_ */
