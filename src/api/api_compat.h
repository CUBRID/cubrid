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
