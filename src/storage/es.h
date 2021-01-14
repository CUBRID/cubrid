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
 * esi.h - header for external storage API  (at client and server)
 */

#ifndef _ES_H_
#define _ES_H_

#include <sys/types.h>

#include "porting.h"
#include "es_common.h"
#include "recovery.h"

#define ES_URI_PREFIX_MAX	8
#define ES_MAX_URI_LEN		(PATH_MAX + ES_URI_PREFIX_MAX)

typedef char ES_URI[ES_MAX_URI_LEN];

/* APIs */
extern int es_init (const char *uri);
extern void es_final (void);
extern int es_create_file (char *out_uri);
extern ssize_t es_write_file (const char *uri, const void *buf, size_t count, off_t offset);
extern ssize_t es_read_file (const char *uri, void *buf, size_t count, off_t offset);
extern int es_delete_file (const char *uri);
extern int es_copy_file (const char *in_uri, const char *metaname, char *out_uri);
extern int es_rename_file (const char *in_uri, const char *metaname, char *out_uri);
extern off_t es_get_file_size (const char *uri);




#endif /* _ES_H_ */
