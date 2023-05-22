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
 * Cursor (Client Side)
 */

#ifndef _CURSOR_H_
#define _CURSOR_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "query_list.h"
#include "storage_common.h"

// forward definitions
struct or_buf;

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
  QUERY_ID query_id;		/* Query id for this cursor */
  QFILE_LIST_ID list_id;	/* List file identifier */
  OID *oid_set;			/* Cursor current page oid set */
  MOP *mop_set;			/* Cursor current page MOP set */
  int oid_ent_count;		/* Number of OIDs in the oid set */
  CURSOR_POSITION position;	/* Cursor position */
  VPID current_vpid;		/* Current real page identifier */
  VPID next_vpid;		/* Next page identifier */
  VPID header_vpid;		/* Header page identifier in buffer area */
  int on_overflow;		/* cursor buffer has an overflow page */
  int tuple_no;			/* Tuple position number */
  QFILE_TUPLE_RECORD tuple_record;	/* Tuple descriptor */
  char *buffer;			/* Current page */
  char *buffer_area;
  int buffer_filled_size;
  int buffer_tuple_count;	/* Tuple count in current page */
  int current_tuple_no;		/* Tuple position in current page */
  int current_tuple_offset;	/* Tuple offset in current page */
  char *current_tuple_p;	/* Current tuple */
  int *oid_col_no;		/* Column numbers of OID's */
  int current_tuple_length;	/* Current tuple length */
  int oid_col_no_cnt;		/* Number of values in oid_col_no */
  DB_FETCH_MODE prefetch_lock_mode;
  int current_tuple_value_index;	/* Current tplvalue index within current_tuple_p */
  char *current_tuple_value_p;	/* Current tplvalue pointer within current_tuple_p */
  bool is_updatable;		/* Cursor updatable ? */
  bool is_oid_included;		/* Cursor has first hidden oid col. */
  bool is_copy_tuple_value;	/* get tplvalue: true = copy(default), false = peek */
};

extern int cursor_copy_list_id (QFILE_LIST_ID * dest_list_id, const QFILE_LIST_ID * src_list_id);

#define cursor_free_list_id(list_id) \
        do { \
          QFILE_LIST_ID *list_id_p = (QFILE_LIST_ID *) (list_id); \
          if (list_id_p != NULL) { \
            if (list_id_p->last_pgptr) { \
              free_and_init (list_id_p->last_pgptr); \
            } \
            if (list_id_p->tpl_descr.f_valp) { \
              free_and_init (list_id_p->tpl_descr.f_valp); \
            } \
            if (list_id_p->tpl_descr.clear_f_val_at_clone_decache) { \
              free_and_init (list_id_p->tpl_descr.clear_f_val_at_clone_decache); \
            } \
            if (list_id_p->sort_list) { \
              free_and_init (list_id_p->sort_list); \
            } \
            if (list_id_p->type_list.domp) { \
              free_and_init (list_id_p->type_list.domp); \
            } \
          } \
        } while (0)

#define cursor_free_self_list_id(list_id) \
        do { \
          cursor_free_list_id (list_id); \
          free_and_init (list_id); \
        } while (0)

extern int cursor_copy_vobj_to_dbvalue (struct or_buf *buf, DB_VALUE * db_value);
extern int cursor_fetch_page_having_tuple (CURSOR_ID * cursor_id, VPID * vpid, int position, int offset);
#if defined (WINDOWS) || defined (CUBRID_DEBUG)
extern void cursor_print_list (QUERY_ID query_id, QFILE_LIST_ID * list_id);
#endif
extern bool cursor_open (CURSOR_ID * cursor_id, QFILE_LIST_ID * list_id, bool updatable, bool oid_included);
extern int cursor_next_tuple (CURSOR_ID * cursor_id);
extern int cursor_get_tuple_value_list (CURSOR_ID * cursor_id, int size, DB_VALUE * value_list);
extern void cursor_close (CURSOR_ID * cursor_id);
extern DB_FETCH_MODE cursor_set_prefetch_lock_mode (CURSOR_ID * cursor_id, DB_FETCH_MODE mode);
extern bool cursor_set_copy_tuple_value (CURSOR_ID * cursor_id, bool copy);
extern int cursor_set_oid_columns (CURSOR_ID * cursor_id, int *oid_col_no, int oid_col_no_cnt);
extern void cursor_free (CURSOR_ID * cursor_id);
extern int cursor_get_current_oid (CURSOR_ID * cursor_id, DB_VALUE * crs_value);
extern int cursor_prev_tuple (CURSOR_ID * cursor_id);
extern int cursor_first_tuple (CURSOR_ID * cursor_id);
extern int cursor_last_tuple (CURSOR_ID * cursor_id);
extern int cursor_get_tuple_value (CURSOR_ID * result, int index, DB_VALUE * value);

#endif /* _CURSOR_H_ */
