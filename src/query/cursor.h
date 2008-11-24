/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * Cursor (Client Side)
 */

#ifndef _CURSOR_H_
#define _CURSOR_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "dbdef.h"
#include "query_list.h"
#include "storage_common.h"
#include "object_primitive.h"
#include "object_representation.h"

enum
{
  DB_CURSOR_SEEK_SET = 1,
  DB_CURSOR_SEEK_CUR,
  DB_CURSOR_SEEK_END
};

typedef enum
{
  C_BEFORE = 1,
  C_ON,
  C_AFTER
} CURSOR_POSITION;

typedef struct cursor_id CURSOR_ID;	/* Cursor Identifier */
struct cursor_id
{
  int query_id;			/* Query id for this cursor */
  QFILE_LIST_ID list_id;	/* List file identifier */
  bool is_updatable;		/* Cursor updatable ?   */
  bool is_oid_included;		/* Cursor has first hidden oid col. */
  int oid_ent_count;		/* Number of OIDs in the oid set */
  OID *oid_set;			/* Cursor current page oid set */
  MOP *mop_set;			/* Cursor current page MOP set */
  CURSOR_POSITION position;	/* Cursor position */
  VPID current_vpid;		/* Current real page identifier */
  VPID next_vpid;		/* Next page identifier */
  VPID header_vpid;		/* Header page identifier in buffer area */
  int tuple_no;			/* Tuple position number */
  QFILE_TUPLE_RECORD tuple_record;	/* Tuple descriptor */
  int on_overflow;		/* cursor buffer has an overflow page */
  char *buffer;			/* Current page */
  char *buffer_area;
  int buffer_filled_size;
  int buffer_tuple_count;	/* Tuple count in current page */
  int current_tuple_no;		/* Tuple position in current page */
  int current_tuple_offset;	/* Tuple offset in current page */
  char *current_tuple_p;	/* Current tuple */
  int current_tuple_length;	/* Current tuple length */
  int *oid_col_no;		/* Column numbers of OID's */
  int oid_col_no_cnt;		/* Number of values in oid_col_no */
  DB_FETCH_MODE prefetch_lock_mode;
  bool is_copy_tuple_value;	/* get tplvalue: true  = copy(default),
				 *               false = peek */
  int current_tuple_value_index;	/* Current tplvalue index within current_tuple_p */
  char *current_tuple_value_p;	/* Current tplvalue pointer within current_tuple_p */
};

extern int cursor_copy_list_id (QFILE_LIST_ID * dest_list_id,
				const QFILE_LIST_ID * src_list_id);
extern void cursor_free_list_id (QFILE_LIST_ID * list_id, bool self);
extern int cursor_copy_vobj_to_dbvalue (OR_BUF * buf, DB_VALUE * db_value);
extern int cursor_fetch_page_having_tuple (CURSOR_ID * cursor_id,
					   VPID * vpid,
					   int position, int offset);
extern void cursor_print_list (int query_id, QFILE_LIST_ID * list_id);
extern bool cursor_open (CURSOR_ID * cursor_id, QFILE_LIST_ID * list_id,
			 bool updatable, bool oid_included);
extern int cursor_next_tuple (CURSOR_ID * cursor_id);
extern int cursor_get_tuple_value_list (CURSOR_ID * cursor_id,
					int size, DB_VALUE * value_list);
extern void cursor_close (CURSOR_ID * cursor_id);
extern DB_FETCH_MODE cursor_set_prefetch_lock_mode (CURSOR_ID * cursor_id,
						    DB_FETCH_MODE mode);
extern bool cursor_set_copy_tuple_value (CURSOR_ID * cursor_id, bool copy);
extern int cursor_set_oid_columns (CURSOR_ID * cursor_id,
				   int *oid_col_no, int oid_col_no_cnt);
extern void cursor_free (CURSOR_ID * cursor_id);
extern int cursor_get_current_oid (CURSOR_ID * cursor_id,
				   DB_VALUE * crs_value);
extern int cursor_prev_tuple (CURSOR_ID * cursor_id);
extern int cursor_first_tuple (CURSOR_ID * cursor_id);
extern int cursor_last_tuple (CURSOR_ID * cursor_id);
extern int cursor_get_tuple_value (CURSOR_ID * result,
				   int index, DB_VALUE * value);

#endif /* _CURSOR_H_ */
