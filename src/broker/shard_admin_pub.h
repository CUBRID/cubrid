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
 * shard_admin_pub.h -
 */

#ifndef	_SHARD_ADMIN_PUB_H_
#define	_SHARD_ADMIN_PUB_H_

#ident "$Id$"

#include "broker_config.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern int shard_broker_activate (int master_shm_id,
				  T_BROKER_INFO * br_info_p, char *shm_as_cp);
void shard_broker_inactivate (T_BROKER_INFO * br_info_p);

extern int shard_as_activate (int as_shm_id,
			      int proxy_id, int shard_id, int as_id,
			      char *shm_as_cp);
extern void shard_as_inactivate (T_BROKER_INFO * br_info_p,
				 T_APPL_SERVER_INFO * as_info_p,
				 int proxy_index, int shard_index,
				 int as_index);

extern int shard_process_activate (int master_shm_id, T_BROKER_INFO * br_info,
				   T_SHM_BROKER * shm_br, char *shm_as_p);

extern void shard_process_inactivate (T_BROKER_INFO * br_info_p);

#endif /* _SHARD_ADMIN_PUB_H_ */
