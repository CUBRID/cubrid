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
 * dbdef.h -Supporting definitions for the CUBRID API functions.
 *
 */

#ifndef _DBDEF_H_
#define _DBDEF_H_

#ident "$Id$"

#include "cubrid_api.h"
#define SERVER_SESSION_KEY_SIZE			8

typedef struct dbdef_vol_ext_info DBDEF_VOL_EXT_INFO;
struct dbdef_vol_ext_info
{
  const char *path;		/* Directory where the volume extension is created.  If NULL, is given, it defaults to
				 * the system parameter. */
  const char *name;		/* Name of the volume extension If NULL, system generates one like "db".ext"volid"
				 * where "db" is the database name and "volid" is the volume identifier to be assigned
				 * to the volume extension. */
  const char *comments;		/* Comments which are included in the volume extension header. */
  int max_npages;		/* Maximum pages of this volume */
  int extend_npages;		/* Number of pages to extend - used for generic volume only */
  INT32 nsect_total;		/* DKNSECTS type, number of sectors for volume extension */
  INT32 nsect_max;		/* DKNSECTS type, maximum number of sectors for volume extension */
  int max_writesize_in_sec;	/* the amount of volume written per second */
  DB_VOLPURPOSE purpose;	/* The purpose of the volume extension. One of the following: -
				 * DB_PERMANENT_DATA_PURPOSE, DB_TEMPORARY_DATA_PURPOSE */
  DB_VOLTYPE voltype;		/* Permanent of temporary volume type */
  bool overwrite;
};

typedef enum
{
  DB_PARTITION_HASH = 0,
  DB_PARTITION_RANGE,
  DB_PARTITION_LIST
} DB_PARTITION_TYPE;

typedef enum
{
  DB_NOT_PARTITIONED_CLASS = 0,
  DB_PARTITIONED_CLASS = 1,
  DB_PARTITION_CLASS = 2
} DB_CLASS_PARTITION_TYPE;

#endif /* _DBDEF_H_ */
