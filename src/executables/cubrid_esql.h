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
 *  cubrid_esql.h : CUBRID Embedded C Program Inclusion Header File
 *  This file contains the definitions of type, macros, function prototypes,
 *  and variables which is exported to each ESQL C program file.
 *  Every ESQL C program should include this file.
 */

#ifndef _CUBRID_ESQL_H_
#define _CUBRID_ESQL_H_

#ident "$Id$"

#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(__STDC__) || defined(_cplusplus) || defined(__cplusplus)
#include        "stdlib.h"
#else
#include        <sys/types.h>
#endif
#include        "dbi.h"

#define SQL_NOT_FOUND           100	/* sqlcode for not found */
#define SQL_WARNING_CHAR        'W'	/* to indicate warning */

/* macro to declare a type of CUBRIDDA having the specified # of sqlvars */
#define CUBRIDDA_TEMPLATE(num_vars)    \
                struct {                \
                        int             sqlmax;  \
                        int             sqldesc; \
                        CUBRID_STMT_TYPE   sqlcmd;  \
                        CUBRIDVAR       sqlvar[(num_vars)]; \
                }

/* macro to alloc CUBRIDDA with # of sqlvars */
#define ALLOC_CUBRIDDA(num_vars)            \
     (CUBRIDDA *)malloc(sizeof(CUBRIDDA) + \
       sizeof(CUBRIDVAR) * ((num_vars) - 1))

#define SQLCODE         sqlca.sqlcode
#define SQLFILE         sqlca.sqlfile
#define SQLLINE         sqlca.sqlline
#define SQLERRML        sqlca.sqlerrm.sqlerrml
#define SQLERRMC        sqlca.sqlerrm.sqlerrmc
#define SQLERRD         sqlca.sqlerrd
#define SQLERRD2        sqlca.sqlerrd[2]
#define SQLWARN0        sqlca.sqlwarn.sqlwarn0
#define SQLWARN1        sqlca.sqlwarn.sqlwarn1
#define SQLWARN2        sqlca.sqlwarn.sqlwarn2
#define SQLWARN3        sqlca.sqlwarn.sqlwarn3
#define SQLWARN4        sqlca.sqlwarn.sqlwarn4

  typedef short DB_INDICATOR;

/* decription of a variable to contain a result column */
  typedef struct cubridvar
  {
    DB_TYPE sqltype;		/* type of the SQL variable */
    int sqlprec;		/* precision, when applicable */
    int sqlscale;		/* scale, when applicable */
    DB_TYPE_C sqlctype;		/* type of the buffer area */
    void *sqldata;		/* pointer to the variable */
    int sqllen;			/* length in bytes of the buffer area */
    DB_INDICATOR *sqlind;	/* pointer to the associated indicator */
    char *sqlname;		/* null-terminated result column name */
  } CUBRIDVAR;

/* SQLDA structure */
  typedef struct cubridda
  {
    int sqlmax;			/* # of sqlvar allocated */
    int sqldesc;		/* # of sqlvar described */
    CUBRID_STMT_TYPE sqlcmd;	/* type of csql statement */
    CUBRIDVAR sqlvar[1];	/* array of variable descriptions */
  } CUBRIDDA;

/* SQLCA structure */
  typedef struct cubridca
  {
    char sqlcaid[8];		/* "SQLCA " */
    long sqlcabc;		/* == sizeof(CUBRIDCA) */
    long sqlcode;		/* error code returned from the last command */
    const char *sqlfile;	/* Name of source file in which error occurred */
    long sqlline;		/* Line number within source file */
    struct
    {
      short sqlerrml;		/* length of message in sqlerrmc */
      char sqlerrmc[512];	/* error message text */
    } sqlerrm;
    char sqlerrp[8];		/* unused */
    long sqlerrd[6];		/* sqlerrd[2] indicates the number of objects
				 * affected by the last command. others are
				 * not used.
				 */
    struct
    {
      char sqlwarn0;		/* 'W' if one of sqlwarn[1-7] is 'W' */
      char sqlwarn1;		/* 'W' if output truncation */
      char sqlwarn2;		/* 'W' if NULL in aggregate evaulation
				 * Currently NOT SUPPORTED
				 */
      char sqlwarn3;		/* 'W' if host variables are mismatches */
      char sqlwarn4;		/* 'W' if UPDATE/DELETE without WHERE
				 * Currently NOT SUPPORTED
				 */
      char sqlwarn5;		/* unused */
      char sqlwarn6;		/* unused */
      char sqlwarn7;		/* unused */
    } sqlwarn;
    char sqlext[8];
  } CUBRIDCA;

#if defined(_ESQLX_VARCHAR2_STYLE_)
  typedef struct var_char
  {
    int len;
    char arr[1];
  } VARCHAR;
#else
  typedef struct var_char
  {
    int length;
    char array[1];
  } VARCHAR;
#endif

  typedef VARCHAR varchar;

  extern unsigned int _uci_opt;	/* UCI runtime option */
  extern CUBRIDCA sqlca;	/* user-exported SQLCA variable */
  extern DB_INDICATOR uci_null_ind;

  extern void uci_startup (const char *);
  extern void uci_start (void *, const char *, int, unsigned int);
  extern void uci_end (void);
  extern void uci_stop (void);

  extern void uci_connect (const char *, const char *, const char *);
  extern void uci_disconnect (void);
  extern void uci_commit (void);
  extern void uci_rollback (void);
  extern void uci_static (int, const char *, int, int);
  extern void uci_open_cs (int, const char *, int, int, int);
  extern void uci_fetch_cs (int, int);
  extern void uci_delete_cs (int);
  extern void uci_close_cs (int);
  extern void uci_psh_curr_csr_oid (int);
  extern void uci_prepare (int, const char *, int);
  extern void uci_describe (int, CUBRIDDA *);
  extern void uci_execute (int, int);
  extern void uci_execute_immediate (const char *, int);
  extern void uci_object_describe
    (DB_OBJECT *, int, const char **, CUBRIDDA *);
  extern void uci_object_fetch (DB_OBJECT *, int, const char **, int);

  extern void uci_set_num_db_values (int);
  extern void uci_get_value (int, DB_INDICATOR *, void *,
			     DB_TYPE_C, int, int *);
  extern void uci_get_db_value (int, DB_VALUE *);
  extern void uci_get_descriptor (int, CUBRIDDA *);

  extern void uci_put_value (DB_INDICATOR *,
			     DB_TYPE, int, int, DB_TYPE_C, void *, int);
  extern void uci_put_descriptor (CUBRIDDA *);

/* A static variable to identify each esql C source file at run-time. */
#if !defined(_ESQL_KERNEL_)
  static char uci_esqlxc_file;
#endif

#if defined(__cplusplus)
}
#endif

#endif				/* _CUBRID_ESQL_H_ */
