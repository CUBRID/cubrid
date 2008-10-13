/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_delete.c - DO functions for delete statements
 * TODO: rename this file to execute_delete.c
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "server.h"
#include "parser.h"
#include "db.h"
#include "authenticate.h"
#include "semantic_check.h"
#include "xasl_generation_2.h"
#include "locator_cl.h"
#include "msgexec.h"
#include "schema_manager_3.h"
#include "execute_statement_11.h"
#include "transaction_cl.h"
#include "virtual_object_1.h"
#include "qp_mem.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "execute_statement_11.h"
#include "view_transform_1.h"
#include "network_interface_sky.h"
#include "dbval.h"

/* used to generate unique savepoint names */
static int savepoint_number = 0;

static int select_delete_list (PARSER_CONTEXT * parser,
			       QFILE_LIST_ID ** result_p, PT_NODE * from,
			       PT_NODE * where, PT_NODE * using_index,
			       PT_NODE * class_specs);
static int delete_object_tuple (const PARSER_CONTEXT * parser,
				const DB_VALUE * values);
static int delete_object_by_oid (const PARSER_CONTEXT * parser,
				 const PT_NODE * statement);
static int delete_list_by_oids (PARSER_CONTEXT * parser,
				QFILE_LIST_ID * list_id);
static int build_xasl_for_server_delete (PARSER_CONTEXT * parser,
					 const PT_NODE * from,
					 PT_NODE * statement);
static int has_unique_constraint (DB_OBJECT * mop);
static int delete_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
			      PT_NODE * statement);

/*
 * select_delete_list() -
 *   return: Error code
 *   parser(in/out): Parser context
 *   result(out): QFILE_LIST_ID for query result
 *   from(in): From clause in the query
 *   where(in): Where clause in the query
 *   using_index(in): Using index in the query
 *   class_specs(in): Class spec for the query
 *
 * Note : The list_id is allocated during query execution
 */
static int
select_delete_list (PARSER_CONTEXT * parser, QFILE_LIST_ID ** result_p,
		    PT_NODE * from,
		    PT_NODE * where, PT_NODE * using_index,
		    PT_NODE * class_specs)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;

  if (from
      && (from->node_type == PT_SPEC)
      && from->info.spec.range_var
      && ((statement = pt_to_upd_del_query (parser, NULL,
					    from, class_specs,
					    where, using_index,
					    0 /* not server update */ ))
	  != NULL))
    {

      /* If we are updating a proxy, the select is not yet fully translated.
         if wer are updating anything else, this is a no-op. */
      statement = mq_translate (parser, statement);

      if (statement)
	{
	  /* This enables authorization checking during methods in queries */
	  AU_ENABLE (parser->au_save);
	  if (do_select (parser, statement) < NO_ERROR)
	    {
	      /* query failed, an error has already been set */
	      statement = NULL;
	    }
	  AU_DISABLE (parser->au_save);
	}
    }

  if (statement)
    {
      result = (QFILE_LIST_ID *) statement->etc;
      parser_free_tree (parser, statement);
    }

  *result_p = result;
  return (er_errid () < 0) ? er_errid () : NO_ERROR;
}

/*
 * delete_object_tuple() - Deletes object attributes with db_values
 *   return: Error code if db_put fails
 *   parser(in): Parser context
 *   values(in): Array of db_value pointers. The first element in the
 *  		 array is the OID of the object to delete.
 */
