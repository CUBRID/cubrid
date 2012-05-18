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
 * shard_shm.h - shard shm information
 */

#ifndef	_SHARD_SHM_H_
#define	_SHARD_SHM_H_

#ident "$Id$"


#include "config.h"

#include "broker_config.h"
#include "broker_shm.h"

extern T_SHM_APPL_SERVER *shard_shm_get_appl_server (char *shm_appl_svr_p);
extern T_SHM_PROXY *shard_shm_get_proxy (char *shm_appl_svr_p);
extern T_PROXY_INFO *shard_shm_get_first_proxy_info (T_SHM_PROXY *
						     shm_proxy_p);
extern T_PROXY_INFO *shard_shm_get_next_proxy_info (T_PROXY_INFO *
						    curr_proxy_p);
extern T_PROXY_INFO *shard_shm_get_proxy_info (char *shm_as_cp, int proxy_id);
extern T_SHARD_INFO *shard_shm_get_first_shard_info (T_PROXY_INFO *
						     proxy_info_p);
extern T_SHARD_INFO *shard_shm_get_next_shard_info (T_SHARD_INFO *
						    curr_shard_p);
extern T_SHARD_INFO *shard_shm_find_shard_info (T_PROXY_INFO * proxy_info_p,
						int shard_id);
extern T_CLIENT_INFO *shard_shm_get_first_client_info (T_PROXY_INFO *
						       proxy_info_p);
extern T_CLIENT_INFO *shard_shm_get_next_client_info (T_CLIENT_INFO *
						      curr_client_p);
extern T_CLIENT_INFO *shard_shm_get_client_info (T_PROXY_INFO * proxy_info_p,
						 int idx);
extern T_APPL_SERVER_INFO *shard_shm_get_as_info (T_PROXY_INFO * proxy_info_p,
						  int shard_id, int as_id);

extern T_SHM_SHARD_CONN_STAT *shard_shm_get_first_shard_stat (T_PROXY_INFO *
							      proxy_info_p);
extern T_SHM_SHARD_CONN_STAT *shard_shm_get_shard_stat (T_PROXY_INFO *
							proxy_info_p,
							int idx);
extern T_SHM_SHARD_KEY_STAT *shard_shm_get_first_key_stat (T_PROXY_INFO *
							   proxy_info_p);
extern T_SHM_SHARD_KEY_STAT *shard_shm_get_key_stat (T_PROXY_INFO *
						     proxy_info_p, int idx);

extern char *shard_shm_initialize (T_BROKER_INFO * br_info,
				   char *shm_metadata_p);
extern void shard_shm_dump_appl_server_internal (FILE * fp,
						 char *shm_appl_svr_p);
extern void shard_shm_dump_appl_server (FILE * fp, int shmid);

/* SHARD TODO : MV OTHER HEADER FILE */
extern char **make_env (char *env_file, int *env_num);
extern const char *get_appl_server_name (int appl_server_type, char **env,
					 int env_num);

extern void shard_shm_init_client_info (T_CLIENT_INFO * client_info_p);
extern void shard_shm_init_client_info_request (T_CLIENT_INFO *
						client_info_p);
extern void shard_shm_set_client_info_request (T_CLIENT_INFO * client_info_p,
					       int func_code);
extern void shard_shm_set_client_info_response (T_CLIENT_INFO *
						client_info_p);

#endif /* _SHARD_SHM_H_ */
