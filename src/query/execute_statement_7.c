/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_query.c - Functions for the implementation of virtual queries.
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "server.h"
#include "db.h"
#include "parser.h"
#include "xasl_generation_2.h"
#include "memory_manager_2.h"
#include "authenticate.h"
#include "msgexec.h"
#include "qp_xdata.h"
#include "system_parameter.h"
#include "execute_statement_11.h"
#include "network_interface_sky.h"

/*
 * do_select() -
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note: Side effects can exist at returned result through application extern
 */
int
do_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error;
  XASL_NODE *xasl = NULL;
  QFILE_LIST_ID *list_id = NULL;
  int into_cnt, i;
  PT_NODE *into;
  const char *into_label;
  DB_VALUE *vals, *v;
  int save;
  int size;
  char *stream = NULL;
  int query_id = -1;
  QUERY_FLAG query_flag;
  int async_flag;

  error = NO_ERROR;

  AU_DISABLE (save);
  parser->au_save = save;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  async_flag = pt_statement_have_methods (parser, statement) ?
    ASYNC_UNEXECUTABLE : ASYNC_EXECUTABLE;

  query_flag = parser->exec_mode | async_flag;

  if (query_flag == (ASYNC_EXEC | ASYNC_UNEXECUTABLE))
    {
      query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
    }

#if defined(CUBRID_DEBUG)
  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
#endif

  xasl = parser_generate_xasl (parser, statement);

  if (xasl && !pt_has_error (parser))
    {
      if (pt_false_where (parser, statement))
	{
	  /* there is no results, this is a compile time false where clause */
	  statement->etc = NULL;
	}
      else
	{
	  if (error >= NO_ERROR)
	    {
	      error = xts_map_xasl_to_stream (xasl, &stream, &size);
	      if (error != NO_ERROR)
		{
		  PT_ERRORm (parser, statement,
			     MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		}
	    }

	  if (error >= NO_ERROR)
	    {
	      error = query_prepare_and_execute (stream,
						 size,
						 &query_id,
						 parser->host_var_count +
						 parser->
						 auto_param_count,
						 parser->host_variables,
						 &list_id, query_flag);
	    }
	  parser->query_id = query_id;
	  statement->etc = list_id;

	  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
	  if (stream)
	    {
	      free_and_init (stream);
	    }

	  if (error >= NO_ERROR)
	    {
	      /* if select ... into label ... has some result val
	         then enter {label,val} pair into the label_table */
	      into = statement->info.query.into_list;

	      into_cnt = pt_length_of_list (into);
	      if (into_cnt > 0
		  && (vals = (DB_VALUE *)
		      malloc (into_cnt * sizeof (DB_VALUE))) != NULL)
		{
		  if (pt_get_one_tuple_from_list_id
		      (parser, statement, vals, into_cnt))
		    {
		      for (i = 0, v = vals;
			   i < into_cnt && into; i++, v++, into = into->next)
			{
			  if (into->node_type == PT_NAME
			      && (into_label =
				  into->info.name.original) != NULL)
			    {
			      pt_associate_label_with_value (into_label,
							     db_value_copy
							     (v));
			    }
			  db_value_clear (v);
			}
		    }
		  else if (into->node_type == PT_NAME)
		    {
		      PT_ERRORmf (parser, statement,
				  MSGCAT_SET_PARSER_RUNTIME,
				  MSGCAT_RUNTIME_PARM_IS_NOT_SET,
				  into->info.name.original);
		    }
		  free_and_init (vals);
		}
	    }
	  else
	    {
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  error = ER_REGU_SYSTEM;
		}
	    }
	}			/* else */
    }
  else
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  AU_ENABLE (save);
  return error;
}

/*
 * do_prepare_select() - Prepare the SELECT statement including optimization and
 *                       plan generation, and creating XASL as the result
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note:
 */
int
do_prepare_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  const char *qstr;
  XASL_ID *xasl_id;
  XASL_NODE *xasl;
  char *stream;
  int size;
  int au_save;

  /* there can be no results, this is a compile time false where clause */
  if (pt_false_where (parser, statement))
    {
      /* tell to the execute routine that there's no XASL to execute */
      statement->xasl_id = NULL;
      return NO_ERROR;
    }

  /* if already prepared */
  if (statement->xasl_id)
    {
      return NO_ERROR;
    }

  /* make query string */
  parser->dont_prt_long_string = 1;
  parser->long_string_skipped = 0;
  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
  qstr = statement->alias_print;
  parser->dont_prt_long_string = 0;
  if (parser->long_string_skipped)
    {
      statement->cannot_prepare = 1;
      return NO_ERROR;
    }

  /* look up server's XASL cache for this query string
     and get XASL file id (XASL_ID) returned if found */
  xasl_id = NULL;
  if (statement->recompile == 0)
    {
      err = query_prepare (qstr, NULL, 0, &xasl_id);
    }
  else
    {
      (void) qmgr_drop_query_plan (qstr, db_identifier (db_get_user ()),
				   NULL, true);
    }
  if (!xasl_id)
    {
      /* cache not found;
         make XASL from the parse tree including query optimization
         and plan generation */

      /* mark the beginning of another level of xasl packing */
      pt_enter_packing_buf ();

      AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
					   checking during generating XASL */
      /* pt_to_xasl() will build XASL tree from parse tree */
      xasl = parser_generate_xasl (parser, statement);
      AU_RESTORE (au_save);
      stream = NULL;

      if (xasl && (err == NO_ERROR) && !pt_has_error (parser))
	{
	  /* convert the created XASL tree to the byte stream for transmission
	     to the server */
	  err = xts_map_xasl_to_stream (xasl, &stream, &size);
	  if (err != NO_ERROR)
	    {
	      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	    }
	}
      else
	{
	  err = er_errid ();
	  if (err == NO_ERROR)
	    {
	      err = ER_FAILED;
	    }
	}

      /* mark the end of another level of xasl packing */
      pt_exit_packing_buf ();

      /* request the server to prepare the query;
         give XASL stream generated from the parse tree
         and get XASL file id returned */
      if (stream && (err == NO_ERROR))
	{
	  err = query_prepare (qstr, stream, size, &xasl_id);
	  if (err != NO_ERROR)
	    {
	      err = er_errid ();
	    }
	}
      /* As a result of query preparation of the server,
         the XASL cache for this query will be created or updated. */

      /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
      if (stream)
	{
	  free_and_init (stream);
	}
    }

  /* save the XASL_ID that is allocated and returned by query_prepare()
     into 'statement->xasl_id' to be used by do_execute_select() */
  statement->xasl_id = xasl_id;

  return err;
}				/* do_prepare_select() */

