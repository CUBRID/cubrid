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
 * es_posix.h - header for posix fs supports (at server)
 */

#ifndef _ES_POSIX_H_
#define _ES_POSIX_H_

#include <sys/types.h>

#include "es_common.h"

#if defined (WINDOWS)
#define ES_PATH_SEPARATOR    "\\"
#else
#define ES_PATH_SEPARATOR    "/"
#endif

#define ES_POSIX_HASH1		(769)
#define ES_POSIX_HASH2		(381)

extern int es_posix_init (const char *base_path);
extern void es_posix_final (void);

#if defined (SA_MODE) || defined (SERVER_MODE)
extern int xes_posix_create_file (char *new_path);
extern ssize_t xes_posix_write_file (const char *path, const void *buf,
				     size_t count, off_t offset);
extern ssize_t xes_posix_read_file (const char *path, void *buf, size_t count,
				    off_t offset);
extern int xes_posix_delete_file (const char *path);
extern int xes_posix_copy_file (const char *src_path, char *metaname,
				char *new_path);
extern int xes_posix_rename_file (const char *src_path, const char *metaname,
				  char *new_path);
extern off_t xes_posix_get_file_size (const char *path);
#endif /* SA_MODE || SERVER_MODE */

extern int es_local_read_file (const char *path, void *buf, size_t count,
			       off_t offset);
extern off_t es_local_get_file_size (const char *path);

#endif /* _ES_POSIX_H_ */
