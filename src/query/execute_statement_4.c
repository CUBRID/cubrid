/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_insert.c - DO functions for insert statements
 * TODO: rename this file to execute_insert.c
 */

#ident "$Id$"

#include "config.h"

#include "dbi.h"
#include "error_manager.h"
#include "parser.h"
#include "xasl_generation_2.h"
#include "db.h"
#include "object_accessor.h"
#include "authenticate.h"
#include "semantic_check.h"
#include "set_object_1.h"
#include "locator_cl.h"
#include "memory_manager_2.h"
#include "msgexec.h"
#include "schema_manager_3.h"
#include "execute_statement_11.h"
#include "xasl_generation_2.h"
#include "transaction_cl.h"
#include "environment_variable.h"
#include "intl.h"
#include "virtual_object_1.h"
#include "qp_mem.h"
#include "system_parameter.h"
#include "execute_schema_8.h"
#include "execute_statement_10.h"
#include "server.h"
#include "transform.h"
#include "schema_manager_3.h"
#include "view_transform_2.h"
#include "network_interface_sky.h"
#include "dbval.h"

typedef enum
{ INSERT_SELECT = 1,
  INSERT_VALUES = 2,
  INSERT_DEFAULT = 4
} SERVER_PREFERENCE;

/* used to generate unique savepoint names */
static int savepoint_number = 0;

/* 0 for no server inserts, a bit vector otherwise */
static int server_preference = -1;

static int insert_object_attr (const PARSER_CONTEXT * parser,
			       DB_OTMPL * otemplate, DB_VALUE * value,
			       PT_NODE * name, DB_ATTDESC * attr_desc);
static int check_for_cons (PARSER_CONTEXT * parser,
			   int *has_unique,
			   PT_NODE ** non_null_attrs,
			   const PT_NODE * attr_list, DB_OBJECT * class_obj);
static int check_for_fk_cache_attr (PARSER_CONTEXT * parser,
				    const PT_NODE * attr_list,
				    DB_OBJECT * class_obj);
static int is_server_insert_allowed (PARSER_CONTEXT * parser,
				     PT_NODE ** non_null_attrs,
				     int *has_uniques, int *server_allowed,
				     const PT_NODE * statement,
				     const PT_NODE * class_);
static int insert_subquery_results (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_NODE * class_,
				    const char **savepoint_name);
static int is_attr_not_in_insert_list (const PARSER_CONTEXT * parser,
				       PT_NODE * name_list, const char *name);
static int check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
					 const PT_NODE * statement);
static PT_NODE *make_vmops (PARSER_CONTEXT * parser, PT_NODE * node,
			    void *arg, int *continue_walk);
static PT_NODE *test_check_option (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *arg, int *continue_walk);
static int insert_local (PARSER_CONTEXT * parser, PT_NODE * statement);
static int insert_predefined_values_into_partition (const PARSER_CONTEXT *
						    parser,
						    DB_OTMPL ** rettemp,
						    const MOP classobj,
						    const DB_VALUE * partcol,
						    PARTITION_SELECT_INFO *
						    psi,
						    PARTITION_INSERT_CACHE *
						    pic);

/*
 * insert_object_attr()
 *   return: Error code if db_put fails
 *   parser(in): Short description of the param1
 *   otemplate(in/out): Short description of the param2
 *   value(in/out): New attr value
 *   name(in): Name to update
 *   attr_desc(in): Attr descriptor of attribute to update
 */
static int
insert_object_attr (const PARSER_CONTEXT * parser,
		    DB_OTMPL * otemplate, DB_VALUE * value,
		    PT_NODE * name, DB_ATTDESC * attr_desc)
{
  int error;

  if (DB_VALUE_TYPE (value) == DB_TYPE_OBJECT && !DB_IS_NULL (value))
    {
      /* we may need to check for value coming in from a parameter
         or host variable as a not-yet-translated-to-real-class
         value. This must be done at run time in general. */
      db_make_object (value, db_real_instance (db_get_object (value)));
    }

  if (name->info.name.db_object && db_is_vclass (name->info.name.db_object))
    {
      /* this is a shared attribute of a view.
         this means this cannot be updated in the template for
         this real class. Its simply done separately by a db_put. */
      error = obj_set_shared (name->info.name.db_object,
			      name->info.name.original, value);
    }
  else
    {
      /* the normal case */
      error = dbt_dput_internal (otemplate, attr_desc, value);
    }

  return error;
}

/*
 * do_insert_at_server() - Brief description of this function
 *   return: Error code if insert fails
 *   parser(in/out): Parser context
 *   statement(in): Parse tree of a insert statement.
 *   non_null_attrs(in):
 *   has_uniques(in):
 *
 * Note: Build an xasl tree for a server insert and execute it.
 *
 *  The xasl tree has an INSERT_PROC node as the top node and
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
 *  insert values.  The INSERT_PROC node scans the BUILDLIST_PROC results.
 *  The server executes the aptr and then for each instance selected,
 *  inserts it.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.

 *  The instances for the class are flushed from the client before the
 *  insert is executed.
 */
