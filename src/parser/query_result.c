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
 * query_result.c - Helper functions to allocate, initialize query result
 *                  descriptor for select expressions.
 */

#ident "$Id$"

#include "config.h"

#include "authenticate.h"
#include "misc_string.h"
#include "error_manager.h"
#include "parser.h"
#include "parser_message.h"
#include "server_interface.h"
#include "db_query.h"
#include "object_accessor.h"
#include "schema_manager.h"
#include "memory_alloc.h"
#include "execute_statement.h"
#include "xasl_generation.h"
#include "object_primitive.h"
#include "db.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"
#include "dbtype.h"

static int pt_find_size_from_dbtype (const DB_TYPE T_type);
static int pt_arity_of_query_type (const DB_QUERY_TYPE * qt);
static char *pt_get_attr_name (PARSER_CONTEXT * parser, PT_NODE * node);
static DB_COL_TYPE pt_get_col_type (const PARSER_CONTEXT * parser, const PT_NODE * node);
static void pt_set_domain_class (SM_DOMAIN * dom, const PT_NODE * nam, const DB_OBJECT * virt);
static void pt_set_domain_class_list (SM_DOMAIN * dom, const PT_NODE * nam, const DB_OBJECT * virt);
static SM_DOMAIN *pt_get_src_domain (PARSER_CONTEXT * parser, const PT_NODE * s, const PT_NODE * specs);
static DB_QUERY_TYPE *pt_get_node_title (PARSER_CONTEXT * parser, const PT_NODE * col, const PT_NODE * from_list);
static PT_NODE *pt_get_from_list (const PARSER_CONTEXT * parser, const PT_NODE * query);
static void pt_fixup_select_columns_type (PT_NODE * columns);

/*
 * pt_find_size_from_dbtype () -  return bytesize of memory representation of
 *                               a given primitive DB_TYPE
 *   return:  the bytesize of dt's memory representation
 *   T_type(in): a DB_TYPE
 */
static int
pt_find_size_from_dbtype (const DB_TYPE db_type)
{
  PRIM type;
  int size = 0;

  if (db_type != DB_TYPE_NULL)
    {
      type = pr_type_from_id (db_type);
      if (type && !(type->variable_p))
	{
	  size = pr_mem_size (type);
	}
    }

  return size;
}

/*
 * pt_arity_of_query_type () -  return arity (number of columns) of
 *                             a given DB_QUERY_TYPE
 *   return:  the arity (number of columns) of qt
 *   qt(in): a DB_QUERY_TYPE handle
 */

static int
pt_arity_of_query_type (const DB_QUERY_TYPE * qt)
{
  int cnt = 0;

  while (qt)
    {
      cnt++;
      qt = qt->next;
    }

  return cnt;
}

/*
 * pt_get_attr_name () - return the attribute name of a select_list item or NULL
 *   return: the attribute name of s if s is a path expression,
 *          NULL otherwise
 *   parser(in):
 *   node(in): an expression representing a select_list item
 */
static char *
pt_get_attr_name (PARSER_CONTEXT * parser, PT_NODE * node)
{
  const char *name;
  char *res = NULL;
  unsigned int save_custom = parser->custom_print;

  node = pt_get_end_path_node (node);
  if (node == NULL)
    {
      return NULL;
    }
  if (node->node_type != PT_NAME)
    {
      return NULL;
    }

  parser->custom_print = (parser->custom_print | PT_SUPPRESS_RESOLVED | PT_SUPPRESS_SELECTOR);

  name = parser_print_tree (parser, node);
  parser->custom_print = save_custom;

  if (name)
    {
      res = strdup (name);
    }

  return res;
}

/*
 * pt_get_col_type () -
 *   return:
 *   parser(in):
 *   node(in):
 */
static DB_COL_TYPE
pt_get_col_type (const PARSER_CONTEXT * parser, const PT_NODE * node)
{
  if (pt_is_expr_node (node))
    return DB_COL_EXPR;

  if (pt_is_value_node (node))
    return DB_COL_VALUE;

  if (pt_is_oid_name (node))
    return DB_COL_OID;

  if (pt_is_name_node (node))
    return DB_COL_NAME;

  if (pt_is_dot_node (node))
    {
      if (node->info.dot.arg2->node_type == PT_FUNCTION || node->info.dot.arg2->node_type == PT_METHOD_CALL)
	return DB_COL_OTHER;
      else
	return DB_COL_PATH;
    }

  if (pt_is_function (node))
    return DB_COL_FUNC;

  return DB_COL_OTHER;
}


/*
 * pt_set_domain_class() -  set SM_DOMAIN's class field
 *   return:  none
 *   dom(out): an SM_DOMAIN
 *   nam(in): an entity name
 *   virt(in):
 */
