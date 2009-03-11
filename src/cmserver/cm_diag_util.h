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
 * cm_diag_util.h - 
 */

#ifndef _CM_DIAG_UTIL_H_
#define _CM_DIAG_UTIL_H_

#ident "$Id$"

#include "cm_config.h"
#include "perf_monitor.h"

extern int init_diag_server_value (T_DIAG_MONITOR_DB_VALUE * server_value);
extern int init_diag_cas_value (T_DIAG_MONITOR_CAS_VALUE * cas_value);
extern int uReadDiagSystemConfig (DIAG_SYS_CONFIG * config, char *err_buf);
extern int init_monitor_config (T_CLIENT_MONITOR_CONFIG * c_config);
extern int init_cas_monitor_config (MONITOR_CAS_CONFIG * c_cas);
extern int init_server_monitor_config (MONITOR_SERVER_CONFIG * c_server);

#endif /* _CM_DIAG_UTIL_H_ */
