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
 * shard_metadata.h - shard metadata information
 */

#ifndef	_SHARD_METADATA_H_
#define	_SHARD_METADATA_H_

#ident "$Id$"

#include "config.h"

#include "broker_config.h"
#include "cas_protocol.h"

#include "shard_key.h"

#include "broker_shm.h"

extern FN_GET_SHARD_KEY fn_get_shard_key;


extern int shard_metadata_initialize (T_BROKER_INFO * br_info, T_SHM_PROXY * shm_proxy_p);

extern T_SHM_SHARD_USER *shard_metadata_get_user (T_SHM_PROXY * shm_proxy_p);
extern T_SHM_SHARD_KEY *shard_metadata_get_key (T_SHM_PROXY * shm_proxy_p);
extern T_SHM_SHARD_CONN *shard_metadata_get_conn (T_SHM_PROXY * shm_proxy_p);
extern void shard_metadata_dump_internal (FILE * fp, T_SHM_PROXY * shm_proxy_p);
extern void shard_metadata_dump (FILE * fp, int shmid);

extern T_SHARD_KEY *shard_metadata_bsearch_key (T_SHM_SHARD_KEY * shm_key_p, const char *keycolumn);
extern T_SHARD_KEY_RANGE *shard_metadata_bsearch_range (T_SHARD_KEY * key_p, unsigned int hash_res);
extern T_SHARD_KEY_RANGE *shard_metadata_find_shard_range (T_SHM_SHARD_KEY * shm_key_p, const char *key,
							   unsigned int hash_res);
extern T_SHARD_USER *shard_metadata_get_shard_user (T_SHM_SHARD_USER * shm_user_p);
extern T_SHARD_USER *shard_metadata_get_shard_user_from_shm (T_SHM_PROXY * shm_proxy_p);

extern int load_shard_key_function (const char *library_name, const char *function_name);
extern void close_shard_key_function (void);
#endif /* _SHARD_METADATA_H_ */
