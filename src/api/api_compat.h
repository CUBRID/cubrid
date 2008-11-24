/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * api_compat.h -
 */

#ifndef _API_COMPAT_H_
#define _API_COMPAT_H_

struct db_session
{
  char *stage;			/* vector of statements' stage */
  char include_oid;		/* NO_OIDS, ROW_OIDS           */
  int dimension;		/* Number of statements        */
  int stmt_ndx;			/* 0 <= stmt_ndx < DIM(statements)     */
  /* statements[stmt_ndx] will be processed by
     next call to db_compile_statement.       */
  int line_offset;		/* amount to add to parsers line number */
  int column_offset;		/* amount to add to parsers column number   */
  PARSER_CONTEXT *parser;		/* handle to parser context structure */
  DB_QUERY_TYPE **type_list;	/* for storing "nice" column headings */
  /* type_list[stmt_ndx] is itself an array.  */
  PT_NODE **statements;		/* statements to be processed in this session */
};

#endif /* _API_COMPAT_H_ */
