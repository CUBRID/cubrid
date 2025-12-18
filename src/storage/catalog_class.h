/*
 *
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
 * catalog_class.h
 */

#ifndef _CATALOG_CLASS_H_
#define _CATALOG_CLASS_H_

#include "heap_file.h"
#include "intl_support.h"
#include "language_support.h"
#include "storage_common.h"
#include "thread_compat.hpp"

extern bool catcls_Enable;

int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);
int catcls_insert_catalog_classes (THREAD_ENTRY * thread_p, RECDES * record);
int catcls_delete_catalog_classes (THREAD_ENTRY * thread_p, const char *name, OID * class_oid);
int catcls_update_catalog_classes (THREAD_ENTRY * thread_p, const char *name, RECDES * record, OID * class_oid_p,
				   UPDATE_INPLACE_STYLE force_in_place);
int catcls_finalize_class_oid_to_oid_hash_table (THREAD_ENTRY * thread_p);
int catcls_remove_entry (THREAD_ENTRY * thread_p, OID * class_oid);
int catcls_get_server_compat_info (THREAD_ENTRY * thread_p, INTL_CODESET * charset_id_p, char *lang_buf,
				   const int lang_buf_size, char *timezone_checksum);
int catcls_get_db_collation (THREAD_ENTRY * thread_p, LANG_COLL_COMPAT ** db_collations, int *coll_cnt);
int catcls_get_apply_info_log_record_time (THREAD_ENTRY * thread_p, time_t * log_record_time);
int catcls_find_and_set_cached_class_oid (THREAD_ENTRY * thread_p);
int catcls_update_class_stats (THREAD_ENTRY * thread_p, const char *class_name, unsigned int ci_time_stamp,
			       bool with_fullscan);

#endif /* _CATALOG_CLASS_H_ */
