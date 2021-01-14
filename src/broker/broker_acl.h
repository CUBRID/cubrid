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
 * broker_acl.h -
 */

#ifndef	_BROKER_ACL_H_
#define	_BROKER_ACL_H_

#ident "$Id$"

#include "broker_shm.h"

extern int access_control_set_shm (T_SHM_APPL_SERVER * shm_as_cp, T_BROKER_INFO * br_info_p, T_SHM_BROKER * shm_br,
				   char *admin_err_msg);
extern int access_control_read_config_file (T_SHM_APPL_SERVER * shm_appl, char *filename, char *admin_err_msg);
extern int access_control_check_right (T_SHM_APPL_SERVER * shm_as_p, char *dbname, char *dbuser,
				       unsigned char *address);

#endif /* _BROKER_ACL_H_ */
