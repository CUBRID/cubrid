/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_alter.c - DO functions for alter statements
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "parser.h"
#include "msgexec.h"
#include "dbi.h"
#include "semantic_check.h"
#include "execute_schema_2.h"
#include "execute_statement_11.h"
#include "schema_manager_3.h"
#include "transaction_cl.h"
#include "execute_schema_8.h"
#include "system_parameter.h"
#include "dbval.h"

#define UNIQUE_SAVEPOINT_ADD_ATTR_MTHD "aDDaTTRmTHD"

/*
 * do_alter() -
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of an alter statement
 */
int
do_alter (PARSER_CONTEXT * parser, PT_NODE * alter)
{
  const char *entity_name, *new_query;
  const char *attr_name, *mthd_name, *mthd_file, *attr_mthd_name;
  const char *new_name, *old_name, *domain;
  DB_CTMPL *ctemplate = NULL;
  DB_OBJECT *vclass, *sup_class;
  int error = NO_ERROR;
  DB_ATTRIBUTE *found_attr, *def_attr;
  DB_METHOD *found_mthd;
  DB_DOMAIN *def_domain;
  DB_VALUE src_val, dest_val;
  DB_TYPE db_desired_type;
  int query_no, class_attr;
  PT_NODE *vlist, *p, *n, *d;
  PT_NODE *node, *nodelist;
  PT_NODE *data_type, *data_default, *path;
  PT_NODE *slist, *parts, *coalesce_list, *names, *delnames;
  PT_TYPE_ENUM pt_desired_type;
#if 0
  HFID *hfid;
#endif
  char keycol[DB_MAX_IDENTIFIER_LENGTH], partnum_str[32];
  MOP classop;
  SM_CLASS *class_, *subcls;
  DB_OBJLIST *objs;
  SM_CLASS_CONSTRAINT *cons;
  SM_ATTRIBUTE **attp;
  char **namep = NULL, **attrnames;
  int *asc_desc = NULL;
  int i, partnum, coalesce_num;
  SM_CLASS *smclass;
  TP_DOMAIN *key_type;
  bool partition_savepoint = false;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      return er_errid ();
    }

  vclass = db_find_class (entity_name);
  if (vclass == NULL)
    {
      return er_errid ();
    }

  db_make_null (&src_val);
  db_make_null (&dest_val);

  ctemplate = dbt_edit_class (vclass);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (eg, because the server unilaterally
         aborts us), we must record the associated error message into the
         parser.  Otherwise, we may get a confusing error msg of the form:
         "so_and_so is not a class". */
      pt_record_error (parser, parser->statement_number - 1,
		       alter->line_number, alter->column_number, er_msg ());
      return er_errid ();
    }

  switch (alter->info.alter.code)
    {
    case PT_ADD_QUERY:
      error = do_add_queries (parser, ctemplate,
			      alter->info.alter.alter_clause.query.query);
      break;

    case PT_DROP_QUERY:
      vlist = alter->info.alter.alter_clause.query.query_no_list;
      if (vlist == NULL)
	{
	  error = dbt_drop_query_spec (ctemplate, 1);
	}
      else if (vlist->next == NULL)
	{			/* ie, only one element in list */
	  error =
	    dbt_drop_query_spec (ctemplate, vlist->info.value.data_value.i);
	  break;
	}
      else
	{
	  slist = pt_sort_in_desc_order (vlist);
	  for (; slist; slist = slist->next)
	    {
	      error =
		dbt_drop_query_spec (ctemplate,
				     slist->info.value.data_value.i);
	      if (error != NO_ERROR)
		{
		  break;
		}
	    }
	}
      break;

    case PT_MODIFY_QUERY:
      if (alter->info.alter.alter_clause.query.query_no_list)
	{
	  query_no =
	    alter->info.alter.alter_clause.query.query_no_list->info.value.
	    data_value.i;
	}
      else
	{
	  query_no = 1;
	}
      new_query = parser_print_tree (parser,
				     alter->info.alter.alter_clause.query.
				     query);
      error = dbt_change_query_spec (ctemplate, new_query, query_no);
      break;


    case PT_ADD_ATTR_MTHD:
      /* we currently core dump when adding a unique
         constraint at the same time as an attribute, whether the unique
         constraint is on the new attribute or another.
         Therefore we temporarily disallow adding a unique constraint
         and an attribute in the same alter statement if the class has
         or has had any instances.
         Note that we should be checking for instances in the entire
         subhierarchy, not just the current class. */
#if 0
      if ((hfid = sm_get_heap (vclass)) && !HFID_IS_NULL (hfid)
	  && alter->info.alter.alter_clause.attr_mthd.attr_def_list)
	{
	  for (p = alter->info.alter.constraint_list; p != NULL; p = p->next)
	    {
	      if (p->info.constraint.type == PT_CONSTRAIN_UNIQUE)
		{
		  error = ER_DO_ALTER_ADD_WITH_UNIQUE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  (void) dbt_abort_class (ctemplate);
		  return error;
		}
	    }
	  PT_END;
	}
#endif
      error = tran_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD, false);
      if (error == NO_ERROR)
	{
	  error = do_add_attributes (parser, ctemplate,
				     alter->info.alter.alter_clause.attr_mthd.
				     attr_def_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  error = do_add_foreign_key_objcache_attr (ctemplate,
						    alter->info.alter.
						    constraint_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  vclass = dbt_finish_class (ctemplate);
	  if (vclass == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  ctemplate = dbt_edit_class (vclass);
	  if (ctemplate == NULL)
	    {
	      error = er_errid ();
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  error = do_add_constraints (ctemplate,
				      alter->info.alter.constraint_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  if (alter->info.alter.alter_clause.attr_mthd.mthd_def_list != NULL)
	    error = do_add_methods (parser,
				    ctemplate,
				    alter->info.alter.alter_clause.attr_mthd.
				    mthd_def_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  if (alter->info.alter.alter_clause.attr_mthd.mthd_file_list != NULL)
	    error = do_add_method_files (parser, ctemplate, alter->info.alter.
					 alter_clause.attr_mthd.
					 mthd_file_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void)
		tran_abort_upto_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }
	}
      break;

    case PT_DROP_ATTR_MTHD:
      p = alter->info.alter.alter_clause.attr_mthd.attr_mthd_name_list;
      for (; p && p->node_type == PT_NAME; p = p->next)
	{
	  attr_mthd_name = p->info.name.original;
	  if (p->info.name.meta_class == PT_META_ATTR)
	    {
	      found_attr = db_get_class_attribute (vclass, attr_mthd_name);
	      if (found_attr)
		{
		  error =
		    dbt_drop_class_attribute (ctemplate, attr_mthd_name);
		}
	      else
		{
		  found_mthd = db_get_class_method (vclass, attr_mthd_name);
		  if (found_mthd)
		    {
		      error =
			dbt_drop_class_method (ctemplate, attr_mthd_name);
		    }
		}
	    }
	  else
	    {
	      found_attr = db_get_attribute (vclass, attr_mthd_name);
	      if (found_attr)
		{
		  error = dbt_drop_attribute (ctemplate, attr_mthd_name);
		}
	      else
		{
		  found_mthd = db_get_method (vclass, attr_mthd_name);
		  if (found_mthd)
		    {
		      error = dbt_drop_method (ctemplate, attr_mthd_name);
		    }
		}
	    }

	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}

      p = alter->info.alter.alter_clause.attr_mthd.mthd_file_list;
      for (; p
	   && p->node_type == PT_FILE_PATH
	   && (path = p->info.file_path.string) != NULL
	   && path->node_type == PT_VALUE
	   && (path->type_enum == PT_TYPE_VARCHAR ||
	       path->type_enum == PT_TYPE_CHAR ||
	       path->type_enum == PT_TYPE_NCHAR ||
	       path->type_enum == PT_TYPE_VARNCHAR); p = p->next)
	{
	  mthd_file = (char *) path->info.value.data_value.str->bytes;
	  error = dbt_drop_method_file (ctemplate, mthd_file);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}

      break;

    case PT_MODIFY_ATTR_MTHD:
      p = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
      for (; p && p->node_type == PT_ATTR_DEF; p = p->next)
	{
	  attr_name = p->info.attr_def.attr_name->info.name.original;
	  if (p->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      class_attr = 1;
	    }
	  else
	    {
	      class_attr = 0;
	    }
	  data_type = p->data_type;

	  domain = pt_node_to_db_domain_name (p);
	  error = dbt_change_domain (ctemplate, attr_name,
				     class_attr, domain);

	  if (data_type && pt_is_set_type (p))
	    {
	      nodelist = data_type->data_type;
	      for (node = nodelist; node != NULL; node = node->next)
		{
		  domain = pt_data_type_to_db_domain_name (node);
		  error = dbt_add_set_attribute_domain (ctemplate,
							attr_name, class_attr,
							domain);
		  if (error != NO_ERROR)
		    {
		      (void) dbt_abort_class (ctemplate);
		      return error;
		    }
		}
	    }

	  data_default = p->info.attr_def.data_default;
	  if (data_default != NULL
	      && data_default->node_type == PT_DATA_DEFAULT)
	    {
	      pt_desired_type = p->type_enum;

	      /* try to coerce the default value into the attribute's type */
	      d = data_default->info.data_default.default_value;
	      d = pt_semantic_check (parser, d);
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  (void) dbt_abort_class (ctemplate);
		  return er_errid ();
		}

	      error =
		pt_coerce_value (parser, d, d, pt_desired_type, p->data_type);
	      if (error != NO_ERROR)
		{
		  break;
		}

	      pt_evaluate_tree (parser, d, &dest_val);
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  (void) dbt_abort_class (ctemplate);
		  return er_errid ();
		}

	      error =
		dbt_change_default (ctemplate, attr_name, class_attr,
				    &dest_val);
	      if (error != NO_ERROR)
		{
		  (void) dbt_abort_class (ctemplate);
		  return error;
		}
	    }
	}

      /* the order in which methods are defined will change;
         currently there's no way around this problem. */
      p = alter->info.alter.alter_clause.attr_mthd.mthd_def_list;
      for (; p && p->node_type == PT_METHOD_DEF; p = p->next)
	{
	  mthd_name = p->info.method_def.method_name->info.name.original;
	  error = dbt_drop_method (ctemplate, mthd_name);
	  if (error == NO_ERROR)
	    {
	      error = do_add_methods (parser,
				      ctemplate,
				      alter->info.alter.alter_clause.
				      attr_mthd.mthd_def_list);
	    }
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_ADD_SUPCLASS:
      error = do_add_supers (parser,
			     ctemplate,
			     alter->info.alter.super.sup_class_list);
      /*
         if (alter->info.alter.super.resolution_list != NULL)
         {
         error = do_add_resolutions(ctemplate,
         alter->info.alter.super.resolution_list);
         if (error != NO_ERROR)
         {
         (void) dbt_abort_class(ctemplate);
         return error;
         }
         }
       */
      break;

      /* This is a change that might be made later to the BNF, in which
         this piece of code will be required.

         case PT_ADD_RESOLUTION :
         error = do_add_resolutions(parser, ctemplate,
         alter->info.alter.super.resolution_list);
         if (error != NO_ERROR)
         {
         (void) dbt_abort_class(ctemplate);
         return error;
         }
         break;
       */

    case PT_DROP_SUPCLASS:
      for (p = alter->info.alter.super.sup_class_list;
	   p && p->node_type == PT_NAME; p = p->next)
	{
	  sup_class = db_find_class (p->info.name.original);
	  if (sup_class == NULL)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      error = dbt_drop_super (ctemplate, sup_class);
	    }
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_DROP_RESOLUTION:
      for (p = alter->info.alter.super.resolution_list;
	   p && p->node_type == PT_RESOLUTION; p = p->next)
	{
	  sup_class =
	    db_find_class (p->info.resolution.of_sup_class_name->info.name.
			   original);
	  attr_mthd_name =
	    p->info.resolution.attr_mthd_name->info.name.original;
	  error = dbt_drop_resolution (ctemplate, sup_class, attr_mthd_name);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_MODIFY_DEFAULT:
      n = alter->info.alter.alter_clause.ch_attr_def.attr_name_list;
      d = alter->info.alter.alter_clause.ch_attr_def.data_default_list;
      for (; n && d; n = n->next, d = d->next)
	{
	  /* try to coerce the default value into the attribute's type */
	  if (!(d = pt_semantic_check (parser, d)))
	    {
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  error = er_errid ();
		}
	      else
		{
		  error = ER_GENERIC_ERROR;
		}
	      break;
	    }

	  pt_evaluate_tree (parser, d, &src_val);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      error = er_errid ();
	      break;
	    }

	  attr_name = n->info.name.original;
	  if (n->info.name.meta_class == PT_META_ATTR)
	    {
	      def_attr = db_get_class_attribute (vclass, attr_name);
	    }
	  else
	    {
	      def_attr = db_get_attribute (vclass, attr_name);
	    }
	  if (!def_attr || !(def_domain = db_attribute_domain (def_attr)))
	    {
	      error = er_errid ();
	      break;
	    }
	  db_desired_type = db_domain_type (def_domain);

	  error = tp_value_coerce (&src_val, &dest_val, def_domain);
	  if (error != NO_ERROR)
	    {
	      DB_OBJECT *desired_class;
	      const char *desired_type;

	      if ((db_desired_type == DB_TYPE_OBJECT)
		  && (desired_class = db_domain_class (def_domain)))
		{
		  desired_type = db_get_class_name (desired_class);
		}
	      else
		{
		  desired_type = db_get_type_name (db_desired_type);
		}

	      if (error == DOMAIN_OVERFLOW)
		PT_ERRORmf2 (parser, d, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OVERFLOW_COERCING_TO,
			     pt_short_print (parser, d), desired_type);
	      else
		PT_ERRORmf2 (parser, d, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_CANT_COERCE_TO,
			     pt_short_print (parser, d), desired_type);
	      break;
	    }

	  if (n->info.name.meta_class == PT_META_ATTR)
	    {
	      error = dbt_change_default (ctemplate, attr_name, 1, &dest_val);
	    }
	  else
	    {
	      error = dbt_change_default (ctemplate, attr_name, 0, &dest_val);
	    }
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  pr_clear_value (&src_val);
	  pr_clear_value (&dest_val);
	}
      break;

      /* If merely renaming resolution,  will be done after switch statement */
    case PT_RENAME_RESOLUTION:
      break;

    case PT_RENAME_ATTR_MTHD:
      if (alter->info.alter.alter_clause.rename.old_name)
	old_name =
	  alter->info.alter.alter_clause.rename.old_name->info.name.original;
      else
	old_name = NULL;
      new_name =
	alter->info.alter.alter_clause.rename.new_name->info.name.original;
      if (alter->info.alter.alter_clause.rename.meta == PT_META_ATTR)
	{
	  class_attr = 1;
	}
      else
	{
	  class_attr = 0;
	}
      switch (alter->info.alter.alter_clause.rename.element_type)
	{
	case PT_ATTRIBUTE:
	case PT_METHOD:
	  error = dbt_rename (ctemplate, old_name, class_attr, new_name);
	  break;

	case PT_FUNCTION_RENAME:
	  mthd_name =
	    alter->info.alter.alter_clause.rename.mthd_name->info.name.
	    original;
	  error =
	    dbt_change_method_implementation (ctemplate, mthd_name,
					      class_attr, new_name);
	  break;

	  /* the following case is not yet supported,
	     but hey, when it is, there'll code for it :-) */

	  /* There's code now. this drops the old file name and
	     puts the new file name in it's place., I took out the
	     class_attr, since for our purpose we don't need it */

	case PT_FILE_RENAME:
	  old_name =
	    (char *) alter->info.alter.alter_clause.rename.old_name->info.
	    file_path.string->info.value.data_value.str->bytes;
	  new_name =
	    (char *) alter->info.alter.alter_clause.rename.new_name->info.
	    file_path.string->info.value.data_value.str->bytes;
	  error = dbt_rename_method_file (ctemplate, old_name, new_name);
	  break;

	default:
	  /* actually, it means that a wrong thing is being
	     renamed, and is really an error condition. */
	  break;
	}
      break;

    case PT_DROP_CONSTRAINT:
      {
	SM_CLASS_CONSTRAINT *cons;

	cons = classobj_find_class_index (ctemplate->current,
					  alter->info.alter.constraint_list->
					  info.name.original);
	if (cons)
	  {
	    if (cons->type == SM_CONSTRAINT_PRIMARY_KEY)
	      {
		error = dbt_drop_constraint (ctemplate,
					     DB_CONSTRAINT_PRIMARY_KEY,
					     alter->info.alter.
					     constraint_list->info.name.
					     original, NULL, 0);
	      }
	    else if (cons->type == SM_CONSTRAINT_FOREIGN_KEY)
	      {
		error = dbt_drop_constraint (ctemplate,
					     DB_CONSTRAINT_FOREIGN_KEY,
					     alter->info.alter.
					     constraint_list->info.name.
					     original, NULL, 0);
	      }
	    else
	      {
		error = dbt_drop_constraint (ctemplate,
					     DB_CONSTRAINT_UNIQUE,
					     alter->info.alter.
					     constraint_list->info.name.
					     original, NULL, 0);
	      }
	  }
	else
	  {
	    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		    ER_SM_CONSTRAINT_NOT_FOUND, 1,
		    alter->info.alter.constraint_list->info.name.original);
	    error = er_errid ();
	  }
      }
      break;

    case PT_APPLY_PARTITION:
    case PT_REMOVE_PARTITION:
    case PT_ADD_PARTITION:
    case PT_ADD_HASHPARTITION:
    case PT_COALESCE_PARTITION:
    case PT_REORG_PARTITION:
    case PT_ANALYZE_PARTITION:
      error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_ALTER, false);
      if (error != NO_ERROR)
	{
	  break;
	}
      partition_savepoint = true;
      switch (alter->info.alter.code)
	{
	case PT_APPLY_PARTITION:
	case PT_ADD_PARTITION:
	case PT_ADD_HASHPARTITION:
	  error = do_create_partition (parser, alter, vclass, ctemplate);
	  break;
	case PT_REORG_PARTITION:
	  error = do_create_partition (parser, alter, vclass, ctemplate);
	  if (error == NO_ERROR)
	    {
	      coalesce_num = 0;
	      names = alter->info.alter.alter_clause.partition.name_list;
	      for (; names; names = names->next)
		{
		  if (names->partition_pruned)
		    {
		      coalesce_num++;
		    }
		}
	      sprintf (partnum_str, "$%d", coalesce_num);
	      error =
		do_remove_partition_pre (ctemplate, keycol, partnum_str);
	    }
	  break;
	case PT_REMOVE_PARTITION:
	  error = do_remove_partition_pre (ctemplate, keycol, "*");
	  break;
	case PT_COALESCE_PARTITION:
	  error = do_get_partition_keycol (keycol, vclass);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  error = do_get_partition_size (vclass);
	  if (error < 0)
	    {
	      break;
	    }
	  partnum = error;
	  coalesce_num =
	    partnum -
	    alter->info.alter.alter_clause.partition.size->info.value.
	    data_value.i;
	  sprintf (partnum_str, "#%d", coalesce_num);
	  error = do_remove_partition_pre (ctemplate, keycol, partnum_str);
	  break;
	case PT_ANALYZE_PARTITION:
	  names = alter->info.alter.alter_clause.partition.name_list;
	  if (names == NULL)
	    {			/* ALL */
	      error =
		au_fetch_class (vclass, &class_, AU_FETCH_READ, AU_SELECT);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      error = sm_update_statistics (vclass);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      for (objs = class_->users; objs; objs = objs->next)
		{
		  error = au_fetch_class (objs->op, &subcls,
					  AU_FETCH_READ, AU_SELECT);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  if (!subcls->partition_of)
		    {
		      continue;	/* not partitioned */
		    }
		  error = sm_update_statistics (objs->op);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		}
	    }
	  else
	    {
	      for (; names; names = names->next)
		{
		  if (!names->info.name.db_object)
		    {
		      break;
		    }
		  error = sm_update_statistics (names->info.name.db_object);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		}
	    }
	  break;
	default:
	  break;
	}
      break;
    case PT_DROP_PARTITION:	/* post work */
      break;
    default:
      (void) dbt_abort_class (ctemplate);
      return error;
    }

  /* Process resolution list if appropriate */
  if (error == NO_ERROR)
    {
      if (alter->info.alter.code != PT_DROP_RESOLUTION)
	{
	  if (alter->info.alter.super.resolution_list != NULL)
	    {
	      error = do_add_resolutions (parser, ctemplate,
					  alter->info.alter.super.
					  resolution_list);
	    }
	}
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_class (ctemplate);
      if (partition_savepoint)
	{
	  goto alter_partition_fail;
	}
      return error;
    }

  vclass = dbt_finish_class (ctemplate);

  /* the dbt_finish_class() failed, the template was not freed */
  if (vclass == NULL)
    {
      error = er_errid ();
      (void) dbt_abort_class (ctemplate);
      if (partition_savepoint)
	{
	  goto alter_partition_fail;
	}
      return error;
    }
  else
    {
      switch (alter->info.alter.code)
	{
	case PT_APPLY_PARTITION:
	case PT_ADD_HASHPARTITION:
	case PT_ADD_PARTITION:
	case PT_REORG_PARTITION:
	  if (alter->info.alter.code == PT_APPLY_PARTITION)
	    {
	      error = do_update_partition_newly (entity_name,
						 alter->info.alter.
						 alter_clause.partition.info->
						 info.partition.keycol->info.
						 name.original);
	    }
	  else if (alter->info.alter.code == PT_ADD_HASHPARTITION
		   || alter->info.alter.code == PT_REORG_PARTITION)
	    {
	      error = do_get_partition_keycol (keycol, vclass);
	      if (error == NO_ERROR)
		{
		  error = do_update_partition_newly (entity_name, keycol);
		}
	    }

	  if (error == NO_ERROR)
	    {
	      /* index propagate */
	      classop = db_find_class (entity_name);
	      if (classop == NULL)
		{
		  error = er_errid ();
		  goto fail_end;
		}
	      if (au_fetch_class (classop, &class_, AU_FETCH_READ,
				  AU_SELECT) != NO_ERROR)
		{
		  error = er_errid ();
		  goto fail_end;
		}

	      smclass = sm_get_class_with_statistics (classop);
	      if (smclass->stats == NULL)
		{
		  if (error == NO_ERROR && (error = er_errid ()) == NO_ERROR)
		    {
		      error = ER_PARTITION_WORK_FAILED;
		    }
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_PARTITION_WORK_FAILED, 0);
		  goto fail_end;
		}

	      for (cons = class_->constraints; cons; cons = cons->next)
		{
		  if (cons->type != DB_CONSTRAINT_INDEX
		      && cons->type != DB_CONSTRAINT_REVERSE_INDEX)
		    {
		      continue;
		    }
		  attp = cons->attributes;
		  i = 0;
		  while (*attp)
		    {
		      attp++;
		      i++;
		    }

		  if (i <= 0
		      || (namep =
			  (char **) malloc (sizeof (char *) * (i + 1)))
		      == NULL
		      || (asc_desc =
			  (int *) malloc (sizeof (int) * (i))) == NULL)
		    {
		      if (error == NO_ERROR
			  && (error = er_errid ()) == NO_ERROR)
			{
			  error = ER_PARTITION_WORK_FAILED;
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PARTITION_WORK_FAILED, 0);
		      goto fail_end;
		    }
		  attp = cons->attributes;
		  attrnames = namep;

		  /* need to get asc/desc info */
		  key_type = classobj_find_cons_index2_col_type_list (cons,
								      smclass->
								      stats);
		  if (key_type == NULL)
		    {
		      if (error == NO_ERROR
			  && (error = er_errid ()) == NO_ERROR)
			{
			  error = ER_PARTITION_WORK_FAILED;
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PARTITION_WORK_FAILED, 0);
		      goto fail_end;
		    }

		  i = 0;
		  while (*attp && key_type)
		    {
		      *attrnames = (char *) (*attp)->header.name;
		      attrnames++;
		      asc_desc[i] = 0;	/* guess as Asc */
		      if (DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (cons->type)
			  || key_type->is_desc)
			{
			  asc_desc[i] = 1;	/* Desc */
			}
		      i++;

		      attp++;
		      key_type = key_type->next;
		    }

		  if (*attp || key_type)
		    {
		      if (error == NO_ERROR
			  && (error = er_errid ()) == NO_ERROR)
			{
			  error = ER_PARTITION_WORK_FAILED;
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PARTITION_WORK_FAILED, 0);
		      goto fail_end;
		    }

		  *attrnames = NULL;

		  for (objs = class_->users; objs; objs = objs->next)
		    {
		      error = au_fetch_class (objs->op, &subcls,
					      AU_FETCH_READ, AU_SELECT);
		      if (error != NO_ERROR)
			{
			  error = er_errid ();
			  goto fail_end;
			}
		      if (!subcls->partition_of)
			{
			  continue;	/* not partitioned */
			}
		      if (alter->info.alter.code == PT_ADD_PARTITION
			  || alter->info.alter.code == PT_REORG_PARTITION
			  || alter->info.alter.code == PT_ADD_HASHPARTITION)
			{
			  parts =
			    alter->info.alter.alter_clause.partition.parts;
			  for (; parts; parts = parts->next)
			    {
			      if (alter->info.alter.code == PT_REORG_PARTITION
				  && parts->partition_pruned)
				{
				  continue;	/* reused partition */
				}
			      if (ws_mop_compare (objs->op,
						  parts->info.parts.name->
						  info.name.db_object) == 0)
				{
				  break;
				}
			    }
			  if (parts == NULL)
			    {
			      continue;
			    }
			}
		      error =
			sm_add_index (objs->op, (const char **) namep,
				      asc_desc, cons->name,
				      DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY
				      (cons->type));
		      if (error != NO_ERROR)
			{
			  break;
			}
		    }

		  free_and_init (namep);
		  namep = NULL;
		  free_and_init (asc_desc);
		  asc_desc = NULL;
		}

	    fail_end:

	      if (namep != NULL)
		{
		  free_and_init (namep);
		}
	      if (asc_desc != NULL)
		{
		  free_and_init (asc_desc);
		}
	    }

	  if (error != NO_ERROR)
	    {
	      goto alter_partition_fail;
	    }

	  if (alter->info.alter.code == PT_REORG_PARTITION)
	    {
	      delnames = NULL;
	      names = alter->info.alter.alter_clause.partition.name_list;
	      for (; names; names = parts)
		{
		  parts = names->next;
		  if (names->partition_pruned)
		    {		/* for delete partition */
		      if (delnames != NULL)
			{
			  delnames->next = names;
			}
		      else
			{
			  alter->info.alter.alter_clause.partition.name_list =
			    names;
			}
		      delnames = names;
		      delnames->next = NULL;
		    }
		  else
		    {
		      names->next = NULL;
		      parser_free_tree (parser, names);
		    }
		}
	      if (delnames != NULL)
		{
		  error = do_drop_partition_list (vclass, alter->
						  info.alter.alter_clause.
						  partition.name_list);
		  if (error != NO_ERROR)
		    {
		      goto alter_partition_fail;
		    }
		}
	    }
	  break;

	case PT_COALESCE_PARTITION:
	  error = do_update_partition_newly (entity_name, keycol);
	  if (error != NO_ERROR)
	    {
	      goto alter_partition_fail;
	    }
	  slist = NULL;
	  coalesce_list = NULL;
	  for (; coalesce_num < partnum; coalesce_num++)
	    {
	      sprintf (partnum_str, "p%d", coalesce_num);
	      parts = pt_name (parser, partnum_str);
	      if (parts == NULL)
		{
		  goto alter_partition_fail;
		}
	      parts->next = NULL;
	      if (coalesce_list == NULL)
		{
		  coalesce_list = parts;
		}
	      else
		{
		  slist->next = parts;
		}
	      slist = parts;
	    }
	  error = do_drop_partition_list (vclass, coalesce_list);
	  if (error != NO_ERROR)
	    {
	      parser_free_tree (parser, coalesce_list);
	      goto alter_partition_fail;
	    }
	  parser_free_tree (parser, coalesce_list);
	  break;

	case PT_REMOVE_PARTITION:
	  error = do_remove_partition_post (parser, entity_name, keycol);
	  if (error != NO_ERROR)
	    {
	      goto alter_partition_fail;
	    }
	  break;

	case PT_DROP_PARTITION:
	  error = do_drop_partition_list (vclass, alter->
					  info.alter.alter_clause.partition.
					  name_list);
	  if (error != NO_ERROR)
	    {
	      goto alter_partition_fail;
	    }
	  break;

	default:
	  break;
	}
    }

  return NO_ERROR;

alter_partition_fail:
  if (partition_savepoint && error != NO_ERROR
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_ALTER);
    }
  return error;
}
