/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas.h -
 */

#ifndef	_CAS_H_
#define	_CAS_H_

#ident "$Id$"

#ifndef CAS
#define CAS
#endif

#include "shm.h"
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