/*
 * do_execute_select() - Execute the prepared SELECT statement
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note:
 */
int
do_execute_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  QFILE_LIST_ID *list_id;
  int query_flag, into_cnt, i, au_save;
  PT_NODE *into;
  const char *into_label;
  DB_VALUE *vals, *v;
  CACHE_TIME clt_cache_time;

  /* check if this statement is not necessary to execute,
     e.g. false where or not prepared correctly */
  if (!statement->xasl_id)
    {
      statement->etc = NULL;
      return NO_ERROR;
    }

  /* adjust query flag */
  if (parser->exec_mode == ASYNC_EXEC)
    {
      if (pt_statement_have_methods (parser, statement))
	{
	  query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
	}
      else
	{
	  query_flag = ASYNC_EXEC | ASYNC_EXECUTABLE;
	}
    }
  else
    {
      query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
    }
  if (statement->do_not_keep == 0)
    {
      query_flag |= KEEP_PLAN_CACHE;
    }
  if (statement->si_timestamp == 1 || statement->si_tran_id == 1)
    {
      statement->info.query.reexecute = 1;
      statement->info.query.do_not_cache = 1;
    }
  if (statement->info.query.reexecute == 1)
    {
      query_flag |= NOT_FROM_RESULT_CACHE;
    }
  if (statement->info.query.do_cache == 1)
    {
      query_flag |= RESULT_CACHE_REQUIRED;
    }
  if (statement->info.query.do_not_cache == 1)
    {
      query_flag |= RESULT_CACHE_INHIBITED;
    }

  /* flush necessary objects before execute */
  (void) parser_walk_tree (parser, statement, pt_flush_classes, NULL, NULL,
			   NULL);
  if (parser->abort)
    {
      return er_errid ();
    }

  /* Request that the server executes the stored XASL, which is the execution
     plan of the prepared query, with the host variables given by users as
     parameter values for the query.
     As a result, query id and result file id (QFILE_LIST_ID) will be returned.
     do_prepare_select() has saved the XASL file id (XASL_ID) in
     'statement->xasl_id' */

  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
				   checking for method */
  parser->query_id = -1;
  list_id = NULL;

  CACHE_TIME_RESET (&clt_cache_time);
  if (statement->clt_cache_check)
    {
      clt_cache_time = statement->cache_time;
      statement->clt_cache_check = 0;
    }
  CACHE_TIME_RESET (&statement->cache_time);
  statement->clt_cache_reusable = 0;

  err = query_execute (statement->xasl_id,
		       &parser->query_id,
		       parser->host_var_count + parser->auto_param_count,
		       parser->host_variables,
		       &list_id,
		       query_flag, &clt_cache_time, &statement->cache_time);

  AU_RESTORE (au_save);

  if (CACHE_TIME_EQ (&clt_cache_time, &statement->cache_time))
    {
      statement->clt_cache_reusable = 1;
    }

  /* save the returned QFILE_LIST_ID into 'statement->etc' */
  statement->etc = (void *) list_id;

  if (err < NO_ERROR)
    {
      return er_errid ();
    }

  /* if SELECT ... INTO label ... has some result val, then enter {label,val}
     pair into the label_table */
  into = statement->info.query.into_list;
  if ((into_cnt = pt_length_of_list (into)) > 0
      && (vals = (DB_VALUE *) malloc (into_cnt * sizeof (DB_VALUE))) != NULL)
    {
      if (pt_get_one_tuple_from_list_id (parser, statement, vals, into_cnt))
	{
	  for (i = 0, v = vals; i < into_cnt && into;
	       i++, v++, into = into->next)
	    {
	      if (into->node_type == PT_NAME
		  && (into_label = into->info.name.original) != NULL)
		pt_associate_label_with_value (into_label, db_value_copy (v));

	      db_value_clear (v);
	    }
	}
      else if (into->node_type == PT_NAME)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_PARM_IS_NOT_SET,
		      into->info.name.original);
	}
      free_and_init (vals);
    }

  return err;
}				/* do_execute_select() */
