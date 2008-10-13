/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_class.c - Code for Creating a Class from a Parse Tree
 */

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#include <ctype.h>

#include "error_manager.h"
#include "parser.h"
#include "dbi.h"
#include "msgexec.h"
#include "semantic_check.h"
#include "memory_manager_2.h"
#include "system_parameter.h"
#include "schema_manager_3.h"
#include "execute_schema_2.h"
#include "parser.h"
#include "locator_cl.h"
#include "transaction_cl.h"
#include "execute_schema_8.h"
#include "execute_statement_10.h"
#include "db.h"

static const int DO_DB_MAX_DOUBLE_PRECISION = 53;
static const int DO_DB_MAX_FLOAT_PRECISION = 24;
static const int DO_DB_MAX_INTEGER_PRECISION = 10;

static int valiate_attribute_domain (PARSER_CONTEXT * parser,
				     PT_NODE * attribute,
				     const bool check_zero_precision);
static SM_FOREIGN_KEY_ACTION map_pt_to_sm_action (const PT_MISC_TYPE action);
static int add_foreign_key (DB_CTMPL * ctemplate, const PT_NODE * cnstr,
			    const char **att_names);

static int add_union_query (PARSER_CONTEXT * parser,
			    DB_CTMPL * ctemplate, const PT_NODE * query);
static int add_query_to_virtual_class (PARSER_CONTEXT * parser,
				       DB_CTMPL * ctemplate,
				       const PT_NODE * queries);

/*
 * valiate_attribute_domain() - This checks an attribute to make sure
 *                                  that it makes sense
 *   return: Error code
 *   parser(in): Parser context
 *   attribute(in): Parse tree of an attribute or method
 *   check_zero_precision(in): Do not permit zero precision if true
 *
 * Note: Error reporting system
 */
static int
valiate_attribute_domain (PARSER_CONTEXT * parser,
			  PT_NODE * attribute,
			  const bool check_zero_precision)
{
  int error = NO_ERROR;

  if (attribute == NULL)
    {
      pt_record_error (parser,
		       parser->statement_number,
		       __LINE__, 0, "system error - NULL attribute node");
    }
  else
    {
      if (attribute->type_enum == PT_TYPE_NONE)
	{
	  pt_record_error (parser,
			   parser->statement_number,
			   attribute->line_number,
			   attribute->column_number,
			   "system error - attribute type not set");
	}
      else
	{
	  PT_NODE *dtyp = attribute->data_type;

	  if (dtyp)
	    {
	      int p = attribute->data_type->info.data_type.precision;
	      int s = attribute->data_type->info.data_type.dec_precision;

	      switch (attribute->type_enum)
		{
		case PT_TYPE_FLOAT:
		  if (p < 0)
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DO_DB_MAX_DOUBLE_PRECISION);
		    }
		  else if (p <= DO_DB_MAX_FLOAT_PRECISION)
		    attribute->type_enum = PT_TYPE_FLOAT;
		  else if (p <= DO_DB_MAX_DOUBLE_PRECISION)
		    attribute->type_enum = PT_TYPE_DOUBLE;
		  else
		    {
		      PT_ERRORmf2 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_PREC_TOO_BIG, p,
				   DO_DB_MAX_DOUBLE_PRECISION);
		    }
		  break;

		case PT_TYPE_DOUBLE:
		  if (p < 0 || s < 0 || p < s)
		    {
		      PT_ERRORmf2 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC_SCALE, p, s);
		    }
		  else if (s == 0)
		    {
		      if (p <= DO_DB_MAX_INTEGER_PRECISION)
			attribute->type_enum = PT_TYPE_INTEGER;
		      else if (p <= DB_MAX_NUMERIC_PRECISION)
			attribute->type_enum = PT_TYPE_NUMERIC;
		      else if (p <= DO_DB_MAX_DOUBLE_PRECISION)
			attribute->type_enum = PT_TYPE_DOUBLE;
		      else
			{
			  PT_ERRORmf2 (parser, attribute,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_PREC_TOO_BIG, p,
				       DO_DB_MAX_DOUBLE_PRECISION);
			}
		    }
		  else
		    {
		      if (p <= DO_DB_MAX_FLOAT_PRECISION)
			attribute->type_enum = PT_TYPE_FLOAT;
		      else if (p <= DO_DB_MAX_DOUBLE_PRECISION)
			attribute->type_enum = PT_TYPE_DOUBLE;
		      else
			{
			  PT_ERRORmf2 (parser, attribute,
				       MSGCAT_SET_PARSER_SEMANTIC,
				       MSGCAT_SEMANTIC_PREC_TOO_BIG, p,
				       DO_DB_MAX_DOUBLE_PRECISION);
			}
		    }
		  break;

		case PT_TYPE_NUMERIC:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_NUMERIC_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_NUMERIC_PRECISION);
		    }
		  break;

		case PT_TYPE_BIT:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_BIT_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_BIT_PRECISION);
		    }
		  break;

		case PT_TYPE_VARBIT:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_VARBIT_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARBIT_PRECISION);
		    }
		  break;

		case PT_TYPE_CHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_CHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_CHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_NCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_NCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_NCHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_VARCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_VARCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARCHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_VARNCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision)
			  || p > DB_MAX_VARNCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute,
				   MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARNCHAR_PRECISION);
		    }
		  break;

		default:
		  break;
		}		/* switch (attribute->type_enum) */
	    }			/* if (dtyp) */
	}
    }

  if (error == NO_ERROR)
    {
      if (pt_has_error (parser))
	{
	  error = ER_PT_SEMANTIC;
	}
    }

  return error;

}				/* valiate_attribute_domain */

