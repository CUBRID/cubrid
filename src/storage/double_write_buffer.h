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
 * double_write_buffer.h
 */

#ifndef _DOUBLE_WRITE_BUFFER_H_
#define _DOUBLE_WRITE_BUFFER_H_

#ident "$Id$"

#include "file_io.h"
#include "log_lsa.hpp"

/* The double write slot type */
typedef struct double_write_slot DWB_SLOT;
struct double_write_slot
{
  FILEIO_PAGE *io_page;		/* The contained page or NULL. */
  VPID vpid;			/* The page identifier. */
  LOG_LSA lsa;			/* The page LSA */
  unsigned int position_in_block;	/* The position in block. */
  unsigned int block_no;	/* The number of the block where the slot reside. */
};

/* double write buffer interface */
extern bool dwb_is_created (void);
extern int dwb_create (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_recreate (THREAD_ENTRY * thread_p);
extern int dwb_load_and_recover_pages (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_destroy (THREAD_ENTRY * thread_p);
extern char *dwb_get_volume_name (void);
extern int dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync);
extern int dwb_read_page (THREAD_ENTRY * thread_p, const VPID * vpid, void *io_page, bool * success);
extern int dwb_set_data_on_next_slot (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, bool can_wait,
				      DWB_SLOT ** p_dwb_slot);
extern int dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid, DWB_SLOT ** p_dwb_slot);

#if defined (SERVER_MODE)
extern void dwb_daemons_init ();
extern void dwb_daemons_destroy ();
#endif /* SERVER_MODE */

#endif /* _DOUBLE_WRITE_BUFFER_H_ */
