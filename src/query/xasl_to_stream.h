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

/*
 * xasl_to_steam.h
 */

#ifndef _XASL_TO_STREAM_H_
#define _XASL_TO_STREAM_H_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "xasl.h"

extern int xts_map_xasl_to_stream (const XASL_NODE * xasl, XASL_STREAM * stream);
extern int xts_map_filter_pred_to_stream (const PRED_EXPR_WITH_CONTEXT * pred, char **stream, int *size);
extern int xts_map_func_pred_to_stream (const FUNC_PRED * xasl, char **stream, int *size);

extern int xts_sizeof (const json_table_node &);
extern int xts_sizeof (const json_table_column &);
extern int xts_sizeof (const json_table_spec_node &);
extern char *xts_process (char *, const json_table_node &);
extern char *xts_process (char *, const json_table_column &);
extern char *xts_process (char *, const json_table_spec_node &);

extern int xts_get_offset_visited_ptr (const void *ptr);
extern int xts_mark_ptr_visited (const void *ptr, int offset);
extern int xts_reserve_location_in_stream (int size);
extern char *xts_Stream_buffer;

template < typename T > int
xts_save (const T & t)
{
  int packed_length;
  char *ptr;

  int offset = xts_get_offset_visited_ptr (&t);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  packed_length = xts_reserve_location_in_stream (xts_sizeof (t));

  offset = xts_reserve_location_in_stream (packed_length);
  if (offset == ER_FAILED || xts_mark_ptr_visited (&t, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }
  ptr = &xts_Stream_buffer[offset];
  ptr = xts_process (ptr, t);

  return offset;
}

#endif /* !_XASL_TO_STREAM_H_ */
