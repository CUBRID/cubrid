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

enum tran_auto_commit {
 TRAN_NOT_AUTOCOMMIT = 0,
 TRAN_AUTOCOMMIT = 1,
 TRAN_AUTOROLLBACK = 2
};

typedef struct t_req_info T_REQ_INFO;
struct t_req_info
{
  T_BROKER_VERSION client_version;
  enum tran_auto_commit need_auto_commit;
  char need_rollback;
};

#ifndef LIBCAS_FOR_JSP
extern int restart_is_needed (void);
extern bool is_server_aborted;

extern const char *program_name;
extern char broker_name[32];

extern int shm_as_index;
extern T_SHM_APPL_SERVER *shm_appl;
extern T_APPL_SERVER_INFO *as_info;

extern struct timeval tran_start_time;
extern struct timeval query_start_time;
extern int tran_timeout;
extern int query_timeout;
#endif

extern int errors_in_transaction;
extern char stripped_column_name;
extern char cas_client_type;

extern int cas_default_isolation_level;
extern int cas_default_lock_timeout;
extern int cas_send_result_flag;
extern int cas_info_size;

extern int xa_prepare_flag;

#endif /* _CAS_H_ */
