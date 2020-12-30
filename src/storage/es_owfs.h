/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