/*
 * do_add_attributes() - Adds attributes to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   atts(in/out): Attribute to add
 *
 * Note : Class object is modified
 */
int
do_add_attributes (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
		   PT_NODE * atts)
{
  const char *attr_name;
  int meta, shared;
  DB_VALUE *default_value = NULL;
  DB_VALUE stack_value;
  int error = NO_ERROR;
  PT_TYPE_ENUM desired_type;
  PT_NODE *def_val;
  DB_DOMAIN *attr_db_domain;
  MOP auto_increment_obj = NULL;
  SM_ATTRIBUTE *att;

  /* add each attribute listed in the virtual class definition  */

  while (atts && (error == NO_ERROR))
    {
      /* For each attribute:
         1. Extract the attribute name from the parse structure.
         2. Make sure attribute domain is ok; some things that look
         ok to the parser (e.g., DECIMAL(2,27)) can make it here.
         3. Check is a default value has been declared.
         If so, then extract the default value.
         4. If the domain of the attribute is a homogeneous set,
         first add the attribute, then add its domain.
         Else If the domain of the attribute is a heterogeneous
         set, first add the attribute, then add the domain
         for each specified type in the heterogeneous set.
         Else If the domain is a primitive type, then simply add
         the attribute.
       */

      /* get derive name, and then original name.
         for example: create view a_view
         as select a av1, a av2, b bv from a_tbl;
       */
      attr_name = atts->info.attr_def.attr_name->alias_print
	? atts->info.attr_def.attr_name->alias_print
	: atts->info.attr_def.attr_name->info.name.original;

      meta = (atts->info.attr_def.attr_type == PT_META_ATTR);
      shared = (atts->info.attr_def.attr_type == PT_SHARED);

      if (valiate_attribute_domain (parser, atts,
				    smt_get_class_type (ctemplate) ==
				    SM_CLASS_CT ? true : false))
	{
	  /* validate_attribute_domain() is assumed to issue whatever
	     messages are pertinent. */
	  return ER_GENERIC_ERROR;
	}

      if (atts->info.attr_def.data_default == NULL)
	{
	  default_value = NULL;
	}
      else
	{
	  desired_type = atts->type_enum;

	  /* try to coerce the default value into the attribute's type */
	  def_val =
	    atts->info.attr_def.data_default->info.data_default.default_value;
	  def_val = pt_semantic_check (parser, def_val);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      return er_errid ();
	    }

	  error =
	    pt_coerce_value (parser, def_val, def_val, desired_type,
			     atts->data_type);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  default_value = &stack_value;
	  pt_evaluate_tree (parser, def_val, default_value);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      return er_errid ();
	    }
	}

      attr_db_domain = pt_node_to_db_domain (parser, atts, ctemplate->name);
      if (attr_db_domain == NULL)
	{
	  return (er_errid ());
	}

      if (meta)
	{
	  error = smt_add_attribute_w_dflt (ctemplate, attr_name, NULL,
					    attr_db_domain, default_value,
					    ID_CLASS_ATTRIBUTE);
	}
      else if (shared)
	{
	  error = smt_add_attribute_w_dflt (ctemplate, attr_name, NULL,
					    attr_db_domain, default_value,
					    ID_SHARED_ATTRIBUTE);
	}
      else
	{
	  error = smt_add_attribute_w_dflt (ctemplate, attr_name, NULL,
					    attr_db_domain, default_value,
					    ID_ATTRIBUTE);
	}

      if (default_value)
	{
	  db_value_clear (default_value);
	}

      /*  Does the attribute belong to a NON_NULL constraint? */
      if (error == NO_ERROR)
	{
	  if (atts->info.attr_def.constrain_not_null)
	    {
	      error = dbt_constrain_non_null (ctemplate, attr_name, meta, 1);
	    }
	}

      /* create & set auto_increment attribute's serial object */
      if (error == NO_ERROR && !meta && !shared)
	{
	  if (atts->info.attr_def.auto_increment)
	    {
	      if (!db_Replication_agent_mode)
		{
		  error = do_create_auto_increment_serial (parser,
							   &auto_increment_obj,
							   ctemplate->name,
							   atts);
		}
	      if (error == NO_ERROR)
		{
		  if (smt_find_attribute (ctemplate, attr_name, 0, &att)
		      == NO_ERROR)
		    {
		      att->auto_increment = auto_increment_obj;
		      att->flags |= SM_ATTFLAG_AUTO_INCREMENT;
		    }
		}
	    }
	}

      atts = atts->next;
    }

  return (error);
}

