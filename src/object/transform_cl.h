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
 * transform_cl.h: Function declarations for the client side transformation
 *      routines.
 */

#ifndef _TRANSFORM_CL_H_
#define _TRANSFORM_CL_H_

#ident "$Id$"

#include "locator.h"

/*
 * this should be an enumeration but define it as ints first to
 * ease the integration
 */
typedef int TF_STATUS;

#define TF_SUCCESS 	(0)
#define TF_OUT_OF_SPACE (1)
#define TF_ERROR 	(2)


extern TF_STATUS tf_mem_to_disk (MOP classmop, MOBJ classobj,
				 volatile MOBJ obj,
				 RECDES * record, bool * index_flag);

extern MOBJ tf_disk_to_mem (MOBJ classobj, RECDES * record, int *convertp);

extern TF_STATUS tf_class_to_disk (MOBJ classobj, RECDES * record);

extern MOBJ tf_disk_to_class (OID * oid, RECDES * record);

extern int tf_object_size (MOBJ classobj, MOBJ obj);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void tf_dump_class_size (MOBJ classobj);
#endif

/* new hacks for bulk temporary OID upgrading */
extern OID *tf_need_permanent_oid (OR_BUF * buf, DB_OBJECT * obj);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int tf_find_temporary_oids (LC_OIDSET * oidset, MOBJ classobj,
				   MOBJ obj);

/* Set packing for M */
extern int tf_set_size (DB_SET * set);
extern int tf_pack_set (DB_SET * set, char *buffer, int buffer_size,
			int *actual_bytes);
#endif /* ENABLE_UNUSED_FUNCTION */

/* temporary integration kludge */
extern int tf_Allow_fixups;

#endif /* _TRANSFORM_CL_H_ */
