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
 * dbtype.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications.
 */

#ifndef _DBTYPE_H_
#define _DBTYPE_H_

#ident "$Id$"

#include "config.h"

#include "system_parameter.h"
#include "dbdef.h"
#include "error_manager.h"

#include "dbtype_common.h"

#ifdef SERVER_MODE
/* Use the inlined version of the functions. */
#include "db_macro.i"
#endif

#endif /* _DBTYPE_H_ */