static void
pt_set_domain_class (SM_DOMAIN * dom, const PT_NODE * nam, const DB_OBJECT * virt)
{
  if (!dom || !nam || nam->node_type != PT_NAME)
    return;

  dom->type = pr_type_from_id (DB_TYPE_OBJECT);
  if (virt != NULL)
    {
      dom->class_mop = (DB_OBJECT *) virt;
    }
  else
    {
      if (nam->info.name.db_object != NULL)
	{
	  dom->class_mop = nam->info.name.db_object;
	  COPY_OID (&dom->class_oid, &(dom->class_mop->oid_info.oid));
	}
      else
	{
	  dom->class_mop = db_find_class (nam->info.name.original);
	  if (dom->class_mop != NULL)
	    {
	      COPY_OID (&dom->class_oid, &(dom->class_mop->oid_info.oid));
	    }
	}
    }
}

/*
 * pt_set_domain_class_list() -  set SM_DOMAIN's class fields
 *   return:  none
 *   dom(out): an SM_DOMAIN anchor node
 *   nam(in): an entity name list
 *   virt(in):
 */
static void
pt_set_domain_class_list (SM_DOMAIN * dom, const PT_NODE * nam, const DB_OBJECT * virt)
{
  SM_DOMAIN *tail = dom;

  while (nam && nam->node_type == PT_NAME && dom)
    {
      pt_set_domain_class (dom, nam, virt);
      nam = nam->next;

      if (!nam || nam->node_type != PT_NAME)
	break;

      dom = sm_domain_alloc ();
      assert (dom != NULL);
      tp_domain_init (dom, DB_TYPE_INTEGER);
      tail->next = dom;
      tail = dom;
    }
}

/*
 * pt_get_src_domain() -  compute & return the source domain of an expression
 *   return:  source domain of the given expression
 *   parser(in): the parser context
 *   s(in): an expression representing a select_list item
 *   specs(in): the list of specs to which s was resolved
 */
static SM_DOMAIN *
pt_get_src_domain (PARSER_CONTEXT * parser, const PT_NODE * s, const PT_NODE * specs)
{
  SM_DOMAIN *result;
  PT_NODE *spec, *entity_names, *leaf = (PT_NODE *) s;
  UINTPTR spec_id;

  result = sm_domain_alloc ();
  if (result == NULL)
    {
      return result;
    }
  tp_domain_init (result, DB_TYPE_INTEGER);

  /* if s is not a path expression then its source domain is DB_TYPE_NULL */
  result->type = pr_type_from_id (DB_TYPE_NULL);

  /* make leaf point to the last leaf name node */
  if (s->node_type == PT_DOT_)
    {
      leaf = s->info.dot.arg2;
    }

  /* s's source domain is the domain of leaf's resolvent(s) */
  if (leaf->node_type == PT_NAME && (spec_id = leaf->info.name.spec_id)
      && (spec = pt_find_entity (parser, specs, spec_id)) && (entity_names = spec->info.spec.flat_entity_list))
    {
      pt_set_domain_class_list (result, entity_names, entity_names->info.name.virt_object);
    }

  return result;
}

/*
 * pt_report_to_ersys () - report query compilation error by
 *                         setting global ER state
 *   return:
 *   parser(in): handle to parser used to process the query
 *   error_type(in): syntax, semantic, or execution
 */
void
pt_report_to_ersys (const PARSER_CONTEXT * parser, const PT_ERROR_TYPE error_type)
{
  PT_NODE *error_node;
  int err;
  char buf[1000];

  error_node = parser->error_msgs;
  if (error_node && error_node->node_type == PT_ZZ_ERROR_MSG)
    {
#if 0				/* TODO */
      assert (er_errid () != NO_ERROR);
#endif
      err = er_errid ();
      if (!ER_IS_LOCK_TIMEOUT_ERROR (err) && !ER_IS_SERVER_DOWN_ERROR (err))
	{
	  switch (error_type)
	    {
	    case PT_SYNTAX:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_SYNTAX, 2,
		      error_node->info.error_msg.error_message, "");
	      break;
	    case PT_SEMANTIC:
	    default:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_SEMANTIC, 2,
		      error_node->info.error_msg.error_message, "");
	      break;
	    case PT_EXECUTION:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_EXECUTE, 2,
		      error_node->info.error_msg.error_message, "");
	      break;
	    }
	}
      return;
    }

  /* a system error reporting error messages */
  sprintf (buf, "Internal error- reporting %s error.",
	   (error_type == PT_SYNTAX ? "syntax" : (error_type == PT_SEMANTIC ? "semantic" : "execution")));

  er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_EXECUTE, 2, buf, "");
}

/*
 * pt_report_to_ersys_with_statement () - report query compilation error by
 *                                        setting global ER state
 *   return:
 *   parser(in): handle to parser used to process the query
 *   error_type(in): syntax, semantic, or execution
 *   statement(in): statement tree
 */
