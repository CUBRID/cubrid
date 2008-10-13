/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_update.c - DO functions for update statements
 * TODO: rename this file to execute_update.c
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "server.h"
#include "parser.h"
#include "db.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "authenticate.h"
#include "semantic_check.h"
#include "xasl_generation_2.h"
#include "msgexec.h"
#include "schema_manager_3.h"
#include "execute_statement_11.h"
#include "transaction_cl.h"
#include "virtual_object_1.h"
#include "qp_mem.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "execute_schema_8.h"
#include "view_transform_1.h"
#include "view_transform_2.h"
#include "network_interface_sky.h"
#include "dbval.h"

#define DB_VALUE_STACK_MAX 40

/* It is used to generate unique savepoint names */
static int savepoint_number = 0;

static void unlink_list (PT_NODE * list);

static QFILE_LIST_ID *get_select_list_to_update (PARSER_CONTEXT * parser,
						 PT_NODE * from,
						 PT_NODE * column_values,
						 PT_NODE * where,
						 PT_NODE * using_index,
						 PT_NODE * class_specs);
static int update_object_attribute (PARSER_CONTEXT * parser,
				    DB_OTMPL * otemplate, PT_NODE * name,
				    DB_ATTDESC * attr_desc, DB_VALUE * value);
static int update_object_tuple (PARSER_CONTEXT * parser, DB_OBJECT * object,
				PT_NODE * list_column_names,
				DB_VALUE * list_values,
				PT_NODE * const_column_names,
				DB_VALUE * const_values,
				DB_ATTDESC ** list_attr_descs,
				DB_ATTDESC ** const_attr_descs,
				PT_NODE * class_, PT_NODE * check_where,
				const int turn_off_unique_check);
static int update_object_by_oid (PARSER_CONTEXT * parser,
				 PT_NODE * statement);
static int update_objs_for_list_file (PARSER_CONTEXT * parser,
				      QFILE_LIST_ID * list_id,
				      PT_NODE * list_column_names,
				      PT_NODE * const_column_names,
				      PT_NODE * const_column_values,
				      PT_NODE * class_, PT_NODE * check_where,
				      const int has_uniques);
static int get_assignment_lists (PARSER_CONTEXT * parser,
				 PT_NODE ** select_names,
				 PT_NODE ** select_values,
				 PT_NODE ** const_names,
				 PT_NODE ** const_values, int *no_vals,
				 int *no_consts, PT_NODE * assign);
static int update_class_attributes (PARSER_CONTEXT * parser,
				    DB_OBJECT * class_obj,
				    PT_NODE * select_names,
				    PT_NODE * select_values,
				    PT_NODE * const_names,
				    PT_NODE * const_values);
static int update_at_server (PARSER_CONTEXT * parser, PT_NODE * from,
			     PT_NODE * statement, PT_NODE ** non_null_attrs,
			     int has_uniques);
static int check_for_constraints (PARSER_CONTEXT * parser, int *has_unique,
				  PT_NODE ** not_nulls, PT_NODE * assignment,
				  DB_OBJECT * class_obj);
static int check_for_fk_cache_attr (PARSER_CONTEXT * parser,
				    PT_NODE * assignment,
				    DB_OBJECT * class_obj);
static int update_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
			      PT_NODE * statement);

/*
 * unlink_list - Unlinks next pointer shortcut of lhs, rhs assignments
 *   return: None
 *   list(in): Node list to cut
 *
 * Note:
 */
static void
unlink_list (PT_NODE * list)
{
  PT_NODE *next;

  while (list)
    {
      next = list->next;
      list->next = NULL;
      list = next;
    }
}

/*
 * get_select_list_to_update -
 *   return: List file if success, otherwise NULL
 *   parser(in): Parser context
 *   from(in): Parse tree of an FROM class
 *   column_values(in): Column list in SELECT clause
 *   where(in): WHERE clause
 *   using_index(in): USING INDEX clause
 *   class_specs(in): Another class specs in FROM clause
 *
 * Note:
 */
static QFILE_LIST_ID *
get_select_list_to_update (PARSER_CONTEXT * parser, PT_NODE * from,
			   PT_NODE * column_values, PT_NODE * where,
			   PT_NODE * using_index, PT_NODE * class_specs)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;

  if (from && (from->node_type == PT_SPEC) && from->info.spec.range_var
      && ((statement = pt_to_upd_del_query (parser, column_values, from,
					    class_specs, where, using_index,
					    0 /* not server update */ )) !=
	  NULL))
    {
      /* If we are updating a proxy, the select is not yet fully translated.
       * If we are updating anything else, this is a no-op.
       */
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
  return result;
}

/*
 * update_object_attribute -
 *   return: Error code
 *   parser(in): Parser context
 *   otemplate(in/out): Class template to be edited
 *   name(in): Parse tree of a attribute name
 *   attr_desc(in): Descriptor of attribute to update
 *   value(in): New attribute value
 *
 * Note: If db_put fails, return an error
 */
