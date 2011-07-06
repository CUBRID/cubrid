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
 * serial.h: interface for serial functions
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#ident "$Id$"

#include "dbtype.h"
#include "thread.h"

extern int xserial_get_current_value (THREAD_ENTRY * thread_p,
				      DB_VALUE * result_num,
				      const DB_VALUE * oid_str_val);
extern int xserial_get_next_value (THREAD_ENTRY * thread_p,
				   DB_VALUE * result_num,
				   const DB_VALUE * oid_str_val,
				   int is_auto_increment,
				   bool force_set_last_insert_id);
extern void serial_finalize_cache_pool (void);
extern int serial_initialize_cache_pool (THREAD_ENTRY * thread_p);
extern void xserial_decache (OID * oidp);

#endif /* _SERIAL_H_ */
