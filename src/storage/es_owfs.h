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
 * es_owfs.h - header for owfs supports (at client and server)
 */

#ifndef __ES_OWFS_H__
#define __ES_OWFS_H__

#include "es_common.h"
#include "es_list.h"

extern int es_owfs_init (const char *base_path);
extern void es_owfs_final (void);

extern int es_owfs_create_file (char *new_path);
extern ssize_t es_owfs_write_file (const char *path, const void *buf, size_t count, off_t offset);
extern ssize_t es_owfs_read_file (const char *path, void *buf, size_t count, off_t offset);
extern int es_owfs_delete_file (const char *path);
extern int es_owfs_copy_file (const char *src_path, const char *metaname, char *new_path);
extern int es_owfs_rename_file (const char *src_path, const char *metaname, char *new_path);
extern off_t es_owfs_get_file_size (const char *path);

#endif /* _ES_OWFS_H_ */