void
pt_report_to_ersys_with_statement (PARSER_CONTEXT * parser, const PT_ERROR_TYPE error_type, PT_NODE * statement)
{
  PT_NODE *error_node;
  char buf[1000];
  char *stmt_string = NULL;
  int err;

  if (parser == NULL)
    {
      return;
    }

  error_node = parser->error_msgs;
  if (statement)
    {
      PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE | PT_SHORT_PRINT);
      stmt_string = (char *) statement->alias_print;
    }
  if (stmt_string == NULL)
    {
      stmt_string = (char *) "";
    }

  if (error_node && error_node->node_type == PT_ZZ_ERROR_MSG)
    {
#if 0				/* TODO */
      assert (er_errid () != NO_ERROR);
#endif
      err = er_errid ();
      if (!ER_IS_LOCK_TIMEOUT_ERROR (err) && !ER_IS_SERVER_DOWN_ERROR (err))
	{
	  switch (error_type)
	    {
	    case PT_SYNTAX:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_SYNTAX, 2,
		      error_node->info.error_msg.error_message, stmt_string);
	      break;
	    case PT_SEMANTIC:
	    default:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_SEMANTIC, 2,
		      error_node->info.error_msg.error_message, stmt_string);
	      break;
	    case PT_EXECUTION:
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_EXECUTE, 2,
		      error_node->info.error_msg.error_message, stmt_string);
	      break;
	    }
	}
      return;
    }

  /* a system error reporting error messages */
  sprintf (buf, "Internal error- reporting %s error.",
	   (error_type == PT_SYNTAX ? "syntax" : (error_type == PT_SEMANTIC ? "semantic" : "execution")));

  er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_EXECUTE, 2, buf, stmt_string);
}				/* pt_report_to_ersys_with_statement() */

/*
 * pt_get_select_list () - PT_NODE *, a pointer to query's select list
 *	                   NULL if query is a 'SELECT *' query
 *   return:
 *   parser(in): handle to parser used to process & derive query
 *   query(out): abstract syntax tree form of a SELECT expression
 */
PT_NODE *
pt_get_select_list (PARSER_CONTEXT * parser, PT_NODE * query)
{
  PT_NODE *list, *attr;
  PT_NODE *arg1, *arg2, *attr1, *attr2, *select_list, *col, *next;
  int cnt1, cnt2;
  PT_TYPE_ENUM common_type;

  if (query == NULL)
    {
      return NULL;
    }

  switch (query->node_type)
    {
    default:
      return NULL;

    case PT_SELECT:
      list = query->info.query.q.select.list;
      if (list == NULL)
	{
	  return NULL;
	}

      /* return the first row of PT_NODE_LIST */
      if (list->node_type == PT_NODE_LIST)
	{
	  return list->info.node_list.list;
	}

      if (list->node_type == PT_VALUE && list->type_enum == PT_TYPE_STAR)
	{
	  return NULL;
	}

      for (attr = list; attr; attr = attr->next)
	{
	  if (attr->node_type == PT_NAME && attr->type_enum == PT_TYPE_STAR)
	    {
	      /* found "class_name.*" */
	      return NULL;
	    }
	}

      return list;

    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      select_list = query->info.query.q.union_.select_list;

      assert (query->parser_id == parser->id);
      if (select_list && select_list->parser_id != parser->id)
	{
	  /*
	   * Union PT_NODE keeps select_list as reference
	   * this case means, this parser copy other parsers tree
	   * but union.info.select_list points old reference
	   *
	   * this function can free & realloc select_list->data_type
	   * so, to prevent modifying (other parser's) original
	   * tree, deep copy select_list in this parser's context
	   */
	  select_list = parser_copy_tree_list (parser, select_list);
	  query->info.query.q.union_.select_list = select_list;
	}

      arg1 = pt_get_select_list (parser, query->info.query.q.union_.arg1);
      arg2 = pt_get_select_list (parser, query->info.query.q.union_.arg2);

      if (select_list == NULL)
	{
	  /* if arg1 or arg2 is null, there need be resolved firstly. */
	  if (arg1 == NULL || arg2 == NULL)
	    {
	      return NULL;
	    }

	  cnt1 = pt_length_of_select_list (arg1, EXCLUDE_HIDDEN_COLUMNS);
	  cnt2 = pt_length_of_select_list (arg2, EXCLUDE_HIDDEN_COLUMNS);

	  if (cnt1 != cnt2)
	    {
	      PT_ERRORmf2 (parser, arg1, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ARITY_MISMATCH, cnt1, cnt2);
	      return NULL;
	    }

	  select_list = parser_copy_tree_list (parser, arg1);
	  if (cnt1 != pt_length_of_select_list (arg1, INCLUDE_HIDDEN_COLUMNS))
	    {
	      /* hidden column is included. get rid of it */
	      for (col = select_list; col && col->next; col = next)
		{
		  next = col->next;
		  if (next->flag.is_hidden_column)
		    {
		      parser_free_tree (parser, next);
		      col->next = NULL;
		      break;
		    }
		}
	    }
	}

      for (col = select_list, attr1 = arg1, attr2 = arg2; col && attr1 && attr2;
	   col = col->next, attr1 = attr1->next, attr2 = attr2->next)
	{
	  /* preserve the ENUM type as common type between two ENUMs in UNION, DIFFERENCE and INTERSECTION. The ENUM
	   * domains already have been verified and if they are different an error is generated. */
	  if (attr1->type_enum == attr2->type_enum && attr1->type_enum == PT_TYPE_ENUMERATION)
	    {
	      common_type = PT_TYPE_ENUMERATION;
	    }
	  else
	    {
	      common_type = pt_common_type (attr1->type_enum, attr2->type_enum);
	    }

	  if (pt_is_value_node (col) && (col->type_enum == PT_TYPE_NA || col->type_enum == PT_TYPE_NULL))
	    {
	      db_make_null (&col->info.value.db_value);
	    }

	  if ((attr2->type_enum == PT_TYPE_NONE) || (attr2->type_enum == PT_TYPE_NA)
	      || (attr2->type_enum == PT_TYPE_NULL))
	    {
	      /* convert type to that of non-null */
	      if (col->type_enum == PT_TYPE_NA && col->alias_print == NULL)
		{
		  col->alias_print = pt_append_string (parser, NULL, "na");
		}
	      else if (col->type_enum == PT_TYPE_NULL && col->alias_print == NULL)
		{
		  col->alias_print = pt_append_string (parser, NULL, "null");
		}
	      col->type_enum = attr1->type_enum;
	      if (col->data_type)
		{
		  parser_free_tree (parser, col->data_type);
		}
	      col->data_type = parser_copy_tree_list (parser, attr1->data_type);

	    }
	  else if ((attr1->type_enum == PT_TYPE_NONE) || (attr1->type_enum == PT_TYPE_NA)
		   || (attr1->type_enum == PT_TYPE_NULL))
	    {
	      /* convert type to that of non-null */
	      if (col->type_enum == PT_TYPE_NA && col->alias_print == NULL)
		{
		  col->alias_print = pt_append_string (parser, NULL, "na");
		}
	      else if (col->type_enum == PT_TYPE_NULL && col->alias_print == NULL)
		{
		  col->alias_print = pt_append_string (parser, NULL, "null");
		}
	      col->type_enum = attr2->type_enum;
	      if (col->data_type)
		{
		  parser_free_tree (parser, col->data_type);
		}
	      col->data_type = parser_copy_tree_list (parser, attr2->data_type);
	    }
	  else if (PT_IS_PARAMETERIZED_TYPE (common_type)
		   || (common_type != PT_TYPE_NONE && col->type_enum != common_type))
	    {
	      col->type_enum = common_type;

	      if (col->data_type)
		{
		  parser_free_tree (parser, col->data_type);
		  col->data_type = NULL;
		}
	      if (PT_IS_COMPLEX_TYPE (common_type))
		{
		  col->data_type = parser_copy_tree_list (parser, attr2->data_type);
		}
	    }
	}

      if (select_list != query->info.query.q.union_.select_list)
	{
	  parser_free_tree (parser, query->info.query.q.union_.select_list);
	}

      query->info.query.q.union_.select_list = select_list;

      return select_list;
    }
}

