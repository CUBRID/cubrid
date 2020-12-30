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
 * cas_sql_log2.h -
 */

#ifndef	_CAS_SQL_LOG2_H_
#define	_CAS_SQL_LOG2_H_

#ident "$Id$"

#define SQL_LOG2_NONE		0
#define SQL_LOG2_PLAN		1
#define SQL_LOG2_HISTO		2
#define SQL_LOG2_MAX		(SQL_LOG2_PLAN | SQL_LOG2_HISTO)

#if defined(WINDOWS) || defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL) || defined(LIBCAS_FOR_JSP)
#define SQL_LOG2_EXEC_BEGIN(SQL_LOG2_VALUE, STMT_ID)
#define SQL_LOG2_EXEC_END(SQL_LOG2_VALUE, STMT_ID, RES)
#define SQL_LOG2_COMPILE_BEGIN(SQL_LOG2_VALUE, SQL_STMT)
#define SQL_LOG2_EXEC_APPEND(SQL_LOG2_VALUE, STMT_ID, RES, PLAN_FILE)
#else /* WINDOWS */
#define SQL_LOG2_EXEC_BEGIN(SQL_LOG2_VALUE, STMT_ID)		\
	do {							\
	  if (SQL_LOG2_VALUE) {					\
	    if ((SQL_LOG2_VALUE) & SQL_LOG2_PLAN) {		\
	      set_optimization_level(513);			\
	    }							\
	    sql_log2_write("execute %d", STMT_ID);		\
	    if ((SQL_LOG2_VALUE) & SQL_LOG2_HISTO) {		\
	      histo_clear();					\
	    }							\
	    sql_log2_dup_stdout();				\
	  }							\
	} while (0)

#define SQL_LOG2_EXEC_END(SQL_LOG2_VALUE, STMT_ID, RES)		\
	do {							\
	  if (SQL_LOG2_VALUE) {					\
	    if ((SQL_LOG2_VALUE) & SQL_LOG2_HISTO) {		\
	      histo_print(NULL);				\
	    }							\
	    printf("\n");					\
	    sql_log2_flush();					\
	    sql_log2_restore_stdout();				\
	    sql_log2_write("execute %d : %d", STMT_ID, RES);	\
	    reset_optimization_level_as_saved();		\
	  }							\
	} while (0)

#define SQL_LOG2_COMPILE_BEGIN(SQL_LOG2_VALUE, SQL_STMT)	\
	do {							\
	  if (SQL_LOG2_VALUE) {			\
	    sql_log2_write("compile :  %s", SQL_STMT);		\
	  }							\
	} while (0)

#define SQL_LOG2_EXEC_APPEND(SQL_LOG2_VALUE, STMT_ID, RES, PLAN_FILE) \
	do {							\
	  if (SQL_LOG2_VALUE) {					\
	    sql_log2_write("execute %d", STMT_ID);		\
	    sql_log2_append_file(PLAN_FILE);			\
	    sql_log2_write("execute %d : %d", STMT_ID, RES);	\
	  }							\
	} while (0)
#endif /* !WINDOWS */

extern void sql_log2_init (char *br_name, int index, int sql_log2_value, bool log_reuse_flag);
extern char *sql_log2_get_filename (void);
extern void sql_log2_dup_stdout (void);
extern void sql_log2_restore_stdout (void);
extern void sql_log2_end (bool reset_filename_flag);
extern void sql_log2_flush (void);
extern void sql_log2_write (const char *fmt, ...);
extern void sql_log2_append_file (char *file_name);

#endif /* _CAS_SQL_LOG2_H_ */
