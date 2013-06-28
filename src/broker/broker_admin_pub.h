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

int admin_start_cmd (T_BROKER_INFO *, int, int, bool, char *);
int admin_stop_cmd (int);
int admin_add_cmd (int, const char *);
int admin_restart_cmd (int, const char *, int);
int admin_drop_cmd (int, const char *);
int admin_on_cmd (int, const char *);
int admin_off_cmd (int, const char *);
int admin_broker_suspend_cmd (int, const char *);
int admin_broker_resume_cmd (int, const char *);
int admin_reset_cmd (int, const char *);
int admin_info_cmd (int);
int admin_get_broker_status (int, const char *);
int admin_broker_job_first_cmd (int, const char *, int);
int admin_conf_change (int, const char *, const char *, const char *, int);
int admin_getid_cmd (int, int, const char **);
int admin_del_cas_log (int master_shmid, const char *broker, int asid);
int admin_acl_status_cmd (int master_shm_id, const char *broker_name);
int admin_acl_reload_cmd (int master_shm_id, const char *broker_name);


void admin_init_env (void);

extern char admin_err_msg[];

#endif /* _BROKER_ADMIN_PUB_H_ */