static int
do_insert_at_server (PARSER_CONTEXT * parser,
		     PT_NODE * statement,
		     PT_NODE * non_null_attrs, const int has_uniques)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  int size, count = 0;
  char *stream = NULL;
  int query_id = -1;
  QFILE_LIST_ID *list_id = NULL;
  int i;

  if (!parser || !statement || statement->node_type != PT_INSERT)
    {
      return ER_GENERIC_ERROR;
    }

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();
  xasl = pt_to_insert_xasl (parser, statement, has_uniques, non_null_attrs);

  if (xasl)
    {
      INSERT_PROC_NODE *insert = &xasl->proc.insert;

      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement,
		     MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}

      if (insert->partition)
	{
	  for (i = 0; i < insert->partition->no_parts; i++)
	    {
	      pr_clear_value (insert->partition->parts[i]->vals);
	    }
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
 * check_for_cons() - Determines whether an attribute has the constaint
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(in/out):
 *   non_null_attrs(in/out):
 *   attr_list(in): Parse tree of an insert statement attribute list
 *   class_obj(in): Class object

 */
static int
check_for_cons (PARSER_CONTEXT * parser,
		int *has_unique,
		PT_NODE ** non_null_attrs,
		const PT_NODE * attr_list, DB_OBJECT * class_obj)
{
  PT_NODE *pointer;

  *has_unique = 0;

  while (attr_list)
    {
      if (attr_list->node_type != PT_NAME)
	{
	  /* bullet proofing, should not get here */
	  return ER_GENERIC_ERROR;
	}

      if (*has_unique == 0 &&
	  sm_att_unique_constrained (class_obj,
				     attr_list->info.name.original))
	{
	  *has_unique = 1;
	}
      if (sm_att_constrained (class_obj, attr_list->info.name.original,
			      SM_ATTFLAG_NON_NULL))
	{
	  pointer = pt_point (parser, attr_list);
	  if (pointer == NULL)
	    {
	      PT_ERRORm (parser, attr_list,
			 MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

	      if (*non_null_attrs)
		{
		  parser_free_tree (parser, *non_null_attrs);
		}
	      *non_null_attrs = NULL;

	      return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
	    }
	  *non_null_attrs = parser_append_node (pointer, *non_null_attrs);
	}
      attr_list = attr_list->next;
    }

  if (*has_unique == 0)
    {
      /* check if the class has an auto-increment attribute which has unique
         constraint */

      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class_force (class_obj, &class_, AU_FETCH_READ) ==
	  NO_ERROR)
	{
	  for (att = class_->ordered_attributes; att; att = att->order_link)
	    {
	      if ((att->flags & SM_ATTFLAG_AUTO_INCREMENT) &&
		  ATT_IS_UNIQUE (att))
		{
		  *has_unique = 1;
		  break;
		}
	    }
	}
    }

  return NO_ERROR;
}

/*
 * check_for_fk_cache_attr() - Brief description of this function
 *   return: Error code
 *   parser(in): Parser context
 *   attr_list(in):
 *   class_obj(in):
 */
static int
check_for_fk_cache_attr (PARSER_CONTEXT * parser, const PT_NODE * attr_list,
			 DB_OBJECT * class_obj)
{
  while (attr_list)
    {
      if (attr_list->node_type != PT_NAME)
	{
	  return ER_GENERIC_ERROR;
	}

      if (sm_is_att_fk_cache (class_obj, attr_list->info.name.original))
	{
	  PT_ERRORmf (parser, attr_list, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_CANT_ASSIGN_FK_CACHE_ATTR,
		      attr_list->info.name.original);
	  return ER_PT_SEMANTIC;
	}
      attr_list = attr_list->next;
    }

  return NO_ERROR;
}

/*
 * is_server_insert_allowed() - Checks to see if a server-side insert is
 *                                 allowed
 *   return: Returns an error if any input nodes are
 *           misformed.
 *   parser(in): Parser context
 *   non_null_attrs(in/out): Parse tree for attributes with the NOT NULL
 *                           constraint
 *   has_uniques(in/out):
 *   server_allowed(in/out): Boolean flag
 *   statement(in): Parse tree of a insert statement
 *   class(in): Parse tree of the target class
 *
 *
 */
static int
is_server_insert_allowed (PARSER_CONTEXT * parser,
			  PT_NODE ** non_null_attrs,
			  int *has_uniques, int *server_allowed,
			  const PT_NODE * statement, const PT_NODE * class_)
{
  int error = NO_ERROR;
  int trigger_involved;
  PT_NODE *attrs, *attr;
  PT_NODE *vals, *val;

  *server_allowed = 0;

  attrs = statement->info.insert.attr_list;
  vals = NULL;
  /* server insert cannot handle insert into a shared attribute */
  attr = attrs;
  while (attr)
    {
      if (attr->node_type != PT_NAME)
	{
	  /* this can occur when inserting into a view
	     with default values. The name list may not
	     be inverted, and may contain expressions, such
	     as (x+2). */
	  return error;
	}
      if (attr->info.name.meta_class != PT_NORMAL)
	{
	  /* We found a shared attribute, bail out */
	  return error;
	}
      attr = attr->next;
    }

  error = check_for_fk_cache_attr (parser, attrs,
				   class_->info.name.db_object);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = check_for_cons (parser,
			  has_uniques, non_null_attrs,
			  attrs, class_->info.name.db_object);

  if (error < NO_ERROR)
    {
      return error;
    }

  if (server_preference < 0)
    {
      server_preference = PRM_INSERT_MODE;
    }
  /* check the insert form against the preference */
  if (statement->info.insert.is_value == PT_IS_SUBQUERY)
    {
      if (!(server_preference & INSERT_SELECT))
	{
	  return 0;
	}
    }
  else if (statement->info.insert.is_value == PT_IS_VALUE)
    {
      vals = statement->info.insert.value_clause;
      if (!(server_preference & INSERT_VALUES))
	{
	  return 0;
	}
    }
  else if (statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
    {
      if (!(server_preference & INSERT_DEFAULT))
	{
	  return 0;
	}
    }

  val = vals;
  while (val)
    {
      /* pt_to_regu in pt_to_xasl can't handle insert
         because it has not been taught to. Its feasible
         for this to call pt_to_insert_xasl with a bit of work.
       */
      if (val->node_type == PT_INSERT)
	{
	  return 0;
	}
      /* pt_to_regu in pt_to_xasl can't handle subquery
         because the context isn't set up. Again its feasible
         with some work.
       */
      if (PT_IS_QUERY_NODE_TYPE (val->node_type))
	{
	  return 0;
	}
      val = val->next;
    }

  error = NO_ERROR;
  if (statement->info.insert.into_var != NULL)
    {
      return error;
    }

  /* check option could be done on the server by adding another predicate
     to the insert_info block. However, one must also take care that
     subqueries in this predicate have there xasl blocks properly
     attached to the insert xasl block. Currently, pt_to_pred
     will do that only if being called from pt_to_xasl.
     This may mean that do_server_insert should call pt_to_xasl,
     and have a portion of its code put in pt_xasl.c
   */
  if (statement->info.insert.where != NULL)
    {
      return error;
    }

  error = sm_class_has_triggers (class_->info.name.db_object,
				 &trigger_involved);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Even if unique indexes are defined on the class,
     the operation could be performed on server. 
   */
  if (!trigger_involved)
    {
      *server_allowed = 1;
    }

  return error;
}

/*
 * do_insert_template() - Inserts an object or row into an object template
 *   return: Error code
 *   parser(in): Short description of the param1
 *   otemplate(in/out): class template to be inserted
 *   statement(in): Parse tree of an insert statement
 *   savepoint_name(in):
 */
int
do_insert_template (PARSER_CONTEXT * parser, DB_OTMPL ** otemplate,
		    PT_NODE * statement, const char **savepoint_name)
{
  const char *into_label;
  DB_VALUE *ins_val, *val, db_value, partcol;
  int error = NO_ERROR;
  PT_NODE *attr, *vc;
  PT_NODE *into;
  PT_NODE *class_;
  PT_NODE *non_null_attrs = NULL;
  DB_ATTDESC **attr_descs = NULL;
  int i, degree, row_count = 0;
  int server_allowed, has_uniques;
  PARTITION_SELECT_INFO *psi = NULL;
  PARTITION_INSERT_CACHE *pic = NULL, *picwork;
  MOP retobj;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  db_make_null (&db_value);
  db_make_null (&partcol);

  degree = 0;
  class_ = statement->info.insert.spec->info.spec.flat_entity_list;

  /* clear any previous error indicator because the
     rest of do_insert is sensitive to er_errid(). */
  er_clear ();

  if (!locator_fetch_class
      (class_->info.name.db_object, DB_FETCH_CLREAD_INSTWRITE))
    {
      return er_errid ();
    }

  error = is_server_insert_allowed (parser, &non_null_attrs,
				    &has_uniques, &server_allowed,
				    statement, class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (server_allowed)
    {
      error = do_insert_at_server (parser, statement,
				   non_null_attrs, has_uniques);
    }
  else if (statement->info.insert.is_value == PT_IS_SUBQUERY
	   && (vc = statement->info.insert.value_clause) != NULL)
    {
      /* execute subquery & insert its results into target class */
      row_count =
	insert_subquery_results (parser, statement, class_, savepoint_name);
      error = (row_count < 0) ? row_count : NO_ERROR;
    }
  else if (statement->info.insert.is_value == PT_IS_VALUE
	   || statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
    {
      /* there is one value to insert into target class
         If we are doing PT_IS_DEFAULT_VALUE value_clause
         will be NULL and we will only make a template with
         no values put in. */
      row_count = 1;

      /* partition adjust */
      if (class_->info.name.partition_of != NULL)
	{
	  if (do_init_partition_select (class_->info.name.db_object, &psi) ||
	      set_get_element (psi->pattr->data.set, 0, &partcol) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PARTITION_WORK_FAILED, 0);
	      return er_errid ();
	    }
	  *otemplate = NULL;	/*  for delayed insert */
	}
      else
	{
	  /* now create the object using templates, and then dbt_put
	     each value for each corresponding attribute.
	     Of course, it is presumed that
	     the order in which attributes are defined in the class as
	     well as in the actual insert statement is preserved. */
	  *otemplate =
	    dbt_create_object_internal (class_->info.name.db_object);
	  if (*otemplate == NULL)
	    {
	      return er_errid ();
	    }
	}

      vc = statement->info.insert.value_clause;
      attr = statement->info.insert.attr_list;
      degree = pt_length_of_list (attr);

      /* allocate attribute descriptors */
      if (attr)
	{
	  attr_descs = (DB_ATTDESC **)
	    calloc ((degree), sizeof (DB_ATTDESC *));
	  if (!attr_descs)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}

      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT &&
	  PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = atof (hint_arg->info.name.original);
	  if (waitsecs >= -1)
	    {
	      old_waitsecs = TM_TRAN_WAITSECS ();
	      (void) tran_reset_wait_times (waitsecs);
	    }
	}
      i = 0;
      while (attr && vc)
	{
	  if (vc->node_type == PT_INSERT && !*savepoint_name)
	    {

	      /* this is a nested insert.  recurse to create object
	         template for the nested insert. */
	      DB_OTMPL *temp = NULL;

	      error = do_insert_template (parser, &temp, vc, savepoint_name);
	      if (error >= NO_ERROR)
		{
		  if (!vc->info.insert.spec)
		    {
		      /* guard against seg fault for bad parse tree */
		      PT_INTERNAL_ERROR (parser, "insert");
		    }
		  else
		    {
		      /* we should test the check option, but
		         do_insert_template cannot since we are in the middle of a template
		         evaluation. The templat stuff should probably
		         get removed, in favor of the more general rollback
		         to savepoint. Then we can correctly test
		         the view check option.
		         error = mq_evaluate_check_option
		         (parser, vc->info.insert.where, obj,
		         vc->info.insert.spec->info.spec.flat_entity_list);
		       */
		    }
		}
	      if (error < NO_ERROR)
		{
		  break;
		}

	      db_make_pointer (&db_value, temp);
	    }
	  else
	    {
	      pt_evaluate_tree_having_serial (parser, vc, &db_value);
	      if (parser->error_msgs)
		{
		  (void) pt_report_to_ersys (parser, PT_EXECUTION);
		  error = er_errid ();
		  db_value_clear (&db_value);
		  break;
		}
	    }

	  if (!*otemplate)
	    {			/* partition adjust */
	      if (error < NO_ERROR)
		{
		  db_make_null (&db_value);
		}
	      if (intl_mbs_casecmp (attr->info.name.original,
				    DB_GET_STRING (&partcol)) == 0)
		{
		  /* partition column */
		  error = do_select_partition (psi, &db_value, &retobj);
		  if (error != NO_ERROR)
		    break;

		  *otemplate = dbt_create_object_internal (retobj);
		  if (*otemplate == NULL)
		    {
		      break;
		    }

		  /* cache back & insert */
		  for (picwork = pic; picwork; picwork = picwork->next)
		    {
		      error = insert_object_attr (parser, *otemplate,
						  picwork->val,
						  picwork->attr,
						  picwork->desc);
		      if (error != NO_ERROR)
			{
			  break;
			}
		    }
		}
	    }

	  if (error >= NO_ERROR)
	    {
	      /* don't get descriptors for shared attrs of views */
	      if (!attr->info.name.db_object ||
		  !db_is_vclass (attr->info.name.db_object))
		{
		  error =
		    db_get_attribute_descriptor (class_->info.name.db_object,
						 attr->info.name.original, 0,
						 1, &attr_descs[i]);
		}
	      if (error >= NO_ERROR)
		{
		  if (*otemplate)
		    {
		      error =
			insert_object_attr (parser, *otemplate, &db_value,
					    attr, attr_descs[i]);
		    }
		  else
		    {
		      error = do_insert_partition_cache (&pic, attr,
							 attr_descs[i],
							 &db_value);
		    }
		}
	    }
	  /* else vc is a SELECT query whose result is empty */

	  /* pt_evaluate_tree() always clones the db_value.
	     Thus we must clear it.
	   */
	  db_value_clear (&db_value);

	  if (!parser->error_msgs)
	    {
	      if (error < NO_ERROR)
		{
		  PT_ERRORmf3 (parser, vc, MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_DBT_PUT_ERROR,
			       pt_short_print (parser, vc),
			       attr->info.name.original,
			       pt_chop_trailing_dots (parser,
						      db_error_string (3)));
		}
	    }

	  attr = attr->next;
	  vc = vc->next;
	  i++;
	}
      if (old_waitsecs >= -1)
	{
	  (void) tran_reset_wait_times (old_waitsecs);
	}
      if (pic)
	{
	  if (*otemplate == NULL && (error >= NO_ERROR))
	    {
	      /* key is not specified */
	      error =
		insert_predefined_values_into_partition (parser,
							 otemplate,
							 class_->info.name.
							 db_object,
							 &partcol, psi, pic);
	    }
	  do_clear_partition_cache (pic);
	}
      db_value_clear (&partcol);
      if (psi != NULL)
	{
	  do_clear_partition_select (psi);
	}
    }

  if (non_null_attrs)
    {
      parser_free_tree (parser, non_null_attrs);
    }

  if ((error >= NO_ERROR)
      && (into = statement->info.insert.into_var) != NULL
      && into->node_type == PT_NAME
      && (into_label = into->info.name.original) != NULL)
    {
      /* check to see if more than one instance was inserted */
      if (row_count > 1)
	{
	  error = ER_DO_INSERT_TOO_MANY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  /* create another DB_VALUE of the new instance for
	     the label_table
	   */
	  ins_val = db_value_create ();
	  db_make_object (ins_val, (DB_OBJECT *) NULL);
	  if (statement->info.insert.is_value == PT_IS_VALUE
	      || statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
	    {

	      (void) dbt_set_label (*otemplate, ins_val);
	    }
	  else if ((val = (DB_VALUE *) (statement->etc)) != NULL)
	    {
	      db_make_object (ins_val, DB_GET_OBJECT (val));
	    }

	  /* enter {label, ins_val} pair into the label_table */
	  error = pt_associate_label_with_value (into_label, ins_val);
	}
    }

  /* free attribute descriptors */
  if (attr_descs)
    {
      for (i = 0; i < degree; i++)
	{
	  if (attr_descs[i])
	    {
	      db_free_attribute_descriptor (attr_descs[i]);
	    }
	}
      free_and_init (attr_descs);
    }

  return error;
}				/* do_insert_template */

/*
 * insert_subquery_results() - Execute subquery & insert its results into
 *                                a target class
 *   return: Error code
 *   parser(in): Handle to the parser used to process & derive subquery qry
 *   statemet(in/out):
 *   class(in):
 *   savepoint_name(in):
 *
 * Note:
 *   The function requires parser is the handle to the parser used to derive qry
 *   qry is an error-free abstract syntax tree derived from a CUBRID
 *   nested SELECT, UNION, DIFFERENCE, INTERSECTION subquery.
 *   qry's select_list attributes have been expanded & type-checked.
 *   qry's select_list and attrs have the same number of elements.
 *   It modifies database state of target class and
 *   effects that executes subquery and inserts its results as new instances of
 *   target class.
 */
static int
insert_subquery_results (PARSER_CONTEXT * parser,
			 PT_NODE * statement,
			 PT_NODE * class_, const char **savepoint_name)
{
  int error = NO_ERROR;
  CURSOR_ID cursor_id;
  DB_OTMPL *otemplate = NULL;
  DB_OBJECT *obj;
  PT_NODE *attr, *qry, *attrs;
  DB_VALUE *vals, *val, *ins_val, partcol;
  int degree, k, first, cnt, i;
  DB_ATTDESC **attr_descs = NULL;
  PARTITION_SELECT_INFO *psi = NULL;
  PARTITION_INSERT_CACHE *pic = NULL, *picwork;
  MOP retobj;

  if (!parser
      || !statement
      || statement->node_type != PT_INSERT
      || statement->info.insert.is_value != PT_IS_SUBQUERY
      || (qry = statement->info.insert.value_clause) == NULL
      || (attrs = statement->info.insert.attr_list) == NULL)
    {
      return ER_GENERIC_ERROR;
    }

  cnt = 0;
  db_make_null (&partcol);

  switch (qry->node_type)
    {
    default:			/* preconditions not met */
      return ER_GENERIC_ERROR;	/* so, nothing doing.    */

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* execute the subquery */
      error = do_select (parser, qry);
      if (error < NO_ERROR)
	{
	  return error;
	}

      /* insert subquery results into target class */
      if (cursor_open (&cursor_id, (QFILE_LIST_ID *) qry->etc, false, false))
	{
	  cursor_id.query_id = parser->query_id;

	  /* allocate space for attribute values */
	  degree =
	    pt_length_of_select_list (pt_get_select_list (parser, qry),
				      EXCLUDE_HIDDEN_COLUMNS);
	  vals = (DB_VALUE *) malloc (degree * sizeof (DB_VALUE));
	  if (vals == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  /* allocate attribute descriptor array */
	  if (degree)
	    {
	      attr_descs = (DB_ATTDESC **)
		malloc ((degree) * sizeof (DB_ATTDESC *));
	      if (!attr_descs)
		{
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      for (i = 0; i < degree; i++)
		{
		  attr_descs[i] = NULL;
		}
	    }

	  /* if the list file contains more than 1 object we need to savepoint
	     the statement to guarantee statement atomicity. */
	  if (((QFILE_LIST_ID *) qry->etc)->tuple_cnt > 1 && !*savepoint_name)
	    {
	      *savepoint_name = mq_generate_name (parser,
						  "UisP", &savepoint_number);
	      error = tran_savepoint (*savepoint_name, false);
	    }

	  if (error >= NO_ERROR)
	    {
	      if (class_->info.name.partition_of != NULL)
		{
		  if (do_init_partition_select
		      (class_->info.name.db_object, &psi)
		      || set_get_element (psi->pattr->data.set, 0,
					  &partcol) != NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PARTITION_WORK_FAILED, 0);
		      return er_errid ();
		    }
		}

	      /* for each tuple in subquery result do */
	      first = 1;
	      while (cursor_next_tuple (&cursor_id) == DB_CURSOR_SUCCESS)
		{
		  /* get current tuple of subquery result */
		  if (cursor_get_tuple_value_list (&cursor_id, degree, vals)
		      != NO_ERROR)
		    {
		      break;
		    }

		  /* create an instance of the target class using templates */
		  if (psi)
		    {
		      otemplate = NULL;	/* for delayed insert */
		    }
		  else
		    {
		      otemplate =
			dbt_create_object_internal (class_->info.name.
						    db_object);
		      if (otemplate == NULL)
			{
			  break;
			}
		    }

		  /* update new instance with current tuple of subquery result */
		  for (attr = attrs, val = vals, k = 0;
		       attr != NULL && k < degree;
		       attr = attr->next, val++, k++)
		    {
		      /* if this is the first tuple, get the attr descriptor */
		      if (attr_descs[k] == NULL)
			{
			  /* don't get descriptors for shared attrs of views */
			  if (!attr->info.name.db_object
			      || !db_is_vclass (attr->info.name.db_object))
			    {
			      error = db_get_attribute_descriptor
				(class_->info.name.db_object,
				 attr->info.name.original, 0, 1,
				 &attr_descs[k]);
			    }
			}

		      if (!otemplate)
			{	/* partition adjust */
			  if (intl_mbs_casecmp (attr->info.name.original,
						DB_GET_STRING (&partcol)) ==
			      0)
			    {
			      /* partition column */
			      error = do_select_partition (psi, val, &retobj);
			      if (error)
				{
				  break;
				}

			      otemplate = dbt_create_object_internal (retobj);
			      if (otemplate == NULL)
				{
				  break;
				}

			      /* cache back & insert */
			      for (picwork = pic; picwork;
				   picwork = picwork->next)
				{
				  error = insert_object_attr (parser,
							      otemplate,
							      picwork->
							      val,
							      picwork->
							      attr,
							      picwork->desc);
				  if (error)
				    {
				      break;
				    }
				}
			    }
			}

		      if (error >= NO_ERROR)
			{
			  if (otemplate)
			    {
			      error = insert_object_attr (parser, otemplate,
							  val, attr,
							  attr_descs[k]);
			    }
			  else
			    {
			      error = do_insert_partition_cache (&pic, attr,
								 attr_descs
								 [k], val);
			    }
			}

		      if (error < NO_ERROR)
			{
			  dbt_abort_object (otemplate);
			  for (val = vals, k = 0; k < degree; val++, k++)
			    {
			      db_value_clear (val);
			    }
			  free_and_init (vals);
			  cursor_close (&cursor_id);
			  regu_free_listid ((QFILE_LIST_ID *) qry->etc);
			  pt_end_query (parser);
			  /* free attribute descriptors */
			  if (attr_descs)
			    {
			      for (i = 0; i < degree; i++)
				{
				  if (attr_descs[i])
				    {
				      db_free_attribute_descriptor
					(attr_descs[i]);
				    }
				}
			      free_and_init (attr_descs);
			    }
			  return er_errid ();
			}
		    }

		  if (pic && !otemplate && error >= NO_ERROR)
		    {
		      /* key is not sepcified */
		      error =
			insert_predefined_values_into_partition
			(parser, &otemplate, class_->info.name.db_object,
			 &partcol, psi, pic);
		    }

		  /* apply the object template */
		  obj = dbt_finish_object (otemplate);

		  if (obj && error >= NO_ERROR)
		    {
		      error = mq_evaluate_check_option
			(parser, statement->info.insert.where, obj, class_);
		    }

		  if (obj == NULL || error < NO_ERROR)
		    {
		      for (val = vals, k = 0; k < degree; val++, k++)
			{
			  db_value_clear (val);
			}
		      free_and_init (vals);
		      cursor_close (&cursor_id);
		      regu_free_listid ((QFILE_LIST_ID *) qry->etc);
		      pt_end_query (parser);

		      /* free attribute descriptors */
		      if (attr_descs)
			{
			  for (i = 0; i < degree; i++)
			    {
			      if (attr_descs[i])
				{
				  db_free_attribute_descriptor (attr_descs
								[i]);
				}
			    }
			  free_and_init (attr_descs);
			}
		      if (obj == NULL)
			{
			  dbt_abort_object (otemplate);
			}

		      if (pic)
			{
			  do_clear_partition_cache (pic);
			}

		      db_value_clear (&partcol);

		      if (psi != NULL)
			{
			  do_clear_partition_select (psi);
			}
		      return er_errid ();
		    }

		  /* treat the first new instance as the insert's "result" */
		  if (first)
		    {
		      first = 0;
		      ins_val = db_value_create ();
		      db_make_object (ins_val, obj);
		      statement->etc = (void *) ins_val;
		    }

		  /* keep track of how many we have inserted */
		  cnt++;

		  /* clear the attribute values */
		  for (val = vals, k = 0; k < degree; val++, k++)
		    {
		      db_value_clear (val);
		    }
		  if (pic)
		    {
		      do_clear_partition_cache (pic);
		      pic = NULL;
		    }
		}
	      db_value_clear (&partcol);
	      if (psi != NULL)
		{
		  do_clear_partition_select (psi);
		}
	    }

	  free_and_init (vals);
	  cursor_close (&cursor_id);
	  /* free attribute descriptors */
	  if (attr_descs)
	    {
	      for (i = 0; i < degree; i++)
		{
		  if (attr_descs[i])
		    {
		      db_free_attribute_descriptor (attr_descs[i]);
		    }
		}
	      free_and_init (attr_descs);
	    }
	}

      regu_free_listid ((QFILE_LIST_ID *) qry->etc);
      pt_end_query (parser);

    }

  return cnt;
}				/* insert_subquery_results */

/*
 * is_attr_not_in_insert_list() - Returns 1 if the name is not on the name_list,
 *              0 otherwise. name_list is assumed to be a list of PT_NAME nodes.
 *   return: Error code
 *   param1(out): Short description of the param1
 *   param2(in/out): Short description of the param2
 *   param2(in): Short description of the param3
 *
 * Note: If you feel the need
 */
static int
is_attr_not_in_insert_list (const PARSER_CONTEXT * parser,
			    PT_NODE * name_list, const char *name)
{
  PT_NODE *tmp;
  int not_on_list = 1;

  for (tmp = name_list; tmp != NULL; tmp = tmp->next)
    {
      if (intl_mbs_casecmp (tmp->info.name.original, name) == 0)
	{
	  not_on_list = 0;
	  break;
	}
    }

  return not_on_list;

}				/* is_attr_not_in_insert_list */

/*
 * check_missing_non_null_attrs() - Check to see that all attributes of
 *              the class that have a NOT NULL constraint AND have no default
 *              value are present in the inserts assign list.
 *   return: Error code
 *   parser(in): Short description of the param1
 *   statement(in): Short description of the param2
 */
static int
check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
			      const PT_NODE * statement)
{
  DB_ATTRIBUTE *attr;
  DB_OBJECT *class_;
  int error = NO_ERROR;

  if (!parser ||
      !statement ||
      !statement->info.insert.spec ||
      !statement->info.insert.spec->info.spec.entity_name ||
      !(class_ = statement->info.insert.spec->
	info.spec.entity_name->info.name.db_object))
    return ER_GENERIC_ERROR;

  attr = db_get_attributes (class_);
  while (attr)
    {
      if (db_attribute_is_non_null (attr) &&
	  (db_value_type (db_attribute_default (attr)) == DB_TYPE_NULL) &&
	  (is_attr_not_in_insert_list (parser,
				       statement->info.insert.attr_list,
				       db_attribute_name (attr))
	   || (statement->info.insert.is_value == PT_IS_DEFAULT_VALUE
	       && db_value_is_null (db_attribute_default (attr)))) &&
	  !(attr->flags & SM_ATTFLAG_AUTO_INCREMENT))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_MISSING_NON_NULL_ASSIGN, 1,
		  db_attribute_name (attr));
	  error = ER_OBJ_MISSING_NON_NULL_ASSIGN;
	}
      attr = db_attribute_next (attr);
    }

  return error;
}				/* check_missing_non_null_attrs */

/*
 * make_vmops() -
 *   return: Error code
 *   parser(in): Short description of the param1
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
make_vmops (PARSER_CONTEXT * parser, PT_NODE * node,
	    void *arg, int *continue_walk)
{
  DB_OBJECT **vobj = ((DB_OBJECT **) arg);
  DB_OBJECT *vclass_mop, *obj;
  const char *into_label;
  DB_VALUE *val;

  if (node->node_type != PT_INSERT)
    {
      return node;
    }

  /* make a virtual obj if it is a virtual class and has an into label */
  if (node->info.insert.into_var &&
      ((vclass_mop = node->info.insert.spec->info.spec.
	flat_entity_list->info.name.virt_object) != NULL))
    {
      into_label = node->info.insert.into_var->info.name.original;
      val = pt_find_value_of_label (into_label);
      obj = DB_GET_OBJECT (val);
      *vobj = vid_build_virtual_mop (obj, vclass_mop);
      /* change the label to point to the newly created vmop, we don't need
         to call pt_associate_label_with_value here because we've directly
         modified the value that has been installed in the table.
       */
      db_make_object (val, *vobj);
    }
  else
    {
      *vobj = NULL;
    }

  return node;

}				/* make_vmops */