/*
 * pt_get_from_list () - returns a pointer to query's from list
 *   return:
 *   parser(in): handle to parser used to process & derive query
 *   query(in): abstract syntax tree form of a SELECT expression
 */
static PT_NODE *
pt_get_from_list (const PARSER_CONTEXT * parser, const PT_NODE * query)
{
  if (query == NULL)
    {
      return NULL;
    }

  switch (query->node_type)
    {
    default:
      return NULL;

    case PT_SELECT:
      return query->info.query.q.select.from;

    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      return pt_get_from_list (parser, query->info.query.q.union_.arg1);
    }
}

static void
pt_fixup_select_columns_type (PT_NODE * columns)
{
  PT_NODE *col = NULL;
  for (col = columns; col != NULL; col = col->next)
    {
      pt_fixup_column_type (col);
    }
}

/*
 * pt_get_titles() - creates, initializes, returns DB_QUERY_TYPE describing the
 *   		     output columns titles of the given query
 *   return:  DB_QUERY_TYPE*, a descriptor of query's output columns
 *   parser(in/out): handle to parser used to process & derive query
 *   query(out): abstract syntax tree form of a SELECT expression
 */

DB_QUERY_TYPE *
pt_get_titles (PARSER_CONTEXT * parser, PT_NODE * query)
{
  DB_QUERY_TYPE *q, *t, *tail;
  PT_NODE *s, *f;

  s = pt_get_select_list (parser, query);
  if (pt_length_of_select_list (s, EXCLUDE_HIDDEN_COLUMNS) <= 0)
    {
      return NULL;
    }
  f = pt_get_from_list (parser, query);

  for (q = NULL, tail = NULL; s; s = s->next)
    {
      if (s->flag.is_hidden_column)
	{
	  continue;
	}
      else
	{
	  parser->custom_print |= PT_SUPPRESS_CHARSET_PRINT;
	  t = pt_get_node_title (parser, s, f);
	  parser->custom_print &= ~PT_SUPPRESS_CHARSET_PRINT;

	  if (t == NULL)
	    {
	      db_free_query_format (q);
	      return NULL;
	    }

	  if (q == NULL)
	    {
	      q = t;
	    }
	  else
	    {
	      tail->next = t;
	    }

	  t->next = NULL;
	  tail = t;
	}
    }

  return q;
}

/*
 * pt_get_node_title() -  allocate and initialize a query_type node.
 *   return:  a fully initialized query type node
 *   parser(in/out): handle to parser used to process & derive query
 *   col(in): column to create the query type node from.
 *   from_list(in):
 *
 */