/*
 * map_pt_to_sm_action() -
 *   return: SM_FOREIGN_KEY_ACTION
 *   action(in): Action to map
 *
 * Note:
 */
static SM_FOREIGN_KEY_ACTION
map_pt_to_sm_action (const PT_MISC_TYPE action)
{
  switch (action)
    {
    case PT_RULE_CASCADE:
      return SM_FOREIGN_KEY_CASCADE;

    case PT_RULE_RESTRICT:
      return SM_FOREIGN_KEY_RESTRICT;

    case PT_RULE_NO_ACTION:
      return SM_FOREIGN_KEY_NO_ACTION;

    default:
      break;
    }

  return SM_FOREIGN_KEY_NO_ACTION;
}

/*
 * add_foreign_key() -
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   cnstr(in): Constraint name
 *   att_names(in): Key attribute names
 *
 * Note:
 */
static int
add_foreign_key (DB_CTMPL * ctemplate, const PT_NODE * cnstr,
		 const char **att_names)
{
  PT_FOREIGN_KEY_INFO *fk_info;
  const char *constraint_name = NULL;
  const char **ref_attrs = NULL;
  int i, n_atts, n_ref_atts;
  PT_NODE *p;
  int error = NO_ERROR;
  const char *cache_attr = NULL;

  fk_info = (PT_FOREIGN_KEY_INFO *) & (cnstr->info.constraint.un.foreign_key);

  n_atts = pt_length_of_list (fk_info->attrs);
  i = 0;
  for (p = fk_info->attrs; p; p = p->next)
    {
      att_names[i++] = p->info.name.original;
    }
  att_names[i] = NULL;

  if (fk_info->referenced_attrs != NULL)
    {
      n_ref_atts = pt_length_of_list (fk_info->referenced_attrs);

      ref_attrs = (const char **) malloc ((n_ref_atts + 1) * sizeof (char *));
      if (ref_attrs == NULL)
	{
	  return er_errid ();
	}

      i = 0;
      for (p = fk_info->referenced_attrs; p; p = p->next)
	{
	  ref_attrs[i++] = p->info.name.original;
	}
      ref_attrs[i] = NULL;
    }

  /* Get the constraint name (if supplied) */
  if (cnstr->info.constraint.name)
    {
      constraint_name = cnstr->info.constraint.name->info.name.original;
    }

  if (fk_info->cache_attr)
    {
      cache_attr = fk_info->cache_attr->info.name.original;
    }

  error = dbt_add_foreign_key (ctemplate, constraint_name, att_names,
			       fk_info->referenced_class->info.name.original,
			       ref_attrs,
			       map_pt_to_sm_action (fk_info->delete_action),
			       map_pt_to_sm_action (fk_info->update_action),
			       cache_attr);
  free_and_init (ref_attrs);
  return error;
}