static int
update_object_attribute (PARSER_CONTEXT * parser, DB_OTMPL * otemplate,
			 PT_NODE * name, DB_ATTDESC * attr_desc,
			 DB_VALUE * value)
{
  int error;

  if (name->info.name.db_object && db_is_vclass (name->info.name.db_object))
    {
      /* this is a shared attribute of a view.
       * this means this cannot be updated in the template for
       * this real class. Its simply done seperately by a db_put.
       */
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
 * update_object_tuple - Updates object attributes with db_values
 *   return: Error code
 *   object(in): Object to update
 *   list_column_names(in): Name list of columns
 *   list_values(in): Value list of columns
 *   const_column_names(in):
 *   const_values(in):
 *   list_attr_descs(in): List of attribute descriptors
 *   const_attr_descs(in):
 *   class(in): Parse tree of an class spec
 *   check_where(in):
 *   turn_off_unique_check(in):
 *
 * Note:
 */
static int
update_object_tuple (PARSER_CONTEXT * parser, DB_OBJECT * object,
		     PT_NODE * list_column_names, DB_VALUE * list_values,
		     PT_NODE * const_column_names, DB_VALUE * const_values,
		     DB_ATTDESC ** list_attr_descs,
		     DB_ATTDESC ** const_attr_descs, PT_NODE * class_,
		     PT_NODE * check_where, const int turn_off_unique_check)
{
  int error = NO_ERROR;
  DB_OTMPL *otemplate, *otmpl;
  PT_NODE *name;
  int i;
  DB_OBJECT *real_object;
  MOP newobj;
  SM_CLASS *smclass;
  SM_ATTRIBUTE *att;
  DB_VALUE *valptr, retval;
  char flag_att, flag_prc;
  int exist_active_triggers;

  real_object = db_real_instance (object);
  if (real_object == NULL)
    {				/* real_object's fail */
      if ((error = er_errid ()) == NO_ERROR)
	error = ER_GENERIC_ERROR;
      return error;
    }

  newobj = NULL;
  if (object->class_mop)
    {
      error = au_fetch_class (object->class_mop, &smclass, AU_FETCH_READ,
			      AU_SELECT);
      if (error == NO_ERROR && smclass->partition_of)
	{
	  newobj = do_is_partition_changed (parser, smclass,
					    object->class_mop,
					    list_column_names, list_values,
					    const_column_names, const_values);
	}
    }

  if (newobj)
    {
      /* partition */
      exist_active_triggers = sm_active_triggers (smclass);
      if (exist_active_triggers)
	{
	  if (exist_active_triggers < 0)
	    {
	      error = er_errid ();
	      return ((error != NO_ERROR) ? error : ER_GENERIC_ERROR);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
	      return ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	    }
	}
      /* partition change - new insert & old delete */
      otmpl = dbt_create_object_internal (newobj);
      if (otmpl == NULL)
	{
	  error = er_errid ();
	  return ((error != NO_ERROR) ? error : ER_GENERIC_ERROR);
	}

      for (att = smclass->attributes, flag_att = 0; att != NULL;)
	{
	  flag_prc = 0;
	  valptr = list_values;
	  for (name = list_column_names; name != NULL; name = name->next)
	    {
	      if (SM_COMPARE_NAMES
		  (att->header.name, name->info.name.original) == 0)
		{
		  error = dbt_put_internal (otmpl, name->info.name.original,
					    valptr);
		  flag_prc = 1;
		  break;
		}
	      valptr++;
	    }

	  if (!flag_prc)
	    {
	      valptr = const_values;
	      for (name = const_column_names; name != NULL; name = name->next)
		{
		  if (SM_COMPARE_NAMES (att->header.name,
					name->info.name.original) == 0)
		    {
		      error =
			dbt_put_internal (otmpl, name->info.name.original,
					  valptr);
		      flag_prc = 1;
		      break;
		    }
		  valptr++;
		}
	    }

	  if (!flag_prc)
	    {
	      error = db_get (real_object, att->header.name, &retval);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      error = dbt_put_internal (otmpl, att->header.name, &retval);
	      db_value_clear (&retval);
	    }
	  att = (SM_ATTRIBUTE *) att->header.next;
	  if (att == NULL && flag_att == 0)
	    {
	      flag_att++;
	      att = smclass->shared;
	    }
	}
      for (name = list_column_names; name != NULL; name = name->next)
	{
	  db_value_clear (list_values);
	  list_values++;
	}

      if (error != NO_ERROR)
	{
	  (void) dbt_abort_object (otmpl);
	  return error;
	}
      else
	{
	  error = obj_delete (real_object);
	  object = dbt_finish_object (otmpl);
	  if (object == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otmpl);
	      return error;
	    }
	}
    }
  else
    {
      /* noraml */
      otemplate = dbt_edit_object (real_object);
      if (otemplate == NULL)
	{
	  return er_errid ();
	}
      i = 0;
      if (turn_off_unique_check)
	{
	  obt_disable_unique_checking (otemplate);
	}

      for (name = list_column_names; name != NULL; name = name->next)
	{
	  /* if this is the first update, get the attribute descriptor */
	  if (list_attr_descs[i] == NULL)
	    {
	      /* don't get descriptors for shared attrs of views */
	      if (!name->info.name.db_object
		  || !db_is_vclass (name->info.name.db_object))
		{
		  error = db_get_attribute_descriptor (real_object,
						       name->info.name.
						       original, 0, 1,
						       &list_attr_descs[i]);
		}
	      if (error != NO_ERROR)
		break;
	    }

	  if (error == NO_ERROR)
	    {
	      error = update_object_attribute (parser, otemplate, name,
					       list_attr_descs[i],
					       list_values);
	    }
	  db_value_clear (list_values);

	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  i++;
	  list_values++;
	}

      i = 0;
      for (name = const_column_names; name != NULL; name = name->next)
	{
	  /* if this is the first update, get the attribute descriptor */
	  if (const_attr_descs[i] == NULL)
	    {
	      /* don't get descriptors for shared attrs of views */
	      if (!name->info.name.db_object
		  || !db_is_vclass (name->info.name.db_object))
		{
		  error = db_get_attribute_descriptor (real_object,
						       name->info.name.
						       original, 0, 1,
						       &const_attr_descs[i]);
		}
	      if (error != NO_ERROR)
		{
		  break;
		}
	    }

	  if (error == NO_ERROR)
	    {
	      error = update_object_attribute (parser, otemplate, name,
					       const_attr_descs[i],
					       const_values);
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  i++;
	  const_values++;
	}

      if (error != NO_ERROR)
	{
	  (void) dbt_abort_object (otemplate);
	}
      else
	{
	  object = dbt_finish_object (otemplate);
	  if (object == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otemplate);
	    }
	  else
	    {
	      error = mq_evaluate_check_option (parser, check_where, object,
						class_);
	    }
	}
    }

  return error;
}

/*
 * update_object_by_oid - Updates attributes of object by oid
 *   return: 1 if success, otherwise returns error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
static int
update_object_by_oid (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *list_column_names;
  PT_NODE *list_column_values;
  PT_NODE *const_column_names;
  PT_NODE *const_column_values;
  int error = NO_ERROR;
  DB_OBJECT *oid = statement->info.update.object;
  int list_columns = 0;
  int const_columns = 0;
  DB_VALUE list_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE const_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE *list_db_value_list = list_db_value_stack;
  DB_VALUE *const_db_value_list = const_db_value_stack;
  DB_ATTDESC **list_attr_descs = NULL;
  DB_ATTDESC **const_attr_descs = NULL;
  DB_VALUE *db_value = NULL;
  DB_VALUE db_value1;
  int i = 0;
  PT_NODE *pt_value = NULL;
  PT_NODE *node = NULL;
  int no_vals;
  int no_consts;
  PT_NODE *class_;
  PT_NODE *lhs;

  if (!statement->info.update.spec || !(class_
					=
					statement->info.update.spec->info.
					spec.flat_entity_list)
      || !(class_->info.name.db_object))
    {
      PT_INTERNAL_ERROR (parser, "update");
      return ER_GENERIC_ERROR;
    }
  if (!locator_fetch_class (class_->info.name.db_object,
			    DB_FETCH_CLREAD_INSTWRITE))
    {
      return er_errid ();
    }

  error =
    get_assignment_lists (parser, &list_column_names, &list_column_values,
			  &const_column_names, &const_column_values, &no_vals,
			  &no_consts, statement->info.update.assignment);

  lhs = statement->info.update.assignment->info.expr.arg1;
  if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }
  if (lhs->info.name.meta_class == PT_META_ATTR)
    {
      /* we are updating class attributes */
      error = update_class_attributes (parser, class_->info.name.db_object,
				       list_column_names, list_column_values,
				       const_column_names,
				       const_column_values);
    }
  else
    {
      if (!list_column_names && !const_column_names)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
	  error = ER_REGU_SYSTEM;
	}

      if (error == NO_ERROR)
	{
	  list_columns = pt_length_of_list (list_column_names);
	  const_columns = pt_length_of_list (const_column_names);

	  if (list_columns >= DB_VALUE_STACK_MAX)
	    {
	      list_db_value_list = (DB_VALUE *) malloc ((list_columns)
							* sizeof (DB_VALUE));
	    }
	  if (const_columns >= DB_VALUE_STACK_MAX)
	    {
	      const_db_value_list = (DB_VALUE *) malloc ((const_columns)
							 * sizeof (DB_VALUE));
	    }

	  if (!list_db_value_list || !const_db_value_list)
	    {
	      error = ER_REGU_NO_SPACE;
	    }

	  /* allocate attribute descriptors */
	  if (list_columns)
	    {
	      list_attr_descs = (DB_ATTDESC **) malloc ((list_columns)
							*
							sizeof (DB_ATTDESC
								*));
	      if (!list_attr_descs)
		{
		  error = ER_REGU_NO_SPACE;
		}
	      for (i = 0; i < list_columns; i++)
		{
		  list_attr_descs[i] = NULL;
		}
	    }
	  if (const_columns)
	    {
	      const_attr_descs = (DB_ATTDESC **) malloc ((const_columns)
							 *
							 sizeof (DB_ATTDESC
								 *));
	      if (!const_attr_descs)
		{
		  error = ER_REGU_NO_SPACE;
		}
	      for (i = 0; i < const_columns; i++)
		{
		  const_attr_descs[i] = NULL;
		}
	    }
	}

      if (error == NO_ERROR)
	{
	  i = 0;
	  for (pt_value = const_column_values; pt_value != NULL; pt_value
	       = pt_value->next)
	    {
	      db_value = pt_value_to_db (parser, pt_value);

	      if (db_value)
		{
		  const_db_value_list[i] = *db_value;
		}
	      else
		{
		  /* this is probably an error condition */
		  db_make_null ((&const_db_value_list[i]));
		}

	      i++;
	    }

	  i = 0;
	  for (node = list_column_values; node != NULL; node = node->next)
	    {
	      error = mq_evaluate_expression_having_serial (parser, node,
							    &db_value1, oid,
							    list_column_names->
							    info.name.
							    spec_id);

	      if (error < NO_ERROR)
		{
		  break;
		}

	      list_db_value_list[i] = db_value1;

	      i++;
	    }

	  if (error >= NO_ERROR)
	    {
	      error = update_object_tuple (parser, oid, list_column_names,
					   list_db_value_list,
					   const_column_names,
					   const_db_value_list,
					   list_attr_descs, const_attr_descs,
					   class_,
					   statement->info.update.check_where,
					   0);
	    }

	}

      if (list_db_value_list != list_db_value_stack)
	{
	  free_and_init (list_db_value_list);
	}

      if (const_db_value_list != const_db_value_stack)
	{
	  free_and_init (const_db_value_list);
	}

      /* free attribute descriptors */
      if (list_attr_descs)
	{
	  for (i = 0; i < list_columns; i++)
	    {
	      if (list_attr_descs[i])
		{
		  db_free_attribute_descriptor (list_attr_descs[i]);
		}
	    }
	  free_and_init (list_attr_descs);
	}
      if (const_attr_descs)
	{
	  for (i = 0; i < const_columns; i++)
	    {
	      if (const_attr_descs[i])
		{
		  db_free_attribute_descriptor (const_attr_descs[i]);
		}
	    }
	  free_and_init (const_attr_descs);
	}
    }

  if (error < NO_ERROR)
    return error;
  else
    return 1;			/* we successfully updated 1 object */
}