static DB_QUERY_TYPE *
pt_get_node_title (PARSER_CONTEXT * parser, const PT_NODE * col, const PT_NODE * from_list)
{
  DB_QUERY_TYPE *q;
  char *name;
  unsigned int save_custom;
  PT_NODE *node, *spec, *range_var;
  char *original_name;

  save_custom = parser->custom_print;
  parser->custom_print |= PT_SUPPRESS_QUOTES;

  if ((q = (DB_QUERY_TYPE *) malloc (DB_SIZEOF (DB_QUERY_TYPE))) == NULL)
    {
      goto error;
    }
  q->visible_type = USER_COLUMN;
  q->next = NULL;
  q->db_type = DB_TYPE_NULL;
  q->size = 0;

  if (pt_resolved (col))
    {
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
    }

  original_name = name = pt_print_alias (parser, col);

  if (col->alias_print == NULL)
    {
      switch (col->node_type)
	{
	case PT_NAME:
	  if (pt_resolved (col))
	    {
	      if (col->info.name.meta_class == PT_META_ATTR)
		{
		  name =
		    pt_append_string (parser, pt_append_string (parser, (char *) col->info.name.resolved, "."), name);
		  name = pt_append_string (parser, pt_append_string (parser, NULL, "class "), name);
		  original_name = name;
		}
	      else if (PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_DOT_NAME))
		{
		  /* PT_NAME comes from PT_DOT_ */
		  original_name =
		    pt_append_string (parser, pt_append_string (parser, (char *) col->info.name.resolved, "."),
				      original_name);
		}
	      else if (PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_DOT_STAR))
		{
		  /* PT_NAME comes from classname.* */
		  original_name = NULL;
		}
	      else if (PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_STAR))
		{
		  /* PT_NAME comes from * */
		  original_name = NULL;
		}
	    }
	  break;
	case PT_DOT_:

	  /* traverse left node */
	  node = (PT_NODE *) col;
	  while (node && node->node_type == PT_DOT_)
	    {
	      node = node->info.dot.arg1;
	    }

	  if (node && node->node_type == PT_NAME)
	    {
	      if (pt_resolved (col))
		{
		  if (node->info.name.meta_class == PT_META_ATTR)
		    {
		      name =
			pt_append_string (parser, pt_append_string (parser, (char *) node->info.name.resolved, "."),
					  name);
		      name = pt_append_string (parser, pt_append_string (parser, NULL, "class "), name);
		      original_name = name;
		    }
		  else if (PT_NAME_INFO_IS_FLAGED (node, PT_NAME_INFO_DOT_NAME))
		    {
		      /* PT_NAME comes from PT_DOT_ */
		      original_name =
			pt_append_string (parser, pt_append_string (parser, (char *) node->info.name.resolved, "."),
					  original_name);
		    }
		}
	      else if (node->info.name.meta_class == PT_NORMAL)
		{
		  /* check for classname */
		  for (spec = (PT_NODE *) from_list; spec; spec = spec->next)
		    {
		      /* get spec's range variable if range variable for spec is used, use range_var. otherwise, use
		       * entity_name */
		      range_var = spec->info.spec.range_var ? spec->info.spec.range_var : spec->info.spec.entity_name;

		      if (pt_check_path_eq (parser, range_var, node) == 0)
			{


			  if (original_name)
			    {
			      /* strip off classname.* */
			      name = strchr (original_name, '.');
			      if (name == NULL || name[0] == '\0')
				{
				  name = original_name;
				}
			      else
				{
				  name++;
				}
			      break;
			    }
			  else
			    {
			      name = NULL;
			    }

			}
		    }
		}
	    }
	  break;
	default:
	  break;
	}
    }

  if (name)
    {
      q->name = strdup (name);
      if (q->name == NULL)
	{
	  goto error;
	}
    }
  else
    {
      q->name = NULL;
    }

  if (original_name)
    {
      q->original_name = strdup (original_name);
      if (q->original_name == NULL)
	{
	  if (q->name)
	    {
	      free_and_init (q->name);
	    }

	  goto error;
	}
    }
  else
    {
      /* PT_NAME comes from classname.* or * */
      q->original_name = NULL;
    }

  q->attr_name = pt_get_attr_name (parser, (PT_NODE *) col);
  q->spec_name = NULL;		/* fill it at pt_fillin_type_size() */

  /* At this time before query compilation, we cannot differentiate qualified attribute name(DB_COL_NAME) from path
   * expression(DB_COL_PATH). */
  q->col_type = pt_get_col_type (parser, col);
  q->domain = NULL;
  q->src_domain = NULL;

  parser->custom_print = save_custom;
  return q;

error:
  parser->custom_print = save_custom;
  if (q)
    free_and_init (q);
  return NULL;
}				/* pt_get_node_title */


/*
 * pt_fillin_type_size() -  set the db_type&size fields of a DB_QUERY_TYPE list
 *   return:  list, a fully initialized descriptor of query's output columns
 *   parser(in): handle to parser used to process & derive query
 *   query(out): abstract syntax tree form of a SELECT expression
 *   list(in/out): a partially initialized DB_QUERY_TYPE list
 *   oids_included(in):
 *   fixup_columns_type(in): whether fixup column type
 */

