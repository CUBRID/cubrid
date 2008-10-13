/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * diag_util.h - 
 */

#ifndef _DIAG_UTIL_H_
#define _DIAG_UTIL_H_

#ident "$Id$"

#include "dbmt_config.h"
#include "perf_monitor.h"

extern int init_diag_server_value (T_DIAG_MONITOR_DB_VALUE * server_value);
extern int init_diag_cas_value (T_DIAG_MONITOR_CAS_VALUE * cas_value);
extern int uReadDiagSystemConfig (DIAG_SYS_CONFIG * config, char *err_buf);
extern int init_monitor_config (T_CLIENT_MONITOR_CONFIG * c_config);
extern int init_cas_monitor_config (MONITOR_CAS_CONFIG * c_cas);
extern int init_server_monitor_config (MONITOR_SERVER_CONFIG * c_server);

#endif /* _DIAG_UTIL_H_ */