/*
 * update_objs_for_list_file - Updates oid attributes for every oid
 *				in a list file
 *   return: Number of affected objects if success, otherwise an error code
 *   parser(in): Parser context
 *   list_id(in): A list file of oid's and values
 *   list_column_names(in): Name list of columns
 *   const_column_names(in):
 *   const_column_values(in):
 *   class(in):
 *   check_where(in):
 *   has_uniques(in):
 *
 * Note:
 */
static int
update_objs_for_list_file (PARSER_CONTEXT * parser, QFILE_LIST_ID * list_id,
			   PT_NODE * list_column_names,
			   PT_NODE * const_column_names,
			   PT_NODE * const_column_values, PT_NODE * class_,
			   PT_NODE * check_where, const int has_uniques)
{
  int error = NO_ERROR;
  int cursor_status;
  int list_columns = 0;
  int const_columns = 0;
  DB_VALUE list_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE const_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE *list_db_value_list = list_db_value_stack;
  DB_VALUE *const_db_value_list = const_db_value_stack;
  DB_ATTDESC **list_attr_descs = NULL;
  DB_ATTDESC **const_attr_descs = NULL;
  DB_VALUE *db_value = NULL;
  DB_VALUE oid_value;
  CURSOR_ID cursor_id;
  int count = 0;
  int i = 0;
  PT_NODE *pt_value;
  const char *savepoint_name = NULL;
  int turn_off_unique_check;

  if (!list_column_names && !const_column_names)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
      error = ER_REGU_SYSTEM;
      goto done;
    }

  list_columns = pt_length_of_list (list_column_names);
  if (list_columns >= DB_VALUE_STACK_MAX)
    {
      list_db_value_list = (DB_VALUE *) malloc ((list_columns)
						* sizeof (DB_VALUE));
      if (!list_db_value_list)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
    }

  const_columns = pt_length_of_list (const_column_names);
  if (const_columns >= DB_VALUE_STACK_MAX)
    {
      const_db_value_list = (DB_VALUE *) malloc ((const_columns)
						 * sizeof (DB_VALUE));
      if (!const_db_value_list)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
    }

  /* allocate attribute descriptors */
  if (list_columns)
    {
      list_attr_descs = (DB_ATTDESC **) malloc ((list_columns)
						* sizeof (DB_ATTDESC *));
      if (!list_attr_descs)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
      for (i = 0; i < list_columns; i++)
	{
	  list_attr_descs[i] = NULL;
	}
    }
  if (const_columns)
    {
      const_attr_descs = (DB_ATTDESC **) malloc ((const_columns)
						 * sizeof (DB_ATTDESC *));
      if (!const_attr_descs)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
      for (i = 0; i < const_columns; i++)
	{
	  const_attr_descs[i] = NULL;
	}
    }

  i = 0;
  for (pt_value = const_column_values; pt_value != NULL; pt_value
       = pt_value->next)
    {
      db_value = pt_value_to_db (parser, pt_value);

      if (db_value)
	{
	  const_db_value_list[i] = *db_value;
	}
      else
	{
	  /* this is probably an error condition */
	  db_make_null ((&const_db_value_list[i]));
	}
      i++;
    }

  /* if the list file contains more than 1 object we need to savepoint
   * the statement to guarantee statement atomicity.
   */
  if (list_id->tuple_cnt > 1 || check_where || has_uniques)
    {
      savepoint_name = mq_generate_name (parser, "UusP", &savepoint_number);
      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	{
	  goto done;
	}
    }

  /* 'turn_off_unique_check' is used when call update_object_tuple(). */
  if (list_id->tuple_cnt == 1)
    {
      /* Instance level uniqueness checking is performed on the server
       * when a new single row is inserted.
       */
      turn_off_unique_check = 0;
    }
  else
    {
      /* list_id->tuple_cnt > 1 : multiple row update
       * Statment level uniqueness checking is performed on the client
       */
      turn_off_unique_check = 1;
    }

  if (!cursor_open (&cursor_id, list_id, false, true))
    {
      error = ER_GENERIC_ERROR;
      if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
      goto done;
    }
  cursor_id.query_id = parser->query_id;

  /* set prefetching lock mode to WRITE access since we'll be
   * updating all the objects in the list file.
   */
  (void) cursor_set_prefetch_lock_mode (&cursor_id, DB_FETCH_WRITE);

  cursor_status = cursor_next_tuple (&cursor_id);

  while (cursor_status == DB_CURSOR_SUCCESS)
    {

      /* the first item on the db_value_list's is an oid.
       * the rest are values to assign to the corresponding
       * oid column names.
       */
      if (cursor_get_current_oid (&cursor_id, &oid_value) != NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      if (cursor_get_tuple_value_list
	  (&cursor_id, pt_length_of_list (list_column_names),
	   list_db_value_list) != NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      if (DB_VALUE_TYPE (&oid_value) == DB_TYPE_NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_OBJECT_ID_NOT_SET,
		  1, class_->info.name.original);
	  error = ER_SM_OBJECT_ID_NOT_SET;
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      error = update_object_tuple (parser, DB_GET_OBJECT (&oid_value),
				   list_column_names, list_db_value_list,
				   const_column_names, const_db_value_list,
				   list_attr_descs, const_attr_descs, class_,
				   check_where, turn_off_unique_check);

      if (error < NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      count++;			/* number of objects affected */
      cursor_status = cursor_next_tuple (&cursor_id);
    }

  if (cursor_status != DB_CURSOR_END)
    {
      error = ER_GENERIC_ERROR;
      cursor_close (&cursor_id);
      if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
      goto done;
    }
  cursor_close (&cursor_id);

  /* check uniques */
  if (has_uniques)
    {
      error = sm_flush_for_multi_update (class_->info.name.db_object);
      /* if error and a savepoint was created, rollback to savepoint.
       * No need to rollback if the TM aborted the transaction itself.
       */
      if ((error < NO_ERROR) && savepoint_name && (error
						   !=
						   ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
    }

done:

  if (list_db_value_list && list_db_value_list != list_db_value_stack)
    {
      free_and_init (list_db_value_list);
    }

  if (const_db_value_list && const_db_value_list != const_db_value_stack)
    {
      free_and_init (const_db_value_list);
    }

  /* free attribute descriptors */
  if (list_attr_descs)
    {
      for (i = 0; i < list_columns; i++)
	{
	  if (list_attr_descs[i])
	    {
	      db_free_attribute_descriptor (list_attr_descs[i]);
	    }
	}
      free_and_init (list_attr_descs);
    }
  if (const_attr_descs)
    {
      for (i = 0; i < const_columns; i++)
	{
	  if (const_attr_descs[i])
	    {
	      db_free_attribute_descriptor (const_attr_descs[i]);
	    }
	}
      free_and_init (const_attr_descs);
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
 * get_assignment_lists - Returns corresponding lists of names and expressions
 *   return: Error code
 *   parser(in): Parser context
 *   select_names(out):
 *   select_values(out):
 *   const_names(out):
 *   const_values(out):
 *   assign(in): Parse tree of assignment lists
 *
 * Note:
 */
static int
get_assignment_lists (PARSER_CONTEXT * parser, PT_NODE ** select_names,
		      PT_NODE ** select_values, PT_NODE ** const_names,
		      PT_NODE ** const_values, int *no_vals, int *no_consts,
		      PT_NODE * assign)
{
  int error = NO_ERROR;
  PT_NODE *lhs;
  PT_NODE *rhs;
  PT_NODE *att;

  if (select_names)
    {
      *select_names = NULL;
    }
  if (select_values)
    {
      *select_values = NULL;
    }
  if (const_names)
    {
      *const_names = NULL;
    }
  if (const_values)
    {
      *const_values = NULL;
    }
  if (no_vals)
    {
      *no_vals = 0;
    }
  if (no_consts)
    {
      *no_consts = 0;
    }

  while (assign)
    {
      if (assign->node_type != PT_EXPR || assign->info.expr.op != PT_ASSIGN
	  || !(lhs = assign->info.expr.arg1)
	  || !(rhs = assign->info.expr.arg2) || !(lhs->node_type == PT_NAME
						  ||
						  PT_IS_N_COLUMN_UPDATE_EXPR
						  (lhs)) || !select_values
	  || !select_names || !const_values || !const_names || !no_vals
	  || !no_consts)
	{
	  /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "system error detected in %s, line %d.\n",
		   __FILE__, __LINE__);
#endif
	  return ER_GENERIC_ERROR;
	}

      if (lhs->node_type == PT_NAME)
	{
	  ++(*no_vals);
	}
      else
	{			/* PT_IS_N_COLUMN_UPDATE_EXPR(lhs) == true */
	  lhs = lhs->info.expr.arg1;
	  for (att = lhs; att; att = att->next)
	    {
	      if (att->node_type != PT_NAME)
		{
#if defined(CUBRID_DEBUG)
		  fprintf (stdout, "system error detected in %s, line %d.\n",
			   __FILE__, __LINE__);
#endif
		  return ER_GENERIC_ERROR;
		}
	      ++(*no_vals);
	    }
	}
      if (!PT_IS_CONST (rhs)
	  || (PT_IS_HOSTVAR (rhs) && parser->set_host_var == 0))
	{
	  /* assume evaluation needed. */
	  if (*select_names == NULL)
	    {
	      *select_names = lhs;
	      *select_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *select_names);
	      parser_append_node (rhs, *select_values);
	    }
	}
      else
	{
	  ++(*no_consts);
	  /* we already have a constant value */
	  if (*const_names == NULL)
	    {
	      *const_names = lhs;
	      *const_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *const_names);
	      parser_append_node (rhs, *const_values);
	    }
	}
      assign = assign->next;
    }

  return error;
}

/*
 * update_class_attributes -
 *   return: Error code
 *   parser(in): Parser context
 *   class_obj(in/out): Class template to be edited
 *   select_names(in):
 *   select_values(in):
 *   const_names(in):
 *   const_values(in):
 *
 * Note:
 */
static int
update_class_attributes (PARSER_CONTEXT * parser, DB_OBJECT * class_obj,
			 PT_NODE * select_names, PT_NODE * select_values,
			 PT_NODE * const_names, PT_NODE * const_values)
{
  int error = NO_ERROR;
  DB_VALUE *db_value = NULL, val;
  PT_NODE *q, *p;
  DB_OTMPL *otemplate = NULL;

  if (!select_names && !const_names)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
      return ER_REGU_SYSTEM;
    }

  otemplate = dbt_edit_object (class_obj);

  for (p = const_names, q = const_values; (error == NO_ERROR) && p && q; p
       = p->next, q = q->next)
    {

      if (p->info.name.meta_class != PT_META_ATTR)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_REGU_MIX_CLASS_NONCLASS_UPDATE, 0);
	  error = ER_REGU_MIX_CLASS_NONCLASS_UPDATE;
	}
      else
	{
	  db_value = pt_value_to_db (parser, q);

	  if (db_value)
	    {
	      error = dbt_put_internal (otemplate, p->info.name.original,
					db_value);
	    }
	  else
	    {
	      error = ER_GENERIC_ERROR;
	    }
	}
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_object (otemplate);
      return error;
    }

  for (p = select_names, q = select_values; (error == NO_ERROR) && p && q; p
       = p->next, q = q->next)
    {

      if (p->info.name.meta_class != PT_META_ATTR)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_REGU_MIX_CLASS_NONCLASS_UPDATE, 0);
	  error = ER_REGU_MIX_CLASS_NONCLASS_UPDATE;
	}
      else
	{
	  pt_evaluate_tree (parser, q, &val);
	  if (parser->error_msgs)
	    {
	      db_value = NULL;
	    }
	  else
	    {
	      db_value = &val;
	    }

	  if (db_value)
	    {
	      error = dbt_put_internal (otemplate, p->info.name.original,
					db_value);
	    }
	  else
	    {
	      error = ER_GENERIC_ERROR;
	    }
	}
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_object (otemplate);
      return error;
    }

  if (dbt_finish_object (otemplate) == NULL)
    {
      error = er_errid ();
      (void) dbt_abort_object (otemplate);

      return error;
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * update_at_server - Build an xasl tree for a server update and execute it
 *   return: Tuple count if success, otherwise an error code
 *   parser(in): Parser context
 *   from(in): Class spec to update
 *   statement(in): Parse tree of a update statement
 *   non_null_attrs(in):
 *   has_uniques(in):
 *
 * Note:
 *  The xasl tree has an UPDATE_PROC node as the top node and
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
 *  instance OID and any update attribute expression values.
 *  The UPDATE_PROC node scans the BUILDLIST_PROC results.
 *  The UPDATE_PROC node contains the attribute ID's and values
 *  for update constants.  The server executes the aptr and then
 *  for each instance selected, updates it with the attribute expression
 *  values and constants.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.
 *
 *  The instances for the class are flushed from the client before the
 *  update is executed.  If any instances are updated, the instances are
 *  decached from the client after the update is executed.
 *
 *  It is assumed that class attributes and regular attributes
 *  are not mixed in the same update statement.
 */
static int
update_at_server (PARSER_CONTEXT * parser, PT_NODE * from,
		  PT_NODE * statement, PT_NODE ** non_null_attrs,
		  int has_uniques)
{
  int error = NO_ERROR;
  PT_NODE *select_names;
  PT_NODE *select_values;
  PT_NODE *const_names;
  PT_NODE *const_values;
  int i, j;
  int no_vals;
  int no_consts;
  XASL_NODE *xasl = NULL;
  int size, count = 0;
  char *stream = NULL;
  int query_id = -1;
  QFILE_LIST_ID *list_id = NULL;
  PT_NODE *cl_name_node;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  error = get_assignment_lists (parser, &select_names, &select_values,
				&const_names, &const_values, &no_vals,
				&no_consts,
				statement->info.update.assignment);

  if (error == NO_ERROR)
    {
      xasl =
	pt_to_update_xasl (parser, statement, select_names, select_values,
			   const_names, const_values, no_vals, no_consts,
			   has_uniques, non_null_attrs);
    }

  if (xasl)
    {
      UPDATE_PROC_NODE *update = &xasl->proc.update;

      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}

      for (i = 0; i < update->no_consts; i++)
	{
	  pr_clear_value (update->consts[i]);
	}
      for (i = 0; i < update->no_classes; i++)
	{
	  if (update->partition[i])
	    {
	      for (j = 0; j < update->partition[i]->no_parts; j++)
		{
		  pr_clear_value (update->partition[i]->parts[j]->vals);
		}
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
      error = query_prepare_and_execute (stream, size, &query_id,
					 parser->host_var_count +
					 parser->auto_param_count,
					 parser->host_variables, &list_id,
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
	  for (cl_name_node = from->info.spec.flat_entity_list; cl_name_node
	       && error == NO_ERROR; cl_name_node = cl_name_node->next)
	    {
	      error =
		sm_flush_and_decache_objects (cl_name_node->info.name.
					      db_object, true);
	    }
	}
      regu_free_listid (list_id);
    }
  pt_end_query (parser);

  unlink_list (const_names);
  unlink_list (const_values);
  unlink_list (select_names);
  unlink_list (select_values);

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
 * check_for_constraints - Determine whether attributes of the target class have
 *                         UNIQUE and/or NOT NULL constratins, and return a list
 *			   of NOT NULL attributes if exist
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(out): Indicator representing there is UNIQUE constraint, 1 or 0
 *   not_nulls(out): A list of pointers to NOT NULL attributes, or NULL
 *   assignment(in):  Parse tree of an assignment part of the UPDATE statement
 *   class_obj(in): Class object of the target spec
 *
 * Note:
 */
static int
check_for_constraints (PARSER_CONTEXT * parser, int *has_unique,
		       PT_NODE ** not_nulls, PT_NODE * assignment,
		       DB_OBJECT * class_obj)
{
  PT_NODE *lhs, *att, *pointer;

  *has_unique = 0;
  *not_nulls = NULL;

  for (; assignment; assignment = assignment->next)
    {
      lhs = assignment->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  att = lhs;
	}
      else if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  att = lhs->info.expr.arg1;
	}
      else
	{
	  /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "system error detected in %s, line %d.\n",
		   __FILE__, __LINE__);
#endif
	  return ER_GENERIC_ERROR;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type != PT_NAME)
	    {
	      /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "system error detected in %s, line %d.\n",
		       __FILE__, __LINE__);
#endif
	      return ER_GENERIC_ERROR;
	    }
	  if (*has_unique == 0 && sm_att_unique_constrained (class_obj,
							     att->info.name.
							     original))
	    {
	      *has_unique = 1;
	    }
	  if (sm_att_constrained (class_obj, att->info.name.original,
				  SM_ATTFLAG_NON_NULL))
	    {
	      pointer = pt_point (parser, att);
	      if (pointer == NULL)
		{
		  PT_ERRORm (parser, att, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  if (*not_nulls)
		    {
		      parser_free_tree (parser, *not_nulls);
		    }
		  *not_nulls = NULL;
		  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
		}
	      *not_nulls = parser_append_node (pointer, *not_nulls);
	    }
	}			/* for ( ; attr; ...) */
    }				/* for ( ; assignment; ...) */

  return NO_ERROR;
}