/*
 * add_foreign_key_objcache_attr() -
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   constraint(in): Constraints in the class
 *
 * Note:
 */
int
do_add_foreign_key_objcache_attr (DB_CTMPL * ctemplate, PT_NODE * constraints)
{
  PT_NODE *cnstr;
  PT_FOREIGN_KEY_INFO *fk_info;
  SM_ATTRIBUTE *cache_attr;
  int error;
  MOP ref_clsop;
  char *ref_cls_name;

  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      if (cnstr->info.constraint.type != PT_CONSTRAIN_FOREIGN_KEY)
	{
	  continue;
	}

      fk_info = &(cnstr->info.constraint.un.foreign_key);
      ref_cls_name = (char *) fk_info->referenced_class->info.name.original;

      if (fk_info->cache_attr)
	{
	  error = smt_find_attribute (ctemplate,
				      fk_info->cache_attr->info.name.original,
				      false, &cache_attr);

	  if (error == NO_ERROR)
	    {
	      ref_clsop = sm_find_class (ref_cls_name);

	      if (ref_clsop == NULL)
		{
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_FK_UNKNOWN_REF_CLASSNAME, 1, ref_cls_name);
		  return er_errid ();
		}

	      if (cache_attr->type->id != DB_TYPE_OBJECT
		  || !OID_EQ (&cache_attr->domain->class_oid,
			      ws_oid (ref_clsop)))
		{
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_SM_INVALID_NAME, 1, ref_cls_name);
		}
	    }
	  else if (error == ER_SM_INVALID_NAME)
	    {
	      return error;
	    }
	  else
	    {
	      er_clear ();

	      if (smt_add_attribute
		  (ctemplate, fk_info->cache_attr->info.name.original,
		   ref_cls_name, NULL) != NO_ERROR)
		{
		  return er_errid ();
		}
	    }
	}
    }

  return NO_ERROR;
}

/*
 * do_add_constraints() - This extern routine adds constraints
 *			  to a class object.
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   constraints(in): Constraints to add
 *
 * Note : Class object is modified
 */