static int
delete_object_tuple (const PARSER_CONTEXT * parser, const DB_VALUE * values)
{
  int error = NO_ERROR;
  DB_OBJECT *object;

  if (values && DB_VALUE_TYPE (values) == DB_TYPE_OBJECT)
    {
      object = DB_GET_OBJECT (values);

      if (db_is_deleted (object))
	{
	  /* nested delete trigger may delete the object already */
	  return NO_ERROR;
	}

      /* authorizations checked in compiler--turn off but remember in
         parser so we can re-enable in case we run out of memory and
         longjmp to the cleanup routine. */
      error = db_drop (object);
    }
  else
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

/*
 * delete_object_by_oid() - Deletes db object by oid.
 *   return: Error code if delete fails
 *   parser(in): Parser context
 *   statement(in): Parse tree representing object to delete
 */
static int
delete_object_by_oid (const PARSER_CONTEXT * parser,
		      const PT_NODE * statement)
{
  int error = NO_ERROR;

  error = ER_GENERIC_ERROR;	/* unimplemented */

  return error;
}

/*
 * delete_list_by_oids() - Deletes every oid in a list file
 *   return: Error code if delete fails
 *   parser(in): Parser context
 *   list_id(in): A list file of oid's
 */
static int
delete_list_by_oids (PARSER_CONTEXT * parser, QFILE_LIST_ID * list_id)
{
  int error = NO_ERROR;
  int cursor_status;
  DB_VALUE oid;
  CURSOR_ID cursor_id;
  int count = 0;		/* how many objects were deleted? */
  const char *savepoint_name = NULL;
  int flush_to_server = -1;
  DB_OBJECT *mop;

  /* if the list file contains more than 1 object we need to savepoint
     the statement to guarantee statement atomicity. */
  if (list_id->tuple_cnt >= 1)
    {
      savepoint_name = mq_generate_name (parser, "UdsP", &savepoint_number);

      error = tran_savepoint (savepoint_name, false);
    }

  if (error == NO_ERROR)
    {
      if (!cursor_open (&cursor_id, list_id, false, false))
	{
	  error = ER_GENERIC_ERROR;
	}
      else
	{
	  cursor_id.query_id = parser->query_id;
	  cursor_status = cursor_next_tuple (&cursor_id);

	  while ((error >= NO_ERROR) && cursor_status == DB_CURSOR_SUCCESS)
	    {
	      /* the db_value has the oid to delete. */
	      error = cursor_get_tuple_value_list (&cursor_id, 1, &oid);
	      if (flush_to_server != 0)
		{
		  mop = DB_GET_OBJECT (&oid);
		  if (flush_to_server == -1)
		    {
		      flush_to_server = has_unique_constraint (mop);
		    }
		}

	      if (error >= NO_ERROR)
		{
		  error = delete_object_tuple (parser, &oid);
		  if (error == ER_HEAP_UNKNOWN_OBJECT && do_Trigger_involved)
		    {
		      er_clear ();
		      error = NO_ERROR;
		      cursor_status = cursor_next_tuple (&cursor_id);
		      continue;
		    }
		}

	      if ((error >= NO_ERROR) && flush_to_server)
		{
		  error = (locator_flush_instance (mop) == NO_ERROR) ?
		    0 : (er_errid () != NO_ERROR ? er_errid () : -1);
		}

	      if (error >= NO_ERROR)
		{
		  count++;	/* another object has been deleted */
		  cursor_status = cursor_next_tuple (&cursor_id);
		}
	    }

	  if ((error >= NO_ERROR) && cursor_status != DB_CURSOR_END)
	    {
	      error = ER_GENERIC_ERROR;
	    }
	  cursor_close (&cursor_id);

	  /* if error and a savepoint was created, rollback to savepoint.
	     No need to rollback if the TM aborted the transaction
	     itself.
	   */
	  if ((error < NO_ERROR) && savepoint_name
	      && error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	}
    }

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * build_xasl_for_server_delete() - Build an xasl tree for a server delete
 *                                     and execute it.
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   from(in): Class spec to delete
 *   statement(in): Parse tree of a delete statement.
 *
 * Note:
 *  The xasl tree has an DELETE_PROC node as the top node and
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
 *  instance OID.  The DELETE_PROC node scans the BUILDLIST_PROC results.
 *  The server executes the aptr and then for each instance selected,
 *  deletes it.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.
 *
 *  The instances for the class are flushed from the client before the
 *  delete is executed.  If any instances are deleted, the instances are
 *  decached from the client after the delete is executed.
 */
static int
build_xasl_for_server_delete (PARSER_CONTEXT * parser, const PT_NODE * from,
			      PT_NODE * statement)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  DB_OBJECT *class_obj;
  int size, count = 0;
  char *stream = NULL;
  int query_id = -1;
  QFILE_LIST_ID *list_id = NULL;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();
  class_obj = from->info.spec.flat_entity_list->info.name.db_object;

  xasl = pt_to_delete_xasl (parser, statement);

  if (xasl)
    {
      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}
    }
  else
    {
      error = er_errid ();
    }

  if (error == NO_ERROR)
    {
      int au_save;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      error = query_prepare_and_execute (stream,
					 size,
					 &query_id,
					 parser->host_var_count +
					 parser->auto_param_count,
					 parser->host_variables,
					 &list_id,
					 parser->
					 exec_mode | ASYNC_UNEXECUTABLE);
      AU_RESTORE (au_save);
    }
  parser->query_id = query_id;

  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (stream)
    {
      free_and_init (stream);
    }

  if (list_id)
    {
      count = list_id->tuple_cnt;
      if (count > 0)
	{
	  error = sm_flush_and_decache_objects (class_obj, true);
	}
      regu_free_listid (list_id);
    }

  pt_end_query (parser);

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * has_unique_constraint() - Check if class has an unique constraint
 *   return: 1 if the class has an unique constraint, otherwise 0
 *   mop(in/out): Class object to be checked
 */
static int
has_unique_constraint (DB_OBJECT * mop)
{
  DB_CONSTRAINT *constraint_list, *c;
  SM_CONSTRAINT_TYPE ctype;

  if (mop == NULL)
    {
      return 0;
    }

  constraint_list = db_get_constraints (mop);
  for (c = constraint_list; c; c = c->next)
    {
      ctype = c->type;
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (ctype))
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * delete_real_class() - Deletes objects or rows in ldb class or ldb table
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   spec(in): Class spec to delete
 *   statement(in): Delete statement
 */
static int
delete_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
		   PT_NODE * statement)
{
  int error = NO_ERROR;
  QFILE_LIST_ID *oid_list = NULL;
  int trigger_involved;
  MOBJ class_;
  DB_OBJECT *class_obj;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  /* delete a "real" class in this database */

  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  /* The IX lock on the class is sufficient.
     DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
  class_ = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE);
  if (!class_)
    {
      return er_errid ();
    }

  error = sm_class_has_triggers (class_obj, &trigger_involved);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* do delete on server if there is no trigger involved and the
     class is a real class */
  if ((!trigger_involved) &&
      (spec->info.spec.flat_entity_list->info.name.virt_object == NULL))
    {
      error = build_xasl_for_server_delete (parser, spec, statement);
    }
  else
    {
      hint_arg = statement->info.delete_.waitsecs_hint;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = atof (hint_arg->info.name.original);
	  if (waitsecs >= -1)
	    {
	      old_waitsecs = TM_TRAN_WAITSECS ();
	      (void) tran_reset_wait_times (waitsecs);
	    }
	}
      if (error >= NO_ERROR)
	{
	  /* get the oid's and new values */
	  error = select_delete_list (parser,
				      &oid_list,
				      spec,
				      statement->info.delete_.
				      search_cond,
				      statement->info.delete_.
				      using_index,
				      statement->info.delete_.class_specs);
	}
      if (old_waitsecs >= -1)
	{
	  (void) tran_reset_wait_times (old_waitsecs);
	}

      if (!oid_list)
	{
	  /* an error should be set already, don't lose it */
	  return error;
	}

      /* delete each oid */
      error = delete_list_by_oids (parser, oid_list);
      regu_free_listid (oid_list);
      pt_end_query (parser);
    }
  return error;
}

/*
 * do_delete() - Deletes objects or rows
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   statement(in): Delete statement
 *
 * Note: Returning the number of deleted object on success is a bug fix!
 *       uci_static expects do_execute_statement to return the number of
 *       affected objects for a successful DELETE, UPDATE, INSERT, SELECT.
 */
int
do_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  int result = NO_ERROR;
  PT_NODE *spec;
  int rollbacktosp = 0;
  const char *savepoint_name = NULL;

  /* DON'T REMOVE this, correct authorization validation of views
     depends on this.

     DON'T return from the body of this function. Break out of the loop
     if necessary. */

  AU_DISABLE (parser->au_save);

  while (statement && error >= 0)
    {

      if (statement->node_type != PT_DELETE)
	{
	  /* bullet proofing, should not get here */
	  PT_INTERNAL_ERROR (parser, "delete");
	  error = ER_GENERIC_ERROR;
	  break;
	}

      spec = statement->info.delete_.spec;

      if (pt_false_where (parser, statement))
	{
	  /* nothing to delete, where part is false */
	}
      else if (!spec)
	{
	  /* this is an delete object if it has no spec */
	  error = delete_object_by_oid (parser, statement);
	}
      else
	{
	  /* the following is the "normal" sqlx type execution */
	  error = delete_real_class (parser, spec, statement);
	}

      result += error;
      statement = statement->next;
    }

  /* if error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */

  if ((error < NO_ERROR) && rollbacktosp &&
      (error != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  if (error < 0)
    {
      result = error;
    }

  /* DON'T REMOVE this, correct authorization validation of views
     depends on this. */

  AU_ENABLE (parser->au_save);

  return result;
}

/*
 * do_prepare_delete() - Prepare the DELETE statement
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): Delete statement
 */
int
do_prepare_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  PT_NODE *flat;
  DB_OBJECT *class_obj;
  int has_trigger, au_save;
  bool server_delete;
  XASL_ID *xasl_id;

  for (err = NO_ERROR; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
      /* there can be no results, this is a compile time false where clause */
      if (pt_false_where (parser, statement))
	{
	  /* tell to the execute routine that there's no XASL to execute */
	  statement->xasl_id = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* Delete object case:
         this is an delete object if it has no spec */
      if (!statement->info.delete_.spec)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* if already prepared */
      if (statement->xasl_id)
	{
	  continue;		/* continue to next DELETE statement */
	}

      flat = statement->info.delete_.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* the presence of a proxy trigger should force the delete
         to be performed through the workspace  */
      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */
      err = sm_class_has_triggers (class_obj, &has_trigger);
      AU_RESTORE (au_save);
      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "delete");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.delete_.has_trigger = (bool) has_trigger;

      /* determine whether it can be server-side or OID list deletion */
      server_delete = (!has_trigger && (flat->info.name.virt_object == NULL));
      statement->info.delete_.server_delete = server_delete;

      xasl_id = NULL;
      if (server_delete)
	{
	  /* Server-side deletion case: (by requesting server to execute XASL)
	     build DELETE_PROC XASL */

	  const char *qstr;
	  XASL_NODE *xasl;
	  char *stream;
	  int size;

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
	  if (statement->recompile == 0)
	    {
	      err = query_prepare (qstr, NULL, 0, &xasl_id);
	    }
	  else
	    {
	      (void) qmgr_drop_query_plan (qstr,
					   db_identifier (db_get_user ()),
					   NULL, true);
	    }
	  if (!xasl_id)
	    {
	      /* cache not found;
	         make XASL from the parse tree including query optimization
	         and plan generation */

	      /* mark the beginning of another level of xasl packing */
	      pt_enter_packing_buf ();

	      /* this prevents authorization checking during generating XASL */
	      AU_SAVE_AND_DISABLE (au_save);

	      /* pt_to_delete_xasl() will build XASL tree from parse tree */
	      xasl = pt_to_delete_xasl (parser, statement);
	      AU_RESTORE (au_save);
	      stream = NULL;
	      if (xasl && (err >= NO_ERROR))
		{
		  /* convert the created XASL tree to the byte stream for
		     transmission to the server */
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
		  pt_record_error (parser, parser->statement_number,
				   statement->line_number,
				   statement->column_number, er_msg ());
		}

	      /* mark the end of another level of xasl packing */
	      pt_exit_packing_buf ();

	      /* request the server to prepare the query;
	         give XASL stream generated from the parse tree
	         and get XASL file id returned */
	      if (stream && (err >= NO_ERROR))
		{
		  err = query_prepare (qstr, stream, size, &xasl_id);
		  if (err != NO_ERROR)
		    {
		      err = er_errid ();
		    }
		}
	      /* As a result of query preparation of the server,
	         the XASL cache for this query will be created or updated. */

	      /* free 'stream' that is allocated inside of
	         xts_map_xasl_to_stream() */
	      if (stream)
		{
		  free_and_init (stream);
		}
	    }
	}
      else
	{
	  /* OID list deletion case: (by selecting OIDs to delete)
	     make SELECT statement for this DELETE statement */

	  PT_NODE *select_statement;
	  PT_DELETE_INFO *delete_info;

	  delete_info = &statement->info.delete_;
	  select_statement = pt_to_upd_del_query (parser, NULL,
						  delete_info->spec,
						  delete_info->class_specs,
						  delete_info->search_cond,
						  delete_info->using_index,
						  0);
	  /* translate views or virtual classes into base classes;
	     If we are updating a proxy, the SELECT is not yet fully
	     translated. If we are updating anything else, this is a no-op. */

	  /* this prevents authorization checking during view transformation */
	  AU_SAVE_AND_DISABLE (au_save);

	  select_statement = mq_translate (parser, select_statement);
	  AU_RESTORE (au_save);
	  if (select_statement)
	    {
	      /* get XASL_ID by calling do_prepare_select() */
	      err = do_prepare_select (parser, select_statement);
	      xasl_id = select_statement->xasl_id;
	      parser_free_tree (parser, select_statement);
	    }
	  else
	    {
	      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	      err = er_errid ();
	    }

	}

      /* save the XASL_ID that is allocated and returned by
         query_prepare() into 'statement->xasl_id'
         to be used by do_execute_delete() */
      statement->xasl_id = xasl_id;

    }

  return err;
}

/*
 * do_execute_delete() - Execute the prepared DELETE statement
 *   return: Tuple count if success, otherwise an error code
 *   parser(in): Parser context
 *   statement(in): Delete statement
 */
int
do_execute_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err, result;
  PT_NODE *flat;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id;
  int au_save;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;
  int query_flag;

  /* If the DELETE statement contains more than one delete component,
     we savepoint the delete components to try to guarantee DELETE statement
     atomicity. */
  savepoint_name = NULL;

  for (err = NO_ERROR, result = 0; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
      /* Delete object case:
         delete object by OID */

      if (!statement->info.delete_.spec)
	{
	  err = delete_object_by_oid (parser, statement);
	  continue;		/* continue to next DELETE statement */
	}

      /* check if this statement is not necessary to execute,
         e.g. false where or not prepared correctly;
         Note that in LDB case, the statement was not prepared. */
      if (!statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* Server-side deletion or OID list deletion case:
         execute the prepared(stored) XASL (DELETE_PROC or SELECT statement) */

      flat = statement->info.delete_.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* The IX lock on the class is sufficient.
         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
	{
	  err = er_errid ();
	  break;		/* stop while loop if error */
	}

      /* flush necessary objects before execute */
      err = sm_flush_objects (class_obj);
      if (err != NO_ERROR)
	{
	  break;		/* stop while loop if error */
	}

      /* Request that the server executes the stored XASL, which is
         the execution plan of the prepared query, with the host variables
         given by users as parameter values for the query.
         As a result, query id and result file id (QFILE_LIST_ID) will be
         returned.
         do_prepare_delete() has saved the XASL file id (XASL_ID) in
         'statement->xasl_id' */
      query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;
      if (statement->do_not_keep == 0)
	{
	  query_flag |= KEEP_PLAN_CACHE;
	}
      query_flag |= NOT_FROM_RESULT_CACHE;
      query_flag |= RESULT_CACHE_INHIBITED;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      parser->query_id = -1;
      list_id = NULL;
      err = query_execute (statement->xasl_id,
			   &parser->query_id,
			   parser->host_var_count +
			   parser->auto_param_count,
			   parser->host_variables, &list_id, query_flag,
			   NULL, NULL);
      AU_RESTORE (au_save);
      if (err < NO_ERROR)
	{
	  err = er_errid ();
	}

      /* in the case of OID list deletion, now delete the seleted OIDs */
      if (!statement->info.delete_.server_delete
	  && (err >= NO_ERROR) && list_id)
	{
	  hint_arg = statement->info.delete_.waitsecs_hint;
	  if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = atof (hint_arg->info.name.original);
	      if (waitsecs >= -1)
		{
		  old_waitsecs = TM_TRAN_WAITSECS ();
		  (void) tran_reset_wait_times (waitsecs);
		}
	    }
	  AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
						   checking during execution */
	  /* delete each oid */
	  err = delete_list_by_oids (parser, list_id);
	  AU_RESTORE (au_save);
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }
	}

      /* free returned QFILE_LIST_ID */
      if (list_id)
	{
	  if (list_id->tuple_cnt > 0 && statement->info.delete_.server_delete)
	    {
	      err = sm_flush_and_decache_objects (class_obj, true);
	    }
	  if (err >= NO_ERROR)
	    {
	      err = list_id->tuple_cnt;	/* as a result */
	    }
	  regu_free_listid (list_id);
	}
      /* end the query; reset query_id and call qmgr_end_query() */
      if (parser->query_id > 0)
	{
	  if (er_errid () != ER_LK_UNILATERALLY_ABORTED)
	    qmgr_end_query (parser->query_id);
	  parser->query_id = -1;
	}

      /* accumulate intermediate results */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      /* To avoid violating the atomicity of a SQL delete statement,
         we need to flush in the proxy case. Otherwise, Opal's
         delayed flushing of dirty proxies can compromise the
         (perceived) atomicity of this SQL delete. For example,
         a proxy update may cause a constraint violation at the
         target ldb and if this is so, we need to provoke that
         violation now, not later. */
      if ((err >= NO_ERROR) && class_obj && db_is_vclass (class_obj))
	{
	  err = sm_flush_objects (class_obj);
	}

    }

  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name &&
      (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  return (err < NO_ERROR) ? err : result;
}