/*
 * test_check_option() - Tests if we are inserting to a class through a view
 *                         with a check option.
 *   return: Error code
 *   parser(in): Parser context
 *   node(in): The PT_NAME node of a potential insert
 *   arg(in/out): Nonzero iff insert statement has a check option
 *   continue_walk(in/out):
 */
static PT_NODE *
test_check_option (PARSER_CONTEXT * parser, PT_NODE * node,
		   void *arg, int *continue_walk)
{
  int *found = (int *) arg;
  PT_NODE *class_;
  DB_OBJECT *view;

  if (node->node_type != PT_INSERT || !node->info.insert.spec)
    {
      return node;
    }

  /* make a virtual obj if it is a virtual class and has an into label */
  class_ = node->info.insert.spec->info.spec.flat_entity_list;
  view = class_->info.name.virt_object;
  if (view)
    {
      if (sm_get_class_flag (view, SM_CLASSFLAG_WITHCHECKOPTION)
	  || sm_get_class_flag (view, SM_CLASSFLAG_LOCALCHECKOPTION))
	{
	  *found = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;

}

/*
 * insert_local() - Inserts an object or row
 *   return: Error code if insert fails
 *   parser(in): Parser context
 *   statement(in): The parse tree of a insert statement
 *
 * Note:
 *   The function requires flat is the PT_NAME node of an
 *   insert/update/delete spec's flat_entity_list.
 *   It modifies has_trigger and effects that determine if flat's
 *   target class is a proxy that has an active trigger
 */
static int
insert_local (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_OBJECT *obj, *vobj;
  DB_OTMPL *otemplate = NULL;
  int error = NO_ERROR;
  PT_NODE *class_, *vc;
  DB_VALUE *ins_val;
  int save;
  int has_check_option = 0;
  const char *savepoint_name = NULL;

  if (!statement
      || statement->node_type != PT_INSERT
      || !statement->info.insert.spec
      || !statement->info.insert.spec->info.spec.flat_entity_list)
    {
      return ER_GENERIC_ERROR;
    }

  class_ = statement->info.insert.spec->info.spec.flat_entity_list;

  statement->etc = NULL;

  error = check_missing_non_null_attrs (parser, statement);
  if (error != NO_ERROR)
    {
      return error;
    }

  statement = parser_walk_tree (parser, statement, NULL, NULL,
				test_check_option, &has_check_option);

  /* if the insert statement contains more than one insert component,
     we savepoint the insert components to try to guarantee insert
     statement atomicity.
   */

  if (!savepoint_name && has_check_option)
    {
      savepoint_name = mq_generate_name (parser, "UisP", &savepoint_number);
      error = tran_savepoint (savepoint_name, false);
    }

  if (statement->info.insert.is_value == PT_IS_SUBQUERY
      && (vc = statement->info.insert.value_clause)
      && pt_false_where (parser, vc))
    {
    }
  else
    {
      /* DO NOT RETURN UNTIL AFTER AU_ENABLE! */
      AU_DISABLE (save);
      parser->au_save = save;

      error = do_insert_template (parser, &otemplate, statement,
				  &savepoint_name);

      /*
         There are three cases:
         1) error == NO_ERROR and otemplate != NULL
         This case is the normal insert case and we must finish the
         template.
         2) error >= NO_ERROR and otemplate == NULL
         This case is an insert like: insert into x select ...
         In this case pt_insert_subquery_results() has done all
         the work and there is none to do here.
         3) error != NO_ERROR
         There was an error and if we have a template, we must clean
         up.
       */
      if (error < NO_ERROR)
	{
	  if (otemplate != NULL)
	    {
	      dbt_abort_object (otemplate);
	    }
	}
      else if (otemplate != NULL)
	{
	  obj = dbt_finish_object (otemplate);
	  if (obj == NULL)
	    {
	      error = er_errid ();
	      /* On error, the template must be freed.
	       */
	      dbt_abort_object (otemplate);
	    }

	  if (error >= NO_ERROR)
	    {
	      error = mq_evaluate_check_option (parser,
						statement->info.insert.where,
						obj, class_);
	    }

	  if (error >= NO_ERROR)
	    {
	      /* If any of the (nested) inserts were view objects we
	         need to find them and create VMOPS for them.  Use a
	         post walk so that vobj will point to the vmop for the
	         outer insert if one is needed.
	       */
	      vobj = NULL;
	      statement = parser_walk_tree (parser, statement, NULL, NULL,
					    make_vmops, &vobj);
	      /* create a DB_VALUE to hold the newly inserted instance */
	      ins_val = db_value_create ();
	      if (vobj)
		{
		  /* use the virtual mop */
		  db_make_object (ins_val, vobj);
		}
	      else
		{
		  db_make_object (ins_val, obj);
		}
	      statement->etc = (void *) ins_val;

	    }
	}

      AU_ENABLE (save);

      if (parser->abort)
	{
	  error = er_errid ();
	}
    }

  /* if error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction.
   */
  if (error < NO_ERROR && savepoint_name &&
      (error != ER_LK_UNILATERALLY_ABORTED))
    {
      /* savepoint from tran_savepoint() */
      (void) tran_internal_abort_upto_savepoint (savepoint_name, true);
      /* Use a special version of rollback which will not clobber
         cached views. We can do this because we know insert can not
         have created any views.
         This is in place of the extern function:
         db_abort_to_savepoint(savepoint_name);
       */
    }

  return error;
}

/*
 * do_insert() - Inserts an object or row
 *   return: Error code if insert fails
 *   parser(in/out): Parser context
 *   statement(in/out): Parse tree of a insert statement
 */
int
do_insert (PARSER_CONTEXT * parser, PT_NODE * root_statement)
{
  PT_NODE *statement = root_statement;
  int error;

  error = insert_local (parser, statement);

  while (error < NO_ERROR && statement->next)
    {
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_EXECUTION);
	}
      /* assume error was from mismatch of multiple possible translated
         inserts. Try the next statement in the list.
         Only report the last error.
       */
      parser_free_tree (parser, parser->error_msgs);
      parser->error_msgs = NULL;
      statement = statement->next;
      error = insert_local (parser, statement);

      /* check whether this transaction is a victim of deadlock during */
      /* request to the driver */
      if (parser->abort)
	{
	  return (er_errid ());
	}
      /* This is to allow the row "counting" to be done
         in db_execute_and_keep_statement, and also correctly
         returns the "result" of the last insert statement.
         Only the first insert statement in the list is examined
         for results.
       */
      root_statement->etc = statement->etc;
      statement->etc = NULL;
    }

  return error;
}

