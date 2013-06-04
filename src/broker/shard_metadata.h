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


extern int shard_metadata_initialize (T_BROKER_INFO * br_info,
				      T_SHM_PROXY * shm_proxy_p);

extern T_SHM_SHARD_USER *shard_metadata_get_user (T_SHM_PROXY * shm_proxy_p);
extern T_SHM_SHARD_KEY *shard_metadata_get_key (T_SHM_PROXY * shm_proxy_p);
extern T_SHM_SHARD_CONN *shard_metadata_get_conn (T_SHM_PROXY * shm_proxy_p);
extern void shard_metadata_dump_internal (FILE * fp,
					  T_SHM_PROXY * shm_proxy_p);
extern void shard_metadata_dump (FILE * fp, int shmid);

extern T_SHARD_KEY *shard_metadata_bsearch_key (T_SHM_SHARD_KEY * shm_key_p,
						const char *keycolumn);
extern T_SHARD_KEY_RANGE *shard_metadata_bsearch_range (T_SHARD_KEY * key_p,
							unsigned int
							hash_res);
extern
  T_SHARD_KEY_RANGE *shard_metadata_find_shard_range (T_SHM_SHARD_KEY *
						      shm_key_p,
						      const char *key,
						      unsigned int hash_res);
extern T_SHARD_USER *shard_metadata_get_shard_user (T_SHM_SHARD_USER *
						    shm_user_p);
extern T_SHARD_USER *shard_metadata_get_shard_user_from_shm (T_SHM_PROXY *
							     shm_proxy_p);

extern int load_shard_key_function (const char *library_name,
				    const char *function_name);
extern void close_shard_key_function (void);
#endif /* _SHARD_METADATA_H_ */
