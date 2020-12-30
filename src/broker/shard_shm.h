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
 * shard_shm.h - shard shm information
 */

#ifndef	_SHARD_SHM_H_
#define	_SHARD_SHM_H_

#ident "$Id$"


#include "config.h"

#include "broker_config.h"
#include "broker_shm.h"
#include "shard_proxy_common.h"

extern T_PROXY_INFO *shard_shm_find_proxy_info (T_SHM_PROXY * proxy_p, int proxy_id);
extern T_SHARD_INFO *shard_shm_find_shard_info (T_PROXY_INFO * proxy_info_p, int shard_id);
extern T_CLIENT_INFO *shard_shm_get_client_info (T_PROXY_INFO * proxy_info_p, int idx);
extern T_APPL_SERVER_INFO *shard_shm_get_as_info (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p,
						  int shard_id, int as_id);
extern bool shard_shm_set_as_client_info (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p, int shard_id,
					  int as_id, unsigned int ip_addr, char *driver_info, char *driver_version);
extern bool shard_shm_set_as_client_info_with_db_param (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p,
							int shard_id, int as_id, T_CLIENT_INFO * client_info_p);

extern T_SHM_SHARD_CONN_STAT *shard_shm_get_shard_stat (T_PROXY_INFO * proxy_info_p, int idx);
extern T_SHM_SHARD_KEY_STAT *shard_shm_get_key_stat (T_PROXY_INFO * proxy_info_p, int idx);

extern T_SHM_PROXY *shard_shm_initialize_shm_proxy (T_BROKER_INFO * br_info);

extern void shard_shm_dump_appl_server_internal (FILE * fp, T_SHM_PROXY * shm_as_p);
extern void shard_shm_dump_appl_server (FILE * fp, int shmid);

/* SHARD TODO : MV OTHER HEADER FILE */

extern void shard_shm_init_client_info (T_CLIENT_INFO * client_info_p);
extern void shard_shm_init_client_info_request (T_CLIENT_INFO * client_info_p);
extern void shard_shm_set_client_info_request (T_CLIENT_INFO * client_info_p, int func_code);
extern void shard_shm_set_client_info_response (T_CLIENT_INFO * client_info_p);

#endif /* _SHARD_SHM_H_ */
