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
 * overflow_file.h - Overflow file manager (at server)
 */

#ifndef _OVERFLOW_FILE_H_
#define _OVERFLOW_FILE_H_

#ident "$Id$"

#include "config.h"
#include "file_manager.h"
#include "mvcc.h"
#include "page_buffer.h"
#include "recovery.h"
#include "slotted_page.h"
#include "storage_common.h"

typedef struct overflow_first_part OVERFLOW_FIRST_PART;
struct overflow_first_part
{
  VPID next_vpid;
  int length;
  char data[1];			/* Really more than one */
};

typedef struct overflow_rest_part OVERFLOW_REST_PART;
struct overflow_rest_part
{
  VPID next_vpid;
  char data[1];			/* Really more than one */
};


extern int overflow_insert (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, VPID * ovf_vpid, RECDES * recdes,
			    FILE_TYPE file_type);
extern int overflow_update (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid, RECDES * recdes,
			    FILE_TYPE file_type);
extern const VPID *overflow_delete (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid);
extern void overflow_flush (THREAD_ENTRY * thread_p, const VPID * ovf_vpid);
extern int overflow_get_length (THREAD_ENTRY * thread_p, const VPID * ovf_vpid);
extern SCAN_CODE overflow_get (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, RECDES * recdes,
			       MVCC_SNAPSHOT * mvcc_snapshot);
extern SCAN_CODE overflow_get_nbytes (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, RECDES * recdes, int start_offset,
				      int max_nbytes, int *remaining_length, MVCC_SNAPSHOT * mvcc_snapshot);
extern int overflow_get_capacity (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, int *ovf_length, int *ovf_num_pages,
				  int *ovf_overhead, int *ovf_free_space);
#if defined (CUBRID_DEBUG)
extern int overflow_dump (THREAD_ENTRY * thread_p, FILE * fp, VPID * ovf_vpid);
#endif
extern int overflow_rv_newpage_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int overflow_rv_newpage_link_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int overflow_rv_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void overflow_rv_link_dump (FILE * fp, int length_ignore, void *data);
extern int overflow_rv_page_update_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void overflow_rv_page_dump (FILE * fp, int length, void *data);
extern char *overflow_get_first_page_data (char *page_ptr);
#endif /* _OVERFLOW_FILE_H_ */