DB_QUERY_TYPE *
pt_fillin_type_size (PARSER_CONTEXT * parser, PT_NODE * query, DB_QUERY_TYPE * list, const int oids_included,
		     bool want_spec_entity_name, bool fixup_columns_type)
{
  DB_QUERY_TYPE *q, *t;
  PT_NODE *s, *from_list;
  const char *spec_name;
  PT_NODE *node, *spec;
  UINTPTR spec_id;
  char *original_name;

  s = pt_get_select_list (parser, query);
  if (s == NULL || list == NULL)
    {
      return list;
    }

  if (fixup_columns_type)
    {
      /* fixup the columns of union statement */
      pt_fixup_select_columns_type (s);
    }
  from_list = pt_get_from_list (parser, query);
  /* from_list is allowed to be NULL for supporting SELECT without references to tables */

  if (oids_included == 1)
    {
      /*
       * prepend single oid column onto the type list
       * the first node of the select list will be the oid column.
       */
      q = pt_get_node_title (parser, s, from_list);

      if (q == NULL)
	{
	  db_free_query_format (list);
	  return NULL;
	}
      q->visible_type = OID_COLUMN;	/* oid columns are NOT user visible */
      q->next = list;
      list = q;
    }

  if (pt_length_of_select_list (s, EXCLUDE_HIDDEN_COLUMNS) != pt_arity_of_query_type (list))
    {
      PT_INTERNAL_ERROR (parser, "query result");
      return list;
    }

  for (t = list; s && t; s = s->next, t = t->next)
    {
      t->col_type = pt_get_col_type (parser, s);
      t->db_type = pt_type_enum_to_db (s->type_enum);
      t->size = pt_find_size_from_dbtype (t->db_type);
      t->domain = pt_xasl_node_to_domain (parser, s);
      t->src_domain = pt_get_src_domain (parser, s, from_list);

      spec_name = NULL;
      /* if it is attribute, find spec name */
      if (pt_is_attr (s))
	{
	  node = s;
	  while (node->node_type == PT_DOT_)
	    {
	      node = node->info.dot.arg1;	/* root node for path expression */
	    }
	  if (node->node_type == PT_NAME && (spec_id = node->info.name.spec_id)
	      && (spec = pt_find_entity (parser, from_list, spec_id)))
	    {
	      if (want_spec_entity_name == true && spec->info.spec.entity_name)
		{
		  spec_name = spec->info.spec.entity_name->info.name.original;
		}
	      else if (want_spec_entity_name == false && spec->info.spec.range_var)
		{
		  spec_name = spec->info.spec.range_var->info.name.original;
		}
	    }
	}
      /* if it is method, find spec name */
      if (pt_is_method_call (s))
	{
	  node = s;
	  while (node->node_type == PT_DOT_)
	    {
	      node = node->info.dot.arg2;	/* leaf node for qualified method */
	    }
	  node = node->info.method_call.method_name;
	  if (node->node_type == PT_NAME && (spec_id = node->info.name.spec_id)
	      && (spec = pt_find_entity (parser, from_list, spec_id)))
	    {
	      if (want_spec_entity_name == true && spec->info.spec.entity_name)
		{
		  spec_name = spec->info.spec.entity_name->info.name.original;
		}
	      else if (want_spec_entity_name == false && spec->info.spec.range_var)
		{
		  spec_name = spec->info.spec.range_var->info.name.original;
		}
	    }
	}

      t->spec_name = (spec_name) ? strdup (spec_name) : NULL;

      if (!t->original_name)
	{
	  /* PT_NAME comes from classname.* or build original_name( = spec_name.name) */
	  if (pt_length_of_list (from_list) == 1)
	    {
	      /* there is only one class spec */
	      original_name = pt_append_string (parser, NULL, t->name);
	      t->original_name = (char *) malloc (strlen (original_name) + 1);
	    }
	  else
	    {
	      /* there are plural class specs */
	      original_name = pt_append_string (parser, pt_append_string (parser, (char *) t->spec_name, "."), t->name);
	      t->original_name = (char *) malloc (strlen (original_name) + 1);
	    }
	  if (!t->original_name)
	    {
	      PT_INTERNAL_ERROR (parser, "insufficient memory");
	      return list;
	    }
	  strcpy ((char *) t->original_name, original_name);
	}
    }

  return list;
}

/*
 * pt_new_query_result_descriptor() - allocates, initializes, returns a new
 *      query result descriptor and opens a cursor for the query's results
 *   return:  DB_QUERY_RESULT* with an open cursor
 *   parser(in): handle to parser used to process & derive query
 *   query(out): abstract syntax tree (AST) form of a query statement
 */