int
do_add_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints)
{
  int error = NO_ERROR;
  PT_NODE *cnstr;
  int max_attrs = 0;
  const char **att_names = NULL;

  /*  Find the size of the largest UNIQUE constraint list and allocate
     a character array large enough to contain it. */
  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      if (cnstr->info.constraint.type == PT_CONSTRAIN_UNIQUE)
	{
	  max_attrs = MAX (max_attrs,
			   pt_length_of_list (cnstr->info.constraint.un.
					      unique.attrs));
	}
      if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
	{
	  max_attrs = MAX (max_attrs,
			   pt_length_of_list (cnstr->info.constraint.un.
					      primary_key.attrs));
	}
      if (cnstr->info.constraint.type == PT_CONSTRAIN_FOREIGN_KEY)
	{
	  max_attrs = MAX (max_attrs,
			   pt_length_of_list (cnstr->info.constraint.un.
					      foreign_key.attrs));
	}
    }

  if (max_attrs > 0)
    {
      att_names = (const char **) malloc ((max_attrs + 1) * sizeof (char *));

      if (att_names == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
	    {
	      if (cnstr->info.constraint.type == PT_CONSTRAIN_UNIQUE)
		{
		  PT_NODE *p;
		  int i, n_atts;
		  int class_attributes = 0;
		  const char *constraint_name = NULL;

		  n_atts =
		    pt_length_of_list (cnstr->info.constraint.un.unique.
				       attrs);
		  i = 0;
		  for (p = cnstr->info.constraint.un.unique.attrs; p;
		       p = p->next)
		    {
		      att_names[i++] = p->info.name.original;

		      /*  Determine if the unique constraint is being applied to
		         class or normal attributes.  The way the parser currently
		         works, all multi-column constraints will be on normal
		         attributes and it's therefore impossible for a constraint
		         to contain both class and normal attributes. */
		      if (p->info.name.meta_class == PT_META_ATTR)
			{
			  class_attributes = 1;
			}
		    }
		  att_names[i] = NULL;

		  /* Get the constraint name (if supplied) */
		  if (cnstr->info.constraint.name)
		    {
		      constraint_name =
			cnstr->info.constraint.name->info.name.original;
		    }

		  error = dbt_add_constraint (ctemplate, DB_CONSTRAINT_UNIQUE,
					      constraint_name, att_names,
					      class_attributes);
		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}

	      if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
		{
		  PT_NODE *p;
		  int i, n_atts;
		  int class_attributes = 0;
		  const char *constraint_name = NULL;

		  n_atts =
		    pt_length_of_list (cnstr->info.constraint.un.primary_key.
				       attrs);
		  i = 0;
		  for (p = cnstr->info.constraint.un.primary_key.attrs; p;
		       p = p->next)
		    {
		      att_names[i++] = p->info.name.original;

		      /*  Determine if the unique constraint is being applied to
		         class or normal attributes.  The way the parser currently
		         works, all multi-column constraints will be on normal
		         attributes and it's therefore impossible for a constraint
		         to contain both class and normal attributes. */
		      if (p->info.name.meta_class == PT_META_ATTR)
			{
			  class_attributes = 1;
			}
		    }
		  att_names[i] = NULL;

		  /* Get the constraint name (if supplied) */
		  if (cnstr->info.constraint.name)
		    {
		      constraint_name =
			cnstr->info.constraint.name->info.name.original;
		    }

		  error =
		    dbt_add_constraint (ctemplate, DB_CONSTRAINT_PRIMARY_KEY,
					constraint_name, att_names,
					class_attributes);
		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}

	      if (cnstr->info.constraint.type == PT_CONSTRAIN_FOREIGN_KEY)
		{
		  error = add_foreign_key (ctemplate, cnstr, att_names);
		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}
	    }

	  free_and_init (att_names);
	}
    }

  return (error);

/* error handler */
constraint_error:
  if (att_names)
    {
      free_and_init (att_names);
    }
  return (error);
}

/*
 * do_add_methods() - Adds methods to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   methods(in): Methods to add
 *
 * Note : Class object is modified
 */
