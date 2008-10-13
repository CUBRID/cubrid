/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * admin_pub.h - 
 */

#ifndef	_ADMIN_PUB_H_
#define	_ADMIN_PUB_H_

#ident "$Id$"

#include "br_config.h"

#define		ADM_PERMANENT_KEY	"KcomKang"

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
int admin_get_broker_status (int, char *);
int admin_broker_job_first_cmd (int, char *, int);
int admin_broker_conf_change (int, char *, char *, char *);
int admin_del_cas_log (int master_shmid, char *broker, int asid);

void admin_init_env (void);

extern char admin_err_msg[];

#endif /* _ADMIN_PUB_H_ */