/*
 * check_for_fk_cache_attr() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   assignment(in): Parse tree of an assignment clause
 *   class_obj(in): Class object to be checked
 *
 * Note:
 */
static int
check_for_fk_cache_attr (PARSER_CONTEXT * parser, PT_NODE * assignment,
			 DB_OBJECT * class_obj)
{
  PT_NODE *lhs, *att;

  for (; assignment; assignment = assignment->next)
    {
      lhs = assignment->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  att = lhs;
	}
      else if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  att = lhs->info.expr.arg1;
	}
      else
	{
	  return ER_GENERIC_ERROR;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type != PT_NAME)
	    {
	      return ER_GENERIC_ERROR;
	    }

	  if (sm_is_att_fk_cache (class_obj, att->info.name.original))
	    {
	      PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_CANT_ASSIGN_FK_CACHE_ATTR,
			  att->info.name.original);
	      return ER_PT_SEMANTIC;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * update_real_class() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   spec(in): Parse tree of a class spec to update
 *   statement(in): Parse tree of a update statement
 *
 * Note: If the statement is of type "update class foo ...", this
 *   routine updates class attributes of foo.  If the statement is of
 *   type "update foo ...", this routine updates objects or rows in foo.
 *   It is assumed that class attributes and regular attributes
 *   are not mixed in the same update statement.
 */