DB_QUERY_RESULT *
pt_new_query_result_descriptor (PARSER_CONTEXT * parser, PT_NODE * query)
{
  int degree;
  DB_QUERY_RESULT *r = NULL;
  QFILE_LIST_ID *list_id;
  int oids_included = 0;
  bool failure = false;

  if (query == NULL)
    {
      return NULL;
    }

  switch (query->node_type)
    {
    default:
      return NULL;
      break;

    case PT_EXECUTE_PREPARE:
      if (query->info.execute.stmt_type != CUBRID_STMT_SELECT)
	{
	  return NULL;
	}
      oids_included = query->info.execute.oids_included;
      degree = query->info.execute.column_count;
      break;

    case PT_INSERT:
      oids_included = 0;
      degree = 1;
      break;

    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
    case PT_SELECT:
      oids_included = query->info.query.oids_included;
      degree = 0;
      degree = pt_length_of_select_list (pt_get_select_list (parser, query), EXCLUDE_HIDDEN_COLUMNS);
      break;
    }

  r = db_alloc_query_result (T_SELECT, degree);
  if (r == NULL)
    {
      return NULL;
    }

  db_init_query_result (r, T_SELECT);
  r->type = T_SELECT;
  r->col_cnt = degree;

  r->oid_included = oids_included == 1;
  r->res.s.query_id = parser->query_id;
  r->res.s.stmt_id = 0;
  r->res.s.stmt_type = CUBRID_STMT_SELECT;
  r->res.s.cache_time = query->cache_time;

  /* the following is for clean up when the query fails */
  memset (&r->res.s.cursor_id.list_id, 0, sizeof (QFILE_LIST_ID));
  r->res.s.cursor_id.query_id = parser->query_id;
  r->res.s.cursor_id.buffer = NULL;
  r->res.s.cursor_id.tuple_record.tpl = NULL;
  r->res.s.holdable = parser->flag.is_holdable;

  list_id = (QFILE_LIST_ID *) query->etc;
  r->type_cnt = degree;
  if (list_id)
    {
      failure = !cursor_open (&r->res.s.cursor_id, list_id, false, r->oid_included);
      /* free result, which was copied by open cursor operation! */
      cursor_free_self_list_id (list_id);
    }
  else
    {
      QFILE_LIST_ID empty_list_id;
      QFILE_CLEAR_LIST_ID (&empty_list_id);
      failure = !cursor_open (&r->res.s.cursor_id, &empty_list_id, false, r->oid_included);
    }

  if (failure)
    {
      db_free_query_result (r);
      r = NULL;
    }

  return r;
}

/*
 * pt_make_cache_hit_result_descriptor () -
 *   return:
 */
DB_QUERY_RESULT *
pt_make_cache_hit_result_descriptor (void)
{
  DB_QUERY_RESULT *r;

  r = db_alloc_query_result (T_CACHE_HIT, 0);
  if (r == NULL)
    {
      return NULL;
    }

  db_init_query_result (r, T_CACHE_HIT);

  return r;
}


/*
 * pt_free_query_etc_area () -
 *   return: none
 *   session(in):
 *   query(in): abstract syntax tree (AST) form of a query statement
 */
void
pt_free_query_etc_area (PARSER_CONTEXT * parser, PT_NODE * query)
{
  if (query && query->etc
      && (pt_node_to_cmd_type (query) == CUBRID_STMT_SELECT || pt_node_to_cmd_type (query) == CUBRID_STMT_DO
	  || pt_is_server_insert_with_generated_keys (parser, query)))
    {
      cursor_free_self_list_id (query->etc);
    }
}


/*
 * pt_end_query() -
 *   return:
 *   parser(in): parser context
 *   query_id_self(in):
 */
void
pt_end_query (PARSER_CONTEXT * parser, QUERY_ID query_id_self)
{
  assert (parser != NULL);
  assert (query_id_self != 0);
  assert (query_id_self == NULL_QUERY_ID || query_id_self > 0);

  if (parser->query_id > 0)
    {
      if (!tran_was_latest_query_ended () && er_errid () != ER_LK_UNILATERALLY_ABORTED)
	{
	  qmgr_end_query (parser->query_id);
	}
    }
  else
    {
      assert (parser->query_id == 0);
    }

  parser->query_id = query_id_self;
}


/*
 * db_object_describe() -  get a DB_QUERY_TYPE descriptor of the named
 *                         attributes of a given object
 *   return:  int (non-zero in case of error)
 *   obj_mop(in): a DB_OBJECT in the workspace
 *   num_attrs(in): number of names in attrs array
 *   attrs(in): an array of null-terminated character strings
 *   col_spec(out): a new DB_QUERY_TYPE structure
 */
int
db_object_describe (DB_OBJECT * obj_mop, int num_attrs, const char **attrs, DB_QUERY_TYPE ** col_spec)
{
  DB_QUERY_TYPE *t;
  int i, attrid, shared, err = NO_ERROR;
  MOP class_mop;
  const char **name;
  SM_DOMAIN *tmp_dom;

  CHECK_CONNECT_ERROR ();

  if (!col_spec)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return -1;
    }

  *col_spec = NULL;

  class_mop = WS_CLASS_MOP (obj_mop);
  if (class_mop == NULL)
    {
      return -1;
    }

  *col_spec = db_alloc_query_format (num_attrs);
  if (*col_spec == NULL)
    {
      return -1;
    }

  for (i = 0, t = *col_spec, name = attrs; i < num_attrs && t && name && err == NO_ERROR; i++, t = t->next, name++)
    {
      t->db_type = sm_att_type_id (class_mop, *name);
      t->size = pt_find_size_from_dbtype (t->db_type);
      t->name = (char *) malloc (1 + strlen (*name));
      if (t->name)
	{
	  strcpy ((char *) t->name, *name);
	}
      else
	{
	  db_free_query_format (*col_spec);
	  *col_spec = NULL;
	  return -1;
	}

      t->attr_name = NULL;
      t->src_domain = NULL;
      err = sm_att_info (class_mop, *name, &attrid, &tmp_dom, &shared, 0);
      t->domain = sm_domain_copy (tmp_dom);
    }

  if (err != NO_ERROR)
    {
      db_free_query_format (*col_spec);
      *col_spec = NULL;
      return -1;
    }

  return 0;
}

