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
 * loader_disk.h: loader transformer disk access module
 */

#ifndef _LOADER_DISK_H_
#define _LOADER_DISK_H_

#ident "$Id$"

#include "load_object.h"

/* Module control */
extern int disk_init (void);
extern void disk_final (void);

/* Instance operations */
extern int disk_reserve_instance (MOP classop, OID * oid);
extern int disk_insert_instance (MOP classop, DESC_OBJ * obj, OID * oid);
extern int disk_update_instance (MOP classop, DESC_OBJ * obj, OID * oid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int disk_insert_instance_using_mobj (MOP classop, MOBJ classobj,
					    MOBJ obj, OID * oid);
extern int disk_update_instance_using_mobj (MOP classop, MOBJ classobj,
					    MOBJ obj, OID * oid);
#endif
#endif /* _LOADER_DISK_H_ */