static int
update_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
		   PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *select_names;
  PT_NODE *select_values;
  PT_NODE *const_names;
  PT_NODE *const_values;
  PT_NODE *non_null_attrs = NULL;
  PT_NODE *lhs;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *oid_list = NULL;
  int trigger_involved;
  int no_vals;
  int no_consts;
  int server_allowed;
  int has_uniques;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  /* update a "real" class in this database */

  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;

  /* The IX lock on the class is sufficient.
   * DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE
   */
  if (!locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE))
    {
      goto exit_on_error;
    }

  error = sm_class_has_triggers (class_obj, &trigger_involved);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }
  error = check_for_constraints (parser, &has_uniques, &non_null_attrs,
				 statement->info.update.assignment,
				 class_obj);
  if (error < NO_ERROR)
    {
      goto exit_on_error;
    }

  error = check_for_fk_cache_attr (parser, statement->info.update.assignment,
				   class_obj);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Check to see if the update can be done on the server */
  server_allowed = ((!trigger_involved)
		    && (spec->info.spec.flat_entity_list->info.name.
			virt_object == NULL));
  lhs = statement->info.update.assignment->info.expr.arg1;
  if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }
  if (lhs->info.name.meta_class != PT_NORMAL)
    {
      server_allowed = 0;
    }

  if (server_allowed)
    {
      /* do update on server */
      error = update_at_server (parser, spec, statement, &non_null_attrs,
				has_uniques);
    }
  else
    {
      /* do update on client */
      error = get_assignment_lists (parser, &select_names, &select_values,
				    &const_names, &const_values, &no_vals,
				    &no_consts,
				    statement->info.update.assignment);
      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class != PT_META_ATTR)
	{

	  hint_arg = statement->info.update.waitsecs_hint;
	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = atof (hint_arg->info.name.original);
	      if (waitsecs >= -1)
		{
		  old_waitsecs = TM_TRAN_WAITSECS ();
		  (void) tran_reset_wait_times (waitsecs);
		}
	    }
	  if (error == NO_ERROR)
	    {
	      /* get the oid's and new values */
	      oid_list =
		get_select_list_to_update (parser, spec, select_values,
					   statement->info.update.search_cond,
					   statement->info.update.using_index,
					   statement->info.update.
					   class_specs);
	    }
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }

	  if (!oid_list)
	    {
	      /* an error should be set already, don't lose it */
	      error = ER_GENERIC_ERROR;
	      goto exit_on_error;
	    }

	  /* update each oid */
	  error = update_objs_for_list_file (parser, oid_list, select_names,
					     const_names, const_values,
					     spec->info.spec.flat_entity_list,
					     statement->info.update.
					     check_where, has_uniques);

	  regu_free_listid (oid_list);
	  pt_end_query (parser);
	}
      else
	{
	  /* we are updating class attributes */
	  error = update_class_attributes (parser, class_obj, select_names,
					   select_values, const_names,
					   const_values);
	}

      /* restore tree structure */
      unlink_list (const_names);
      unlink_list (const_values);
      unlink_list (select_names);
      unlink_list (select_values);

    }

