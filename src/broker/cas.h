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
 * cas.h -
 */

#ifndef	_CAS_H_
#define	_CAS_H_

#ident "$Id$"

#ifndef CAS
#define CAS
#endif

#include "broker_shm.h"
#include "cas_protocol.h"
#include "cas_cci.h"
#include "cas_common.h"


/* server error code */
#define CAS_ER_GLO			-999
#define CAS_ER_GLO_CMD			-1023
#define CAS_ER_NOT_IMPLEMENTED		-1100

typedef struct t_object T_OBJECT;
struct t_object
{
  int pageid;
  short slotid;
  short volid;
};

typedef struct t_req_info T_REQ_INFO;
struct t_req_info
{
  T_BROKER_VERSION client_version;
  char need_rollback;
};

#ifndef LIBCAS_FOR_JSP
extern int restart_is_needed (void);
#endif

extern char broker_name[32];
extern int shm_as_index;
extern T_SHM_APPL_SERVER *shm_appl;
extern char sql_log_mode;
extern T_TIMEVAL tran_start_time;

extern char stripped_column_name;
extern char cas_client_type;

extern int cas_default_isolation_level;
extern int cas_default_lock_timeout;
extern int cas_send_result_flag;

extern int xa_prepare_flag;

#endif /* _CAS_H_ */
