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
 * cas_db_inc.h -
 */

#ifndef	_CAS_DB_INC_H_
#define	_CAS_DB_INC_H_

#ident "$Id$"

#include "dbi.h"
#include "dbval.h"
/*#include "db.h"*/
extern int db_get_connect_status (void);
extern void db_set_connect_status (int status);

#define CUBRID_VERSION(X, Y)	(((X) << 8) | (Y))
#define CUR_CUBRID_VERSION	\
	CUBRID_VERSION(MAJOR_VERSION, MINOR_VERSION)

#if defined(WINDOWS)
extern char **db_get_lock_classes (DB_SESSION * session);
#endif

#endif /* _CAS_DB_INC_H_ */
