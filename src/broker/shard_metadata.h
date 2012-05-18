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

/* for dbshard phase0 : will be removed */
#define MAX_SHARD_USER	4
#define MAX_SHARD_KEY	1
#define MAX_SHARD_CONN	2

#define SHARD_KEY_COLUMN_LEN		(32)
#define SHARD_KEY_RANGE_MAX		(256)

extern FN_GET_SHARD_KEY fn_get_shard_key;

/* SHARD USER */
typedef struct t_shard_user T_SHARD_USER;
struct t_shard_user
{
  char db_name[SRV_CON_DBNAME_SIZE];
  char db_user[SRV_CON_DBUSER_SIZE];
  char db_password[SRV_CON_DBPASSWD_SIZE];
};

typedef struct t_shm_shard_user T_SHM_SHARD_USER;
struct t_shm_shard_user
{
  int num_shard_user;
  T_SHARD_USER shard_user[1];
};

/* SHARD KEY */
typedef struct t_shard_key_range T_SHARD_KEY_RANGE;
struct t_shard_key_range
{
  int key_index;
  int range_index;

  int min;
  int max;
  int shard_id;
};

typedef struct t_shard_key T_SHARD_KEY;
struct t_shard_key
{
  char key_column[SHARD_KEY_COLUMN_LEN];
  int num_key_range;
  T_SHARD_KEY_RANGE range[SHARD_KEY_RANGE_MAX];
};

typedef struct t_shm_shard_key T_SHM_SHARD_KEY;
struct t_shm_shard_key
{
  int num_shard_key;
  T_SHARD_KEY shard_key[1];
};

/* SHARD CONN */
typedef struct t_shard_conn T_SHARD_CONN;
struct t_shard_conn
{
  int shard_id;
  char db_name[SRV_CON_DBNAME_SIZE];
  char db_conn_info[LINE_MAX];
};

typedef struct t_shm_shard_conn T_SHM_SHARD_CONN;
struct t_shm_shard_conn
{
  int num_shard_conn;
  T_SHARD_CONN shard_conn[1];
};

extern char *shard_metadata_initialize (T_BROKER_INFO * br_info);
extern T_SHM_SHARD_USER *shard_metadata_get_user (char *shm_metadata_p);
extern T_SHM_SHARD_KEY *shard_metadata_get_key (char *shm_metadata_p);
extern T_SHM_SHARD_CONN *shard_metadata_get_conn (char *shm_metadata_p);
extern int shard_metadata_conn_count (char *shm_metadata_p);
extern void shard_metadata_dump_internal (FILE * fp, char *shm_metadata_p);
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

extern int load_shard_key_function (const char *library_name,
				    const char *function_name);
extern void close_shard_key_function (void);
#endif /* _SHARD_METADATA_H_ */
