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
 * large_object.h - Large object manager (LOM)
 */

#ifndef _LARGE_OBJECT_H_
#define _LARGE_OBJECT_H_

#ident "$Id$"

#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "recovery.h"

extern LOID *xlargeobjmgr_create (THREAD_ENTRY * thread_p, LOID * loid,
				  int length, char *buffer, int est_lo_len,
				  OID * oid);
extern int xlargeobjmgr_destroy (THREAD_ENTRY * thread_p, LOID * loid);
extern int xlargeobjmgr_read (THREAD_ENTRY * thread_p, LOID * loid,
			      int offset, int length, char *buffer);
extern int xlargeobjmgr_write (THREAD_ENTRY * thread_p, LOID * loid,
			       int offset, int length, char *buffer);
extern int xlargeobjmgr_insert (THREAD_ENTRY * thread_p, LOID * loid,
				int offset, int length, char *buffer);
extern int xlargeobjmgr_delete (THREAD_ENTRY * thread_p, LOID * loid,
				int offset, int length);
extern int xlargeobjmgr_append (THREAD_ENTRY * thread_p, LOID * loid,
				int length, char *buffer);
extern int xlargeobjmgr_truncate (THREAD_ENTRY * thread_p, LOID * loid,
				  int offset);
extern int xlargeobjmgr_compress (THREAD_ENTRY * thread_p, LOID * loid);
extern int xlargeobjmgr_length (THREAD_ENTRY * thread_p, LOID * loid);

extern void largeobjmgr_dump (THREAD_ENTRY * thread_p, LOID * loid, int n);
extern bool largeobjmgr_check (THREAD_ENTRY * thread_p, LOID * loid);

extern int largeobjmgr_rv_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int largeobjmgr_rv_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int largeobjmgr_rv_get_newpage_undo (THREAD_ENTRY * thread_p,
					    LOG_RCV * recv);
extern int largeobjmgr_rv_get_newpage_redo (THREAD_ENTRY * thread_p,
					    LOG_RCV * recv);
extern int largeobjmgr_rv_split_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int largeobjmgr_rv_split_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_split_dump (int length_ignore, void *data);
extern int largeobjmgr_rv_overwrite (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_overwrite_dump (int length, void *dump_data);
extern int largeobjmgr_rv_putin (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int largeobjmgr_rv_takeout (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_putin_dump (int length_ignore, void *data);
extern void largeobjmgr_rv_takeout_dump (int length_ignore, void *dump_data);
extern int largeobjmgr_rv_append_redo (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);
extern int largeobjmgr_rv_append_undo (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);
extern void largeobjmgr_rv_append_dump_undo (int length_ignore,
					     void *dump_data);

#endif /* _LARGE_OBJECT_H_ */
