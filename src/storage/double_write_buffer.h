/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * double_write_buffer.h
 */

#ifndef _DOUBLE_WRITE_BUFFER_H_
#define _DOUBLE_WRITE_BUFFER_H_

#ident "$Id$"

#include "file_io.h"

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
