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
 * broker_admin_pub.h -
 */

#ifndef	_BROKER_ADMIN_PUB_H_
#define	_BROKER_ADMIN_PUB_H_

#ident "$Id$"

#include "broker_config.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#include "shard_metadata.h"
#include "shard_shm.h"
#include "shard_key_func.h"

#if defined (ENABLE_UNUSED_FUNCTION)
int admin_isstarted_cmd (int);
#endif

int admin_start_cmd (T_BROKER_INFO *, int, int, bool, char *, char *);
int admin_stop_cmd (int);
int admin_add_cmd (int, const char *);
int admin_restart_cmd (int, const char *, int);
int admin_drop_cmd (int, const char *);
int admin_on_cmd (int, const char *);
int admin_off_cmd (int, const char *);
int admin_reset_cmd (int, const char *);
int admin_info_cmd (int);
int admin_conf_change (int, const char *, const char *, const char *, int);
int admin_getid_cmd (int, int, const char **);
int admin_del_cas_log (int master_shmid, const char *broker, int asid);
int admin_acl_status_cmd (int master_shm_id, const char *broker_name);
int admin_acl_reload_cmd (int master_shm_id, const char *broker_name);


void admin_init_env (void);

extern char admin_err_msg[];

#endif /* _BROKER_ADMIN_PUB_H_ */
