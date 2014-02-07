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
 * show_meta.h -  include basic enum and structure for show statements.
 */

#ifndef _SHOW_META_H_
#define _SHOW_META_H_

#ident "$Id$"

#include "config.h"
#include "dbtype.h"
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

typedef PT_NODE *(*SHOW_SEMANTIC_CHECK_FUNC) (PARSER_CONTEXT * parser,
					      PT_NODE * node);

typedef struct showstmt_metadata SHOWSTMT_METADATA;
struct showstmt_metadata
{
  SHOWSTMT_TYPE show_type;
  const char *alias_print;	/* for pt_print_select */
  const SHOWSTMT_COLUMN *cols;	/* SHOWSTMT_COLUMN array */
  int num_cols;			/* size of cols array */
  const SHOWSTMT_COLUMN_ORDERBY *orderby;	/* SHOWSTMT_COLUMN array */
  int num_orderby;		/* size of orderby array */
  SHOWSTMT_NAMED_ARG *args;	/* argument rule */
  int arg_size;			/* size of args array */
  SHOW_SEMANTIC_CHECK_FUNC semantic_check_func;	/* semantic check function
						   pointer */
  DB_ATTRIBUTE *showstmt_attrs;	/* the attribute list */
};

extern int showstmt_metadata_init (void);
extern void showstmt_metadata_final (void);
extern const SHOWSTMT_METADATA *showstmt_get_metadata (SHOWSTMT_TYPE
						       show_type);
extern DB_ATTRIBUTE *showstmt_get_attributes (SHOWSTMT_TYPE show_type);
#endif

#endif /* _SHOW_META_H_ */
