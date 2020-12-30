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
extern ssize_t xes_posix_write_file (const char *path, const void *buf, size_t count, off_t offset);
extern ssize_t xes_posix_read_file (const char *path, void *buf, size_t count, off_t offset);
extern int xes_posix_delete_file (const char *path);
extern int xes_posix_copy_file (const char *src_path, char *metaname, char *new_path);
extern int xes_posix_rename_file (const char *src_path, const char *metaname, char *new_path);
extern off_t xes_posix_get_file_size (const char *path);
#endif /* SA_MODE || SERVER_MODE */

extern int es_local_read_file (const char *path, void *buf, size_t count, off_t offset);
extern off_t es_local_get_file_size (const char *path);

#endif /* _ES_POSIX_H_ */
