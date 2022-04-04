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
 * log_applier_sql_log.h - Definitions for log applier's SQL logging module
 */

#ifndef LOG_APPLIER_SQL_LOG_H_
#define LOG_APPLIER_SQL_LOG_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "work_space.h"

#define CA_MARK_TRAN_START      "/* TRAN START */"
#define CA_MARK_TRAN_END        "/* TRAN END */"

extern int sl_write_statement_sql (char *class_name, char *db_user, int item_type, const char *ddl, char *ha_sys_prm);
extern int sl_write_insert_sql (DB_OTMPL * inst_tp, DB_VALUE * key);
extern int sl_write_update_sql (DB_OTMPL * inst_tp, DB_VALUE * key);
extern int sl_write_delete_sql (char *class_name, MOBJ mclass, DB_VALUE * key);
extern int sl_init (const char *db_name, const char *repl_log_path);

#endif /* LOG_APPLIER_SQL_LOG_H_ */
