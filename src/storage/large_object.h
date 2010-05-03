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
 * large_object.h - Large object manager (LOM)
 */

#ifndef _LARGE_OBJECT_H_
#define _LARGE_OBJECT_H_

#ident "$Id$"

#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "recovery.h"

#if defined (CUBRID_DEBUG)
extern void largeobjmgr_dump (THREAD_ENTRY * thread_p, FILE * fp, LOID * loid,
			      int n);
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
extern bool largeobjmgr_check (THREAD_ENTRY * thread_p, LOID * loid);
#endif

extern int largeobjmgr_rv_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int largeobjmgr_rv_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int largeobjmgr_rv_get_newpage_undo (THREAD_ENTRY * thread_p,
					    LOG_RCV * recv);
extern int largeobjmgr_rv_get_newpage_redo (THREAD_ENTRY * thread_p,
					    LOG_RCV * recv);
extern int largeobjmgr_rv_split_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int largeobjmgr_rv_split_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_split_dump (FILE * fp, int length_ignore,
				       void *data);
extern int largeobjmgr_rv_overwrite (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_overwrite_dump (FILE * fp, int length,
					   void *dump_data);
extern int largeobjmgr_rv_putin (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int largeobjmgr_rv_takeout (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void largeobjmgr_rv_putin_dump (FILE * fp, int length_ignore,
				       void *data);
extern void largeobjmgr_rv_takeout_dump (FILE * fp, int length_ignore,
					 void *dump_data);
extern int largeobjmgr_rv_append_redo (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);
extern int largeobjmgr_rv_append_undo (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);
extern void largeobjmgr_rv_append_dump_undo (FILE * fp, int length_ignore,
					     void *dump_data);

#endif /* _LARGE_OBJECT_H_ */
