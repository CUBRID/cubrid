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
 * Value fetch routines
 */

#ifndef _FETCH_H_
#define _FETCH_H_

#ident "$Id$"

#include "storage_common.h"
#include "oid.h"
#include "query_evaluator.h"

extern int fetch_peek_dbval (THREAD_ENTRY * thread_p,
			     REGU_VARIABLE * regu_var, VAL_DESCR * vd,
			     OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			     DB_VALUE ** peek_dbval);
extern int fetch_copy_dbval (THREAD_ENTRY * thread_p,
			     REGU_VARIABLE * regu_var, VAL_DESCR * vd,
			     OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			     DB_VALUE * dbval);
extern int fetch_val_list (THREAD_ENTRY * thread_p,
			   REGU_VARIABLE_LIST regu_list, VAL_DESCR * vd,
			   OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			   int peek);
extern void fetch_init_val_list (REGU_VARIABLE_LIST regu_list);

#endif /* _FETCH_H_ */
