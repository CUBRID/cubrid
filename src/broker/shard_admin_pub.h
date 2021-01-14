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
 * shard_admin_pub.h -
 */
#if defined(UNDEFINED)

#ifndef	_SHARD_ADMIN_PUB_H_
#define	_SHARD_ADMIN_PUB_H_

#ident "$Id$"

#include "broker_config.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern int shard_broker_activate (int master_shm_id, T_BROKER_INFO * br_info_p, T_SHM_APPL_SERVER * shm_as_p);
void shard_broker_inactivate (T_BROKER_INFO * br_info_p);

extern int shard_as_activate (int as_shm_id, int proxy_id, int shard_id, int as_id, T_SHM_APPL_SERVER * shm_as_p,
			      T_SHM_PROXY * shm_proxy_p);
extern void shard_as_inactivate (T_BROKER_INFO * br_info_p, T_APPL_SERVER_INFO * as_info_p, int proxy_index,
				 int shard_index, int as_index);

extern int shard_process_activate (int master_shm_id, T_BROKER_INFO * br_info, T_SHM_APPL_SERVER * shm_as_p,
				   T_SHM_PROXY * shm_proxy_p);

extern void shard_process_inactivate (T_BROKER_INFO * br_info_p);

#endif /* _SHARD_ADMIN_PUB_H_ */
#endif /* UNDEFINED */
