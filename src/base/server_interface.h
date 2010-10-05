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
 * server_interface.h - Server interface functions
 *
 * Note: This file defines the interface to the server. The client modules that
 * calls any function in the server should include this module instead of the
 * header file of the desired function.
 */

#ifndef _SERVER_INTERFACE_H_
#define _SERVER_INTERFACE_H_

#ident "$Id$"

enum
{ SI_SYS_DATETIME = 1, SI_LOCAL_TRANSACTION_ID = 2,	/* next is 4 */
  SI_CNT = 2
};

enum
{
  CHECKDB_FILE_TRACKER_CHECK = 1,
  CHECKDB_HEAP_CHECK_ALLHEAPS = 2,
  CHECKDB_CT_CHECK_CAT_CONSISTENCY = 4,
  CHECKDB_BTREE_CHECK_ALL_BTREES = 8,
  CHECKDB_LC_CHECK_CLASSNAMES = 16,
  CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES = 32,
  CHECKDB_REPAIR = 64
};

#define CHECKDB_ALL_CHECK \
    (CHECKDB_FILE_TRACKER_CHECK         | CHECKDB_HEAP_CHECK_ALLHEAPS  | \
     CHECKDB_CT_CHECK_CAT_CONSISTENCY | CHECKDB_BTREE_CHECK_ALL_BTREES  | \
     CHECKDB_LC_CHECK_CLASSNAMES      | CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES)

#define COMPACTDB_LOCKED_CLASS -1
#define COMPACTDB_INVALID_CLASS -2
#define COMPACTDB_UNPROCESSED_CLASS -3

#define COMPACTDB_REPR_DELETED -2

#endif /* _SERVER_INTERFACE_H_ */