wrapup:

  if (non_null_attrs)
    {
      parser_free_tree (parser, non_null_attrs);
    }

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      error = er_errid ();
    }

  goto wrapup;
}

/*
 * do_update() - Updates objects or rows
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
int
do_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  int result = NO_ERROR;
  PT_NODE *spec;
  int rollbacktosp = 0;
  const char *savepoint_name = NULL;

  /* DON'T REMOVE this, correct authorization validation of views
   * depends on this.
   *
   * DON'T return from the body of this function. Break out of the loop
   * if necessary.
   */
  AU_DISABLE (parser->au_save);

  /* if the update statement contains more than one update component,
   * we savepoint the update components to try to guarantee update
   * statement atomicity.
   */

  while (statement && (error >= 0))
    {
      if (statement->node_type != PT_UPDATE
	  || statement->info.update.assignment == NULL)
	{
	  /* bullet proofing, should not get here */
	  PT_INTERNAL_ERROR (parser, "update");
	  error = ER_GENERIC_ERROR;
	  break;
	}

      spec = statement->info.update.spec;

      if (pt_false_where (parser, statement))
	{
	  /* nothing to update, where part is false */
	}
      else if (statement->info.update.object != NULL)
	{
	  /* this is a update object if it has an object */
	  error = update_object_by_oid (parser, statement);
	}
      else
	{
	  /* the following is the "normal" sqlx type execution */
	  error = update_real_class (parser, spec, statement);
	}

      if (error < NO_ERROR && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg ());
	}

      result += error;
      statement = statement->next;
    }

  /* if error and a savepoint was created, rollback to savepoint.
   * No need to rollback if the TM aborted the transaction.
   */
  if ((error < NO_ERROR) && rollbacktosp && (error
					     != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  if (error < 0)
    {
      result = error;
    }

  /* DON'T REMOVE this, correct authorization validation of views
   * depends on this.
   */
  AU_ENABLE (parser->au_save);

  return result;
}

/*
 * do_execute_update() - Prepare the UPDATE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a update statement
 *
 * Note:
 */
int
do_prepare_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  PT_NODE *flat, *not_nulls, *lhs;
  DB_OBJECT *class_obj;
  int has_trigger, has_unique, au_save;
  bool server_update;
  PT_NODE *select_names, *select_values, *const_names, *const_values;
  int no_vals, no_consts;
  XASL_ID *xasl_id;
  const char *qstr;

  for (err = NO_ERROR; statement && (err >= NO_ERROR); statement
       = statement->next)
    {
      /* there can be no results, this is a compile time false where clause */
      if (pt_false_where (parser, statement))
	{
	  /* tell to the execute routine that there's no XASL to execute */
	  statement->xasl_id = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      /*
       * Update object case:
       *   this is a update object if it has an object
       */
      if (statement->info.update.object)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      /* if already prepared */
      if (statement->xasl_id)
	{
	  continue;		/* continue to next UPDATE statement */
	}

      flat = statement->info.update.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* the presence of a proxy trigger should force the update
         to be performed through the workspace  */
      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */
      err = sm_class_has_triggers (class_obj, &has_trigger);
      AU_RESTORE (au_save);
      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.update.has_trigger = (bool) has_trigger;

      err = check_for_fk_cache_attr (parser,
				     statement->info.update.assignment,
				     class_obj);
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      /* check if the target class has UNIQUE constraint and
         get attributes that has NOT NULL constraint */
      err = check_for_constraints (parser, &has_unique, &not_nulls,
				   statement->info.update.assignment,
				   class_obj);
      if (err < NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      statement->info.update.has_unique = (bool) has_unique;

      /* determine whether it can be server-side or OID list update */
      server_update = (!has_trigger && (flat->info.name.virt_object == NULL));
      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class != PT_NORMAL)
	{
	  server_update = false;
	}
      statement->info.update.server_update = server_update;

      /* if we are updating class attributes, not need to prepare */
      if (lhs->info.name.meta_class == PT_META_ATTR)
	{
	  statement->info.update.do_class_attrs = true;
	  continue;		/* continue to next UPDATE statement */
	}

      if (server_update)
	{
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
	}

      /* get lists of names and values (expressions and constants)
         from the assignment part of UPDATE statement */
      err = get_assignment_lists (parser, &select_names, &select_values,
				  &const_names, &const_values, &no_vals,
				  &no_consts,
				  statement->info.update.assignment);
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      xasl_id = NULL;
      if (server_update)
	{
	  /*
	   * Server-side update case: (by requesting server to execute XASL)
	   *  build UPDATE_PROC XASL
	   */
	  XASL_NODE *xasl;
	  char *stream;
	  int size;

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

	      /* pt_to_update_xasl() will build XASL tree from parse tree */
	      xasl = pt_to_update_xasl (parser, statement, select_names,
					select_values, const_names,
					const_values, no_vals, no_consts,
					has_unique, &not_nulls);
	      AU_RESTORE (au_save);
	      stream = NULL;
	      if (xasl && (err >= NO_ERROR))
		{
		  int i, j;
		  UPDATE_PROC_NODE *update = &xasl->proc.update;

		  /* convert the created XASL tree to the byte stream for
		     transmission to the server */
		  err = xts_map_xasl_to_stream (xasl, &stream, &size);
		  if (err != NO_ERROR)
		    {
		      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		    }
		  for (i = 0; i < update->no_consts; i++)
		    {
		      pr_clear_value (update->consts[i]);
		    }
		  for (i = 0; i < update->no_classes; i++)
		    {
		      if (update->partition[i])
			{
			  for (j = 0; j < update->partition[i]->no_parts; j++)
			    {
			      pr_clear_value (update->partition[i]->parts[j]->
					      vals);
			    }
			}
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
	  else
	    {			/* if (!xasl_id) */
	      while (flat)
		{
		  if (locator_flush_class (flat->info.name.db_object)
		      != NO_ERROR)
		    {
		      xasl_id = NULL;
		      err = er_errid ();
		      break;
		    }
		  flat = flat->next;
		}
	    }
	}
      else
	{			/* if (server_update) */
	  /*
	   * OID list update case: (by selecting OIDs to update)
	   *  make SELECT statement for this UPDATE statement
	   */
	  PT_NODE *select_statement;

	  /* make sure that lhs->info.name.meta_class != PT_META_ATTR */
	  select_statement = pt_to_upd_del_query (parser, select_values,
						  statement->info.update.spec,
						  statement->info.update.
						  class_specs,
						  statement->info.update.
						  search_cond,
						  statement->info.update.
						  using_index, 0);

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

	}			/* else (server_update) */

      /* save the XASL_ID that is allocated and returned by
         query_prepare() into 'statement->xasl_id'
         to be used by do_execute_update() */
      statement->xasl_id = xasl_id;

      if (not_nulls)
	{
	  parser_free_tree (parser, not_nulls);	/* check_for_constraints() */
	}

      /* restore tree structure; get_assignment_lists() */
      unlink_list (const_names);
      unlink_list (const_values);
      unlink_list (select_names);
      unlink_list (select_values);
    }

  return err;
}

/*
 * do_execute_update() - Execute the prepared UPDATE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
int
do_execute_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err, result;
  PT_NODE *flat;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  PT_NODE *select_names, *select_values, *const_names, *const_values;
  int no_vals, no_consts;
  QFILE_LIST_ID *list_id;
  int au_save;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  /* If the UPDATE statement contains more than one update component,
     we savepoint the update components to try to guarantee UPDATE statement
     atomicity. */
  savepoint_name = NULL;

  for (err = NO_ERROR, result = 0; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
      /*
       * Update object case:
       *   update object by OID
       */
      if (statement->info.update.object)
	{
	  err = update_object_by_oid (parser, statement);
	  continue;		/* continue to next UPDATE statement */
	}

      /* check if this statement is not necessary to execute,
         e.g. false where or not prepared correctly;
         Note that in LDB case, the statement was not prepared. */
      if (!statement->info.update.do_class_attrs && !statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      flat = statement->info.update.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* The IX lock on the class is sufficient.
         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
	{
	  err = er_errid ();
	  break;		/* stop while loop if error */
	}

      /*
       * Server-side update or OID list update case:
       *  execute the prepared(stored) XASL (UPDATE_PROC or SELECT statement)
       */
      if (statement->info.update.server_update
	  || !statement->info.update.do_class_attrs)
	{
	  /* Request that the server executes the stored XASL, which is
	     the execution plan of the prepared query, with the host variables
	     given by users as parameter values for the query.
	     As a result, query id and result file id (QFILE_LIST_ID) will be
	     returned.
	     do_prepare_update() has saved the XASL file id (XASL_ID) in
	     'statement->xasl_id' */

	  int query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;

	  /* flush necessary objects before execute */
	  err = sm_flush_objects (class_obj);
	  if (err != NO_ERROR)
	    {
	      break;		/* stop while loop if error */
	    }

	  if (statement->do_not_keep == 0)
	    {
	      query_flag |= KEEP_PLAN_CACHE;
	    }
	  query_flag |= NOT_FROM_RESULT_CACHE;
	  query_flag |= RESULT_CACHE_INHIBITED;

	  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
	  list_id = NULL;
	  parser->query_id = -1;
	  err = query_execute (statement->xasl_id, &parser->query_id,
			       parser->host_var_count +
			       parser->auto_param_count,
			       parser->host_variables, &list_id, query_flag,
			       NULL, NULL);
	  AU_RESTORE (au_save);
	  if (err != NO_ERROR)
	    {
	      err = er_errid ();
	    }
	}

      if (!statement->info.update.server_update)
	{
	  /* get lists of names and values (expressions and constants)
	     from the assignment part of UPDATE statement */
	  err = get_assignment_lists (parser, &select_names, &select_values,
				      &const_names, &const_values, &no_vals,
				      &no_consts,
				      statement->info.update.assignment);

	  hint_arg = statement->info.update.waitsecs_hint;
	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
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
	  if (statement->info.update.do_class_attrs)
	    {
	      /* in case of update class attributes, */
	      err = update_class_attributes (parser, class_obj, select_names,
					     select_values, const_names,
					     const_values);
	    }
	  else
	    {
	      /* in the case of OID list update, now update the seleted OIDs */
	      err = update_objs_for_list_file (parser, list_id, select_names,
					       const_names, const_values,
					       flat,
					       statement->info.update.
					       check_where,
					       (int) statement->info.update.
					       has_unique);
	    }

	  AU_RESTORE (au_save);
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }

	  /* restore tree structure; get_assignment_lists() */
	  unlink_list (const_names);
	  unlink_list (const_values);
	  unlink_list (select_names);
	  unlink_list (select_values);
	}

      if (statement->info.update.server_update
	  || !statement->info.update.do_class_attrs)
	{
	  /* free returned QFILE_LIST_ID */
	  if (list_id)
	    {
	      if (list_id->tuple_cnt > 0
		  && statement->info.update.server_update)
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
	  pt_end_query (parser);
	}

      /* accumulate intermediate results */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      /* To avoid violating the atomicity of a SQL update statement,
         we need to flush in the proxy case. Otherwise, Opal's
         delayed flushing of dirty proxies can compromise the
         (perceived) atomicity of this SQL update. For example,
         a proxy update may cause a constraint violation at the
         target ldb and if this is so, we need to provoke that
         violation now, not later. */
      if ((err >= NO_ERROR) && class_obj && db_is_vclass (class_obj))
	{
	  err = sm_flush_objects (class_obj);
	}

      if ((err < NO_ERROR) && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg ());
	}
    }

  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name
      && (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  return (err < NO_ERROR) ? err : result;
}