int
do_add_methods (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
		PT_NODE * methods)
{
  const char *method_name, *method_impl;
  PT_NODE *args_list, *type, *type_list;
  PT_NODE *data_type;
  int arg_num;
  int is_meta;
  int error = NO_ERROR;
  DB_DOMAIN *arg_db_domain;

  /* add each method listed in the class definition */

  while (methods && (error == NO_ERROR))
    {
      method_name = methods->info.method_def.method_name->info.name.original;

      if (methods->info.method_def.function_name != NULL)
	{
	  method_impl =
	    methods->info.method_def.function_name->info.name.original;
	}
      else
	{
	  method_impl = NULL;
	}

      if (methods->info.method_def.mthd_type == PT_META_ATTR)
	{
	  error = dbt_add_class_method (ctemplate, method_name, method_impl);
	}
      else
	{
	  error = dbt_add_method (ctemplate, method_name, method_impl);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* if the result of the method is declared, then add it! */

      arg_num = 0;
      is_meta = methods->info.method_def.mthd_type == PT_META_ATTR;

      if (methods->type_enum != PT_TYPE_NONE)
	{
	  if (PT_IS_COLLECTION_TYPE (methods->type_enum))
	    {
	      arg_db_domain = pt_node_to_db_domain (parser, methods,
						    ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name,
						  is_meta, NULL, arg_num,
						  NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      type_list = methods->data_type;
	      for (type = type_list; type != NULL; type = type->next)
		{
		  arg_db_domain = pt_data_type_to_db_domain (parser, type,
							     ctemplate->name);
		  if (arg_db_domain == NULL)
		    {
		      return (er_errid ());
		    }

		  error = smt_add_set_argument_domain (ctemplate,
						       method_name,
						       is_meta, NULL,
						       arg_num, NULL,
						       arg_db_domain);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	    }
	  else
	    {
	      if (valiate_attribute_domain (parser, methods, false))
		{
		  return ER_GENERIC_ERROR;
		}
	      arg_db_domain = pt_node_to_db_domain (parser, methods,
						    ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name,
						  is_meta, NULL, arg_num,
						  NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      /* add each argument of the method that is declared. */

      args_list = methods->info.method_def.method_args_list;
      for (data_type = args_list; data_type != NULL;
	   data_type = data_type->next)
	{
	  arg_num++;

	  if (PT_IS_COLLECTION_TYPE (data_type->type_enum))
	    {
	      arg_db_domain = pt_data_type_to_db_domain (parser, data_type,
							 ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name,
						  is_meta, NULL, arg_num,
						  NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      type_list = data_type->data_type;
	      for (type = type_list; type != NULL; type = type->next)
		{
		  arg_db_domain = pt_data_type_to_db_domain (parser, type,
							     ctemplate->name);
		  if (arg_db_domain == NULL)
		    {
		      return (er_errid ());
		    }

		  error = smt_add_set_argument_domain (ctemplate,
						       method_name,
						       is_meta, NULL,
						       arg_num, NULL,
						       arg_db_domain);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	    }
	  else
	    {
	      if (valiate_attribute_domain (parser, data_type, false))
		{
		  return ER_GENERIC_ERROR;
		}
	      arg_db_domain = pt_node_to_db_domain (parser, data_type,
						    ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name,
						  is_meta, NULL, arg_num,
						  NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      methods = methods->next;
    }
  return (error);
}

/*
 * do_add_method_files() - Adds method files to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   method_files(in): Method files to add
 *
 * Note : Class object is modified
 */
int
do_add_method_files (const PARSER_CONTEXT * parser,
		     DB_CTMPL * ctemplate, PT_NODE * method_files)
{
  const char *method_file_name;
  int error = NO_ERROR;
  PT_NODE *path, *mf;

  /* add each method_file listed in the class definition */

  for (mf = method_files; mf && error == NO_ERROR; mf = mf->next)
    {
      if (mf->node_type == PT_FILE_PATH
	  && (path = mf->info.file_path.string) != NULL
	  && path->node_type == PT_VALUE
	  && (path->type_enum == PT_TYPE_VARCHAR
	      || path->type_enum == PT_TYPE_CHAR
	      || path->type_enum == PT_TYPE_NCHAR
	      || path->type_enum == PT_TYPE_VARNCHAR))
	{
	  method_file_name = (char *) path->info.value.data_value.str->bytes;
	  error = dbt_add_method_file (ctemplate, method_file_name);
	}
      else
	{
	  break;
	}
    }

  return (error);
}

/*
 * do_add_supers() - Adds super-classes to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Superclasses to add
 *
 * Note : Class object is modified
 */
int
do_add_supers (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
	       const PT_NODE * supers)
{
  MOP super_class;
  int error = NO_ERROR;


  /* Add each superclass listed in the class definition.
     Each superclass must already exist inthe database before
     it can be added. */

  while (supers && (error == NO_ERROR))
    {
      super_class = db_find_class (supers->info.name.original);
      if (super_class == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  error = dbt_add_super (ctemplate, super_class);
	}

      supers = supers->next;
    }

  return (error);
}

/*
 * do_add_resolutions() - Adds resolutions to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Resolution to add
 *
 * Note : Class object is modified
 */
int
do_add_resolutions (const PARSER_CONTEXT * parser,
		    DB_CTMPL * ctemplate, const PT_NODE * resolution)
{
  int error = NO_ERROR;
  DB_OBJECT *resolution_super_mop;
  const char *resolution_attr_mthd_name, *resolution_as_attr_mthd_name;

  /* add each conflict resolution listed in the
     class definition */

  while (resolution && (error == NO_ERROR))
    {
      resolution_super_mop =
	db_find_class (resolution->info.resolution.of_sup_class_name->info.
		       name.original);

      if (resolution_super_mop == NULL)
	{
	  error = er_errid ();
	  break;
	}

      resolution_attr_mthd_name =
	resolution->info.resolution.attr_mthd_name->info.name.original;
      if (resolution->info.resolution.as_attr_mthd_name == NULL)
	{
	  resolution_as_attr_mthd_name = NULL;
	}
      else
	{
	  resolution_as_attr_mthd_name =
	    resolution->info.resolution.as_attr_mthd_name->info.name.original;
	}

      if (resolution->info.resolution.attr_type == PT_META_ATTR)
	{
	  error = dbt_add_class_resolution (ctemplate, resolution_super_mop,
					    resolution_attr_mthd_name,
					    resolution_as_attr_mthd_name);
	}
      else
	{
	  error = dbt_add_resolution (ctemplate, resolution_super_mop,
				      resolution_attr_mthd_name,
				      resolution_as_attr_mthd_name);
	}

      resolution = resolution->next;
    }

  return (error);
}

/*
 * add_query_to_virtual_class() - Adds a query to a virtual class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Queries to add
 *
 * Note : Class object is modified
 */
static int
add_query_to_virtual_class (PARSER_CONTEXT * parser,
			    DB_CTMPL * ctemplate, const PT_NODE * queries)
{
  const char *query;
  int error = NO_ERROR;

  query = parser_print_tree (parser, queries);
  error = dbt_add_query_spec (ctemplate, query);

  return (error);
}

/*
 * add_union_query() - Adds a query to a virtual class
 *			  object. If the query is a union all query, it is
 * 			  divided into its component queries
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Union queries to add
 *
 * Note : class object modified
 */
static int
add_union_query (PARSER_CONTEXT * parser,
		 DB_CTMPL * ctemplate, const PT_NODE * query)
{
  int error = NO_ERROR;

  /* Add each query listed in the virtual class definition. */

  if (query->node_type == PT_UNION
      && query->info.query.all_distinct == PT_ALL)
    {
      error = add_union_query
	(parser, ctemplate, query->info.query.q.union_.arg1);

      if (error == NO_ERROR)
	{
	  error = add_union_query
	    (parser, ctemplate, query->info.query.q.union_.arg2);
	}
    }
  else
    {
      error = add_query_to_virtual_class (parser, ctemplate, query);
    }

  return (error);
}

/*
 * do_add_queries() - Adds a list of queries to a virtual class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   queries(in): Queries to add
 *
 * Note : Class object is modified
 */
int
do_add_queries (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
		const PT_NODE * queries)
{
  int error = NO_ERROR;

  while (queries && (error == NO_ERROR))
    {
      error = add_union_query (parser, ctemplate, queries);

      queries = queries->next;
    }

  return (error);
}

/*
 * do_set_object_id() - Sets the object_id for a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   queries(in): Object ids to set
 *
 * Note : Class object is modified
 */
int
do_set_object_id (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
		  PT_NODE * object_id_list)
{
  int error = NO_ERROR;
  PT_NODE *object_id;
  int total_ids = 0;
  DB_NAMELIST *id_list = NULL;
  const char *att_name;

  object_id = object_id_list;
  while (object_id)
    {
      att_name = object_id->info.name.original;
      if (att_name)
	{
	  (void) db_namelist_append (&id_list, att_name);
	}
      ++total_ids;
      object_id = object_id->next;
    }
  if (total_ids == 0)
    {
      if (id_list)
	{
	  db_namelist_free (id_list);
	}
      return (error);
    }

  error = dbt_set_object_id (ctemplate, id_list);
  db_namelist_free (id_list);

  return (error);
}

/*
 * do_create_vclass() - Creates a new vclass
 *   return: Error code if a vclass is not created
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   pt_node(in): Parse tree of a create vclass
 *
 * Note : Essentially, we walk the parse tree structure which describes
 *   a vclass and actually call various API functions defined
 *   in db.c to create the vclass. The most common reason for
 *   returning error is that some vclass with the given name
 *   already existed.
 */
int
do_create_local (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
		 PT_NODE * pt_node)
{
  int error = NO_ERROR;

  /* create a MOP for the ctemplate, extracting its name
     from the parse tree. */

  error = do_add_attributes (parser, ctemplate,
			     pt_node->info.create_entity.attr_def_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_attributes (parser, ctemplate,
			     pt_node->info.create_entity.class_attr_def_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_constraints (ctemplate,
			      pt_node->info.create_entity.constraint_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_methods (parser, ctemplate,
			  pt_node->info.create_entity.method_def_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_method_files (parser, ctemplate,
			       pt_node->info.create_entity.method_file_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_resolutions (parser, ctemplate,
			      pt_node->info.create_entity.resolution_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_supers (parser, ctemplate,
			 pt_node->info.create_entity.supclass_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_queries (parser, ctemplate,
			  pt_node->info.create_entity.as_query_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_set_object_id (parser, ctemplate,
			    pt_node->info.create_entity.object_id_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  return (error);
}

/*
 * do_create_entity() - Creates a new vclass
 *   return: Error code if a vclass is not created
 *   parser(in): Parser context
 *   node(in/out): Parse tree of a create vclass
 *
 * Note : Creates a new vclass.
 *    Essentially, we walk the parse tree structure
 *    which describes a vclass and actually call various
 *    API functions defined in db.c to create the
 *    vclass.
 *
 *    The most common reason for returning error
 *    is that some vclass with the given name
 *    already existed.
 */
int
do_create_entity (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int error;
  DB_CTMPL *ctemplate = NULL;
  DB_OBJECT *class_obj = NULL;
  const char *class_name;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  class_name = node->info.create_entity.entity_name->info.name.original;

  switch (node->info.create_entity.entity_type)
    {
    case PT_CLASS:
      if (node->info.create_entity.partition_info != NULL)
	{
	  error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_CREATE, false);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      ctemplate = dbt_create_class (class_name);
      break;

    case PT_VCLASS:
      ctemplate = dbt_create_vclass (class_name);
      break;

    default:
      error = ER_GENERIC_ERROR;	/* a system error */
      break;
    }

  if (ctemplate == NULL)
    {
      return er_errid ();
    }

  error = do_create_local (parser, ctemplate, node);

  if (error != NO_ERROR)
    {
      (void) dbt_abort_class (ctemplate);
      return error;
    }

  class_obj = dbt_finish_class (ctemplate);

  if (class_obj == NULL)
    {
      (void) dbt_abort_class (ctemplate);
      return er_errid ();
    }

  switch (node->info.create_entity.entity_type)
    {
    case PT_VCLASS:
      if (node->info.create_entity.with_check_option == PT_CASCADED)
	error =
	  sm_set_class_flag (class_obj, SM_CLASSFLAG_WITHCHECKOPTION, 1);
      else if (node->info.create_entity.with_check_option == PT_LOCAL)
	{
	  error =
	    sm_set_class_flag (class_obj, SM_CLASSFLAG_LOCALCHECKOPTION, 1);
	}
      break;
    case PT_CLASS:
      if (locator_has_heap (class_obj) == NULL)
	error = er_errid ();
      break;

    default:
      break;
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_class (ctemplate);
      return error;
    }

  if (node->info.create_entity.partition_info != NULL)
    {
      error = do_create_partition (parser, node, class_obj, NULL);
      if (error != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void)
		tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_CREATE);
	    }
	  return error;
	}
    }
  return NO_ERROR;
}