/*
 * db_object_fetch() -  get the values of the named attributes of the given
 *                      object into a DB_QUERY_RESULT structure
 *   return:  int (non-zero in event of failure)
 *   obj_mop(in): a DB_OBJECT in the workspace
 *   num_attrs(in): number of names in attrs array
 *   attrs(in): an array of null-terminated character strings
 *   result(out): a new DB_QUERY_RESULT structure
 */

int
db_object_fetch (DB_OBJECT * obj_mop, int num_attrs, const char **attrs, DB_QUERY_RESULT ** result)
{
  MOP class_mop;
  DB_QUERY_RESULT *r;
  DB_QUERY_TYPE *t;
  int k;
  const char **name;
  int err = NO_ERROR;
  DB_VALUE **v;
  bool r_inited = false;

  CHECK_CONNECT_ERROR ();

  if (!result)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return -1;
    }

  *result = NULL;
  class_mop = sm_get_class (obj_mop);
  if (class_mop == NULL)
    {
      goto err_end;
    }

  r = *result = db_alloc_query_result (T_OBJFETCH, num_attrs);
  if (r == NULL)
    {
      goto err_end;
    }
  r_inited = true;

  db_init_query_result (r, T_OBJFETCH);
  r->type = T_OBJFETCH;
  r->col_cnt = num_attrs;
  r->oid_included = false;

  /* allocate and initialize type list */
  r->type_cnt = num_attrs;
  r->query_type = db_alloc_query_format (num_attrs);
  if (!r->query_type)
    {
      goto err_end;
    }

  r->res.o.crs_pos = C_BEFORE;
  for (k = 0, t = r->query_type, name = attrs, v = r->res.o.valptr_list;
       k < num_attrs && t && name && v && err == NO_ERROR; k++, t = t->next, name++, v++)
    {
      t->db_type = sm_att_type_id (class_mop, *name);
      t->size = pt_find_size_from_dbtype (t->db_type);
      t->name = (char *) malloc (1 + strlen (*name));
      if (t->name)
	{
	  strcpy ((char *) t->name, *name);
	}
      else
	{
	  goto err_end;
	}

      *v = pr_make_ext_value ();
      if (*v == NULL)
	{
	  goto err_end;
	}

      err = obj_get (obj_mop, *name, *v);
    }

  if (err != NO_ERROR)
    {
      goto err_end;
    }

  err = 0;
  goto end;

err_end:
  if (r_inited)
    {
      db_free_query_result (r);
    }
  *result = NULL;
  err = -1;

end:
  return err;
}

/*
 * db_get_attribute () -
 *   return:
 *   obj(in):
 *   name(in):
 */
DB_ATTRIBUTE *
db_get_attribute_force (DB_OBJECT * obj, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  att = NULL;
  if (au_fetch_class_force (obj, &class_, AU_FETCH_READ) == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, name);
	}
    }

  return ((DB_ATTRIBUTE *) att);
}

/*
 * db_get_attributes_force () -
 *   return:
 *   obj(in):
 */
DB_ATTRIBUTE *
db_get_attributes_force (DB_OBJECT * obj)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *atts;

  atts = NULL;
  if (au_fetch_class_force ((DB_OBJECT *) obj, &class_, AU_FETCH_READ) == NO_ERROR)
    {
      atts = class_->ordered_attributes;
      if (atts == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_COMPONENTS, 0);
	}
    }
  return ((DB_ATTRIBUTE *) atts);
}


/*
 * pt_find_users_class () -
 *   return: class object if found
 *   parser(in):
 *   name(in/out):
 */
DB_OBJECT *
pt_find_users_class (PARSER_CONTEXT * parser, PT_NODE * name)
{
  DB_OBJECT *object;

  object = db_find_class (name->info.name.original);

  if (!object)
    {
      PT_ERRORmf (parser, name, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CLASS_DOES_NOT_EXIST,
		  name->info.name.original);
    }
  name->info.name.db_object = object;

  pt_check_user_owns_class (parser, name);

  return object;
}

/*
 * pt_is_server_insert_with_generated_keys () - check if an insert statement
 *				has been executed on the server and it must
 *				return the autogenerated keys.
 * return:
 * session(in) :
 * statement(in) :
 */
int
pt_is_server_insert_with_generated_keys (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  if (statement && statement->node_type == PT_INSERT && parser && parser->flag.return_generated_keys
      && statement->info.insert.server_allowed == SERVER_INSERT_IS_ALLOWED)
    {
      return 1;
    }

  return 0;
}
