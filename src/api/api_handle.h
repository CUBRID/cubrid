/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * api_handle.h -
 */

#ifndef _API_HANDLE_H_
#define _API_HANDLE_H_

#include "config.h"

typedef UINT64 BIND_HANDLE;
typedef struct bh_interface_s BH_INTERFACE;
typedef struct bh_bind_s BH_BIND;
typedef void (*bh_destroyf) (BH_BIND * bind);
typedef int (*bh_mapf) (BH_INTERFACE * ifs, BH_BIND * bind, void *arg);
typedef struct bh_provider_s bh_provider;

typedef enum
{
  BH_ROOT_TYPE_STATIC_HASH_64 = 0,
  BH_ROOT_TYPE_STATIC_HASH_128 = 1,
  BH_ROOT_TYPE_STATIC_HASH_1024 = 2,
  BH_ROOT_TYPE_EXTENDIBLE_HASH = 3,	/* not implemented */
  BH_ROOT_TYPE_RB_TREE = 4	/* not implemented */
} BH_ROOT_TYPE;

/* 
 * basic bind structure corresponding a handle.
 * user struct should extend (first member) this structure 
 */
struct bh_bind_s
{
  bh_destroyf dtor;		/* user supplied */
  void *bptr;			/* back pointer. DO NOT TOUCH THIS */
};

/*
 * abstract structure that provides functionality related with 
 * handle management.
 */
struct bh_interface_s
{
  int (*alloc_handle) (BH_INTERFACE * ifs, BH_BIND * bind, BIND_HANDLE * bh);
  int (*destroy_handle) (BH_INTERFACE * ifs, BIND_HANDLE bh);
  int (*lookup) (BH_INTERFACE * ifs, BIND_HANDLE bh, BH_BIND ** bind);
  int (*bind_to_handle) (BH_INTERFACE * ifs, BH_BIND * bind,
			 BIND_HANDLE * bh);
  int (*bind_get_parent) (BH_INTERFACE * ifs, BH_BIND * bind,
			  BH_BIND ** pbind);
  int (*bind_prune) (BH_INTERFACE * ifs, BH_BIND * bind);
  int (*bind_graft) (BH_INTERFACE * ifs, BH_BIND * bind, BH_BIND * on_bind);
  int (*bind_get_first_child) (BH_INTERFACE * ifs, BH_BIND * bind,
			       BH_BIND ** pchild);
  int (*bind_get_next_sibling) (BH_INTERFACE * ifs, BH_BIND * bind,
				BH_BIND ** psibling);
  int (*bind_map) (BH_INTERFACE * ifs, BH_BIND * bind, bh_mapf mf, void *arg);
  void (*destroy) (BH_INTERFACE * ifs);
};


/*
 * next handle value provider
 */
struct bh_provider_s
{
  int (*next_handle) (bh_provider * p, BIND_HANDLE * rv);
};

/*
 * Exported functions.
 *
 * A user first acquire root id using bh_root_acquire() and get 
 * BH_INTERFACE by calling bh_root_lock().
 * BH_INTERFACE is MT safe until the root id is unlocked by calling
 * bh_root_unlock() 
 */
extern int bh_root_acquire (int *rid, BH_ROOT_TYPE rt);
extern int bh_root_release (int rid);
extern int bh_root_lock (int rid, BH_INTERFACE ** ifs);
extern int bh_root_unlock (int rid);
extern int bh_get_rid (BIND_HANDLE bh, int *rid);
extern int create_handle_context (bh_provider * prov, BH_ROOT_TYPE rt,
				  BH_INTERFACE ** ifs);
#endif /* _API_HANDLE_H_ */
