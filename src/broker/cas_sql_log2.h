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
 * cas_sql_log2.h -
 */

#ifndef	_CAS_SQL_LOG2_H_
#define	_CAS_SQL_LOG2_H_

#ident "$Id$"

#define SQL_LOG2_NONE		0
#define SQL_LOG2_PLAN		1
#define SQL_LOG2_HISTO		2
#define SQL_LOG2_MAX		(SQL_LOG2_PLAN | SQL_LOG2_HISTO)

#if defined(UNITCLSH) || defined(WINDOWS)
#define SQL_LOG2_EXEC_BEGIN(SQL_LOG2_VALUE, STMT_ID)
#define SQL_LOG2_EXEC_END(SQL_LOG2_VALUE, STMT_ID, RES)
#define SQL_LOG2_COMPILE_BEGIN(SQL_LOG2_VALUE, SQL_STMT)
#define SQL_LOG2_EXEC_APPEND(SQL_LOG2_VALUE, STMT_ID, RES, PLAN_FILE, HISTO_FILE)
#else
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
	      histo_print();					\
	    }							\
	    printf("\n");					\
	    sql_log2_flush();					\
	    sql_log2_restore_stdout();				\
	    sql_log2_write("execute %d : %d", STMT_ID, RES);	\
	    set_optimization_level(1);				\
	  }							\
	} while (0)

#define SQL_LOG2_COMPILE_BEGIN(SQL_LOG2_VALUE, SQL_STMT)	\
	do {							\
	  if (SQL_LOG2_VALUE) {			\
	    sql_log2_write("compile :  %s", SQL_STMT);		\
	  }							\
	} while (0)

#define SQL_LOG2_EXEC_APPEND(SQL_LOG2_VALUE, STMT_ID, RES, PLAN_FILE, HISTO_FILE) \
	do {							\
	  if (SQL_LOG2_VALUE) {					\
	    sql_log2_write("execute %d", STMT_ID);		\
	    sql_log2_append_file(PLAN_FILE);			\
	    if ((SQL_LOG2_VALUE) & SQL_LOG2_HISTO) {		\
	      sql_log2_append_file(HISTO_FILE);			\
	      sql_log2_write("\n");				\
	    }							\
	    sql_log2_write("execute %d : %d", STMT_ID, RES);	\
	  }							\
	} while (0)
#endif

extern void sql_log2_init (char *br_name, int index, int sql_log2_value,
			   char log_reuse_flag);
extern char *sql_log2_get_filename (void);
extern void sql_log2_dup_stdout (void);
extern void sql_log2_restore_stdout (void);
extern void sql_log2_end (char reset_filename_flag);
extern void sql_log2_flush (void);
extern void sql_log2_write (const char *fmt, ...);
extern void sql_log2_append_file (char *file_name);
extern void set_optimization_level (int level);

#endif /* _CAS_SQL_LOG2_H_ */
