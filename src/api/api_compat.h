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
 * api_compat.h -
 */

#ifndef _API_COMPAT_H_
#define _API_COMPAT_H_

struct db_session
{
  char *stage;			/* vector of statements' stage */
  char include_oid;		/* NO_OIDS, ROW_OIDS */
  int dimension;		/* Number of statements */
  int stmt_ndx;			/* 0 <= stmt_ndx < DIM(statements) */
  /* statements[stmt_ndx] will be processed by next call to db_compile_statement.  */
  int line_offset;		/* amount to add to parsers line number */
  int column_offset;		/* amount to add to parsers column number */
  PARSER_CONTEXT *parser;	/* handle to parser context structure */
  DB_QUERY_TYPE **type_list;	/* for storing "nice" column headings */
  /* type_list[stmt_ndx] is itself an array.  */
  PT_NODE **statements;		/* statements to be processed in this session */

  bool is_subsession_for_prepared;	/* whether this session is created for running a prepared statement, as a
					 * sub-session of a "true" client session */
  DB_SESSION *next;		/* subsessions for prepared statements */
};

#endif /* _API_COMPAT_H_ */
