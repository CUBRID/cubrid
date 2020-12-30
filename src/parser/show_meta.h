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
 * show_meta.h -  include basic enum and structure for show statements.
 */

#ifndef _SHOW_META_H_
#define _SHOW_META_H_

#ident "$Id$"

#include "config.h"
#include "dbtype_def.h"
#include "parse_tree.h"

#if !defined(SERVER_MODE)
typedef struct showstmt_column SHOWSTMT_COLUMN;
struct showstmt_column
{
  const char *name;		/* column name of show statement */
  const char *type;		/* colume type */
};

typedef struct showstmt_column_orderby SHOWSTMT_COLUMN_ORDERBY;
struct showstmt_column_orderby
{
  int pos;			/* order by position, start from 1 */
  bool asc;			/* true for PT_ASC */
};

typedef enum
{
  AVT_INTEGER,			/* argument value type of integer */
  AVT_STRING,			/* argument value type of string */
  AVT_IDENTIFIER,		/* argument value type of identifier */
} ARG_VALUE_TYPE;

typedef struct showstmt_named_arg SHOWSTMT_NAMED_ARG;
struct showstmt_named_arg
{
  const char *name;		/* name of argument should be used in syntax */
  ARG_VALUE_TYPE type;		/* expected type of argument value */
  bool optional;		/* whether or not omit */
};

typedef PT_NODE *(*SHOW_SEMANTIC_CHECK_FUNC) (PARSER_CONTEXT * parser, PT_NODE * node);

typedef struct showstmt_metadata SHOWSTMT_METADATA;
struct showstmt_metadata
{
  SHOWSTMT_TYPE show_type;
  bool only_for_dba;		/* dba is only allowed */
  const char *alias_print;	/* for pt_print_select */
  const SHOWSTMT_COLUMN *cols;	/* SHOWSTMT_COLUMN array */
  int num_cols;			/* size of cols array */
  const SHOWSTMT_COLUMN_ORDERBY *orderby;	/* SHOWSTMT_COLUMN array */
  int num_orderby;		/* size of orderby array */
  const SHOWSTMT_NAMED_ARG *args;	/* argument rule */
  int arg_size;			/* size of args array */
  SHOW_SEMANTIC_CHECK_FUNC semantic_check_func;	/* semantic check function pointer */
  DB_ATTRIBUTE *showstmt_attrs;	/* the attribute list */
};

extern int showstmt_metadata_init (void);
extern void showstmt_metadata_final (void);
extern const SHOWSTMT_METADATA *showstmt_get_metadata (SHOWSTMT_TYPE show_type);
extern DB_ATTRIBUTE *showstmt_get_attributes (SHOWSTMT_TYPE show_type);
#endif

#endif /* _SHOW_META_H_ */
