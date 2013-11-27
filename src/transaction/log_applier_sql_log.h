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
 * log_applier_sql_log.h - Definitions for log applier's SQL logging module
 */

#ifndef LOG_APPLIER_SQL_LOG_H_
#define LOG_APPLIER_SQL_LOG_H_

#ident "$Id$"

#include "dbdef.h"
#include "dbtype.h"
#include "work_space.h"

extern int sl_write_schema_sql (char *class_name, char *db_user,
				int item_type, char *ddl, char *ha_sys_prm);
extern int sl_write_insert_sql (DB_OTMPL * inst_tp, DB_VALUE * key);
extern int sl_write_update_sql (DB_OTMPL * inst_tp, DB_VALUE * key);
extern int sl_write_delete_sql (char *class_name, MOBJ mclass,
				DB_VALUE * key);
extern int sl_init (const char *db_name, const char *repl_log_path);

#endif /* LOG_APPLIER_SQL_LOG_H_ */
