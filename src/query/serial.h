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
 * serial.h: interface for serial functions
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "heap_attrinfo.h"
#include "record_descriptor.hpp"
#include "recovery.h"
#include "thread_compat.hpp"
#include "storage_common.h"

/* A client write of a _db_serial row in progress; see serial_row_write_keep_value () */
typedef struct serial_row_write SERIAL_ROW_WRITE;
struct serial_row_write
{
  bool active;
  bool merged;			/* serial_row_write_keep_value () filled its merged row */
  bool undo;			/* serial_row_write_undo () filled its undo */
  HEAP_CACHE_ATTRINFO old_info;
  HEAP_CACHE_ATTRINFO new_info;
};

/* How the log applier's image of a _db_serial row is written on this node; see serial_repl_prepare () */
typedef enum
{
  SERIAL_REPL_TAKE,		/* the image as it is */
  SERIAL_REPL_WRITE_BACK,	/* only while the page holds the block end the write-back replaced, with started 1 */
  SERIAL_REPL_FORWARD		/* only when it does not move current_val back; an active node's rule */
} SERIAL_REPL_RULE;

typedef struct serial_repl_apply SERIAL_REPL_APPLY;
struct serial_repl_apply
{
  SERIAL_REPL_RULE rule;
  bool rebuilt;			/* the image is this node's row with the image's current_val and started */
  DB_VALUE prior_val;		/* SERIAL_REPL_WRITE_BACK: the block end */
  const char *kept;		/* out: why the page's current_val and started stayed, or NULL */
  DB_VALUE page_cur;		/* out: with kept, the page's current_val */
  DB_VALUE image_cur;		/* out: with kept, the image's current_val */
  char name[DB_MAX_IDENTIFIER_LENGTH];	/* the serial's name, from the image's key */
};

extern int xserial_get_current_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p,
				      int cached_num);
extern int xserial_get_next_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p, int cached_num,
				   int num_alloc, int is_auto_increment, bool force_set_last_insert_id);
extern int xserial_get_cached_num (THREAD_ENTRY * thread_p, int *cached_num, const OID * serial_oidp);
extern void serial_flush_cache_pool (THREAD_ENTRY * thread_p);
extern void serial_finalize_cache_pool (void);
extern int serial_initialize_cache_pool (THREAD_ENTRY * thread_p, bool load_attr_info);
extern void xserial_decache (THREAD_ENTRY * thread_p, OID * oidp);
extern bool serial_repl_prepare (THREAD_ENTRY * thread_p, const OID * serial_oidp, const DB_VALUE * key,
				 RECDES * image, DB_VALUE * prior_val, bool issue, record_descriptor * rebuilt,
				 SERIAL_REPL_APPLY * apply);
extern void serial_repl_finish (THREAD_ENTRY * thread_p, const OID * serial_oidp, SERIAL_REPL_APPLY * apply);
extern void serial_repl_skip_missing_row (THREAD_ENTRY * thread_p, const DB_VALUE * key, DB_VALUE * prior_val);
extern void serial_row_write_begin (THREAD_ENTRY * thread_p, const OID * class_oidp, SERIAL_ROW_WRITE * write);
extern int serial_row_write_keep_value (THREAD_ENTRY * thread_p, SERIAL_ROW_WRITE * write, const OID * serial_oidp,
					RECDES * old_recdes, RECDES * new_recdes, record_descriptor * merged);
extern int serial_row_write_undo (THREAD_ENTRY * thread_p, SERIAL_ROW_WRITE * write, RECDES * old_recdes,
				  record_descriptor * undo);
extern void serial_row_write_end (THREAD_ENTRY * thread_p, SERIAL_ROW_WRITE * write);
extern int serial_rv_undo_ddl_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

#if defined (SERVER_MODE)
extern void serial_flush_cache_pool_replicated (THREAD_ENTRY * thread_p);
extern int serial_cache_index_btid (THREAD_ENTRY * thread_p);
extern void serial_get_index_btid (BTID * output);
#endif /* SERVER_MODE */

#endif /* _SERIAL_H_ */
