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
 * es_common.h - header for external storage API supports (at client and server)
 */

#ifndef _ES_COMMON_H_
#define _ES_COMMON_H_

#include "system.h"

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

// note - to use, one must include error_manager.h & system_parameter.h
#define es_log(...) if (prm_get_bool_value (PRM_ID_DEBUG_ES)) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)

extern ES_TYPE es_get_type (const char *uri);
extern const char *es_get_type_string (ES_TYPE type);
extern unsigned int es_name_hash_func (int size, const char *name);
extern UINT64 es_get_unique_num (void);

#endif /* _ES_COMMON_H_ */