/*
 * do_prepare_insert () - Prepare the INSERT statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 */
int
do_prepare_insert (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int err;
  err = NO_ERROR;
  return err;
}				/* do_prepare_insert() */

/*
 * do_execute_insert () - Execute the prepared INSERT statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 */
int
do_execute_insert (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  err = NO_ERROR;
  return err;
}				/* do_execute_insert() */

/*
 * insert_predefined_values_into_partition () - Execute the prepared INSERT
 *                                                 statement
 *   return: Error code
 *   parser(in): Parser context
 *   rettemp(out): Partitioned new instance template
 *   classobj(in): Class's db_object
 *   partcol(in): Partition's key column name
 *   psi(in): Partition selection information
 *   pic(in): Partiton insert cache
 *
 * Note: attribute's auto increment value or default value
 *       is fetched, partiton selection is performed,
 *       instance created, cached values are inserted.
 *       and auto increment or default value are inserted.
 */
static int
insert_predefined_values_into_partition (const PARSER_CONTEXT * parser,
					 DB_OTMPL ** rettemp,
					 const MOP classobj,
					 const DB_VALUE * partcol,
					 PARTITION_SELECT_INFO * psi,
					 PARTITION_INSERT_CACHE * pic)
{
  DB_ATTRIBUTE *dbattr;
  DB_VALUE *ins_val = NULL;
  MOP retobj;
  int error;
  PARTITION_INSERT_CACHE *picwork;
  char oid_str[36];
  DB_VALUE next_val, auto_inc_val, oid_str_val;
  int r = 0, found;
  char auto_increment_name[SM_MAX_IDENTIFIER_LENGTH];
  DB_IDENTIFIER serial_obj_id;
  DB_DATA_STATUS data_status;
  const char *class_name;

  for (dbattr = db_get_attributes (classobj);
       dbattr; dbattr = db_attribute_next (dbattr))
    {
      if (intl_mbs_casecmp
	  (db_attribute_name (dbattr), DB_GET_STRING (partcol)) == 0)
	{
	  /* partition column */

	  /* 1. check auto increment */
	  if (dbattr->flags & SM_ATTFLAG_AUTO_INCREMENT)
	    {
	      /* set auto increment value */
	      if (dbattr->auto_increment == NULL)
		{
		  class_name = sm_class_name (dbattr->class_mop);
		  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name,
						  class_name,
						  dbattr->header.name);
		  r =
		    do_get_serial_obj_id (&serial_obj_id, &found,
					  auto_increment_name);
		  if (r == 0 && found)
		    {
		      dbattr->auto_increment = db_object (&serial_obj_id);
		    }
		}

	      if (dbattr->auto_increment != NULL)
		{
		  db_make_null (&next_val);

		  sprintf (oid_str, "%d %d %d",
			   dbattr->auto_increment->oid_info.oid.pageid,
			   dbattr->auto_increment->oid_info.oid.slotid,
			   dbattr->auto_increment->oid_info.oid.volid);
		  db_make_string (&oid_str_val, oid_str);

		  error = qp_get_serial_next_value (&next_val, &oid_str_val);
		  if (error == NO_ERROR)
		    {
		      db_value_domain_init (&auto_inc_val, dbattr->type->id,
					    dbattr->domain->precision,
					    dbattr->domain->scale);

		      (void) numeric_db_value_coerce_from_num (&next_val,
							       &auto_inc_val,
							       &data_status);

		      if (data_status == NO_ERROR)
			{
			  ins_val = &auto_inc_val;
			}
		    }
		}
	    }

	  /* 2. check default value */
	  if (ins_val == NULL)
	    {
	      ins_val = db_attribute_default (dbattr);
	    }

	  break;
	}
    }

  if (ins_val == NULL)
    {
      return -1;
    }

  error = do_select_partition (psi, ins_val, &retobj);
  if (!error)
    {
      *rettemp = dbt_create_object_internal (retobj);
      if (*rettemp != NULL)
	{
	  /* cache back & insert */
	  for (picwork = pic; picwork; picwork = picwork->next)
	    {
	      error = insert_object_attr (parser, *rettemp,
					  picwork->val,
					  picwork->attr, picwork->desc);
	      if (error)
		{
		  break;
		}
	    }

	  error = obt_assign (*rettemp, dbattr, 0, ins_val, NULL);
	  return (error == NO_ERROR) ? NO_ERROR : -1;
	}
      error = -1;
    }
  return error;
}
