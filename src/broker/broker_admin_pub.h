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

int admin_isstarted_cmd (int);
int admin_start_cmd (T_BROKER_INFO *, int, int);
int admin_stop_cmd (int);
int admin_add_cmd (int, char *);
int admin_restart_cmd (int, char *, int);
int admin_drop_cmd (int, char *);
int admin_broker_on_cmd (int, char *);
int admin_broker_off_cmd (int, char *);
int admin_broker_suspend_cmd (int, char *);
int admin_broker_resume_cmd (int, char *);
int admin_broker_reset_cmd (int, char *);
int admin_broker_info_cmd (int);
int admin_get_broker_status (int, char *);
int admin_broker_job_first_cmd (int, char *, int);
int admin_broker_conf_change (int, char *, char *, char *);
int admin_del_cas_log (int master_shmid, char *broker, int asid);

void admin_init_env (void);

extern char admin_err_msg[];

#endif /* _BROKER_ADMIN_PUB_H_ */
