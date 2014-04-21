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
 * es_common.h - header for external storage API supports (at client and server)
 */

#ifndef _ES_COMMON_H_
#define _ES_COMMON_H_

typedef enum
{
  ES_NONE = -1,
  ES_OWFS = 0,
  ES_POSIX = 1,
  ES_LOCAL = 2
} ES_TYPE;

#define ES_OWFS_PATH_PREFIX     "owfs:"
#define ES_POSIX_PATH_PREFIX    "file:"
#define ES_LOCAL_PATH_PREFIX    "local:"

#define ES_OWFS_PATH_POS(uri)	((uri) + sizeof(ES_OWFS_PATH_PREFIX) - 1)
#define ES_POSIX_PATH_POS(uri)	((uri) + sizeof(ES_POSIX_PATH_PREFIX) - 1)
#define ES_LOCAL_PATH_POS(uri)	((uri) + sizeof(ES_LOCAL_PATH_PREFIX) - 1)

extern ES_TYPE es_get_type (const char *uri);
extern const char *es_get_type_string (ES_TYPE type);
extern unsigned int es_name_hash_func (int size, const char *name);
extern UINT64 es_get_unique_num (void);

#endif /* _ES_COMMON_H_ */
