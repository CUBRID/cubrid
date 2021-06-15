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
 * compile.c - compile parse tree into executable form
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "authenticate.h"
#include "dbi.h"
#include "parser.h"
#include "semantic_check.h"
#include "locator_cl.h"
#include "memory_alloc.h"
#include "schema_manager.h"
#include "parser_message.h"
#include "view_transform.h"
#include "intl_support.h"
#include "server_interface.h"
#include "network_interface_cl.h"
#include "execute_statement.h"
#include "transaction_cl.h"
#include "dbtype.h"

typedef struct trigger_exec_info TRIGGER_EXEC_INFO;
struct trigger_exec_info
{
  UINTPTR spec_id1;
  UINTPTR spec_id2;
  DB_OBJECT *object1;
  DB_OBJECT *object2;
  const char *name1;
  const char *name2;
  int path_expr_level;
  int trig_corr_path;		/* path expr rooted by trigger corr name */
  bool is_update_object;
};

/* structure used for parser_walk_tree in pt_class_pre_fetch */
typedef struct pt_class_locks PT_CLASS_LOCKS;
struct pt_class_locks
{
  int num_classes;
  int allocated_count;
  DB_FETCH_MODE lock_type;
  char **classes;
  int *only_all;
  LOCK *locks;
  LC_PREFETCH_FLAGS *flags;
};

enum pt_order_by_adjustment
{
  PT_ADD_ONE,
  PT_TIMES_TWO
};

static PT_NODE *pt_add_oid_to_select_list (PARSER_CONTEXT * parser, PT_NODE * statement, VIEW_HANDLING how);
static PT_NODE *pt_count_entities (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static int pt_add_lock_class (PARSER_CONTEXT * parser, PT_CLASS_LOCKS * lcks, PT_NODE * spec, LC_PREFETCH_FLAGS flags);
static PT_NODE *pt_find_lck_classes (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_find_lck_class_from_partition (PARSER_CONTEXT * parser, PT_NODE * node, PT_CLASS_LOCKS * locks);
static int pt_in_lck_array (PT_CLASS_LOCKS * lcks, const char *str, LC_PREFETCH_FLAGS flags);

static void remove_appended_trigger_info (char *msg, int with_evaluate);

static PT_NODE *pt_set_trigger_obj_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_set_trigger_obj_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_set_class_chn (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);

/*
 * pt_spec_to_oid_attr () - Generate an oid attribute from a resolved spec.
 * 			    Can be called any time after name resolution.
 *   return:  a PT_NAME node, or a NULL
 *   parser(in): the parser context used to derive stmt
 *   spec(in/out): an entity spec. requires spec has been resolved
 *   how(in):
 */

PT_NODE *
pt_spec_to_oid_attr (PARSER_CONTEXT * parser, PT_NODE * spec, VIEW_HANDLING how)
{
  PT_NODE *node = NULL, *oid = NULL;
  PT_NODE *flat;
  PT_NODE *range;

  if (spec->info.spec.range_var == NULL)
    return NULL;

  flat = spec->info.spec.flat_entity_list;
  range = spec->info.spec.range_var;

  if (PT_SPEC_IS_DERIVED (spec) && spec->info.spec.flat_entity_list && spec->info.spec.as_attr_list)
    {
      /* this spec should have come from a vclass that was rewritten as a derived table; pull ROWOID/CLASSOID from
       * as_attr_list NOTE: see mq_rewrite_derived_table_for_update () */
      switch (how)
	{
	case OID_NAME:
	  return parser_copy_tree (parser, spec->info.spec.as_attr_list);

	case CLASSOID_NAME:
	case HIDDEN_CLASSOID_NAME:
	  node = parser_copy_tree (parser, spec->info.spec.as_attr_list);
	  break;

	default:
	  /* should not be here */
	  return NULL;
	}
    }

  if (how == OID_NAME || how == CLASSOID_NAME || how == HIDDEN_CLASSOID_NAME || !flat
      || (!flat->info.name.virt_object || mq_is_updatable (flat->info.name.virt_object)))
    {
      /* just generate an oid name, if that is what is asked for or the class is not a proxy and there is no view or
       * the view is updatable */
      if (node != NULL)
	{
	  oid = node;
	}
      else
	{
	  oid = pt_name (parser, "");
	  if (oid)
	    {
	      oid->info.name.resolved = range->info.name.original;
	      oid->info.name.meta_class = PT_OID_ATTR;
	      oid->info.name.spec_id = spec->info.spec.id;
	      oid->type_enum = PT_TYPE_OBJECT;
	      oid->data_type = parser_new_node (parser, PT_DATA_TYPE);
	    }
	  else
	    {
	      return NULL;
	    }
	  if (oid->data_type)
	    {
	      oid->data_type->type_enum = PT_TYPE_OBJECT;
	      oid->data_type->info.data_type.entity = parser_copy_tree_list (parser, flat);
	    }
	  else
	    {
	      return NULL;
	    }

	  if (flat)
	    {
	      oid->data_type->info.data_type.virt_object = flat->info.name.virt_object;
	      oid->data_type->info.data_type.virt_type_enum = flat->info.name.virt_type_enum;
	    }
	}
      if (how == CLASSOID_NAME || how == HIDDEN_CLASSOID_NAME)
	{
	  PT_NODE *func, *tmp;

	  /* make into a class_of function with the generated OID as the argument */
	  func = parser_new_node (parser, PT_FUNCTION);
	  if (func)
	    {
	      func->info.function.function_type = F_CLASS_OF;
	      func->info.function.arg_list = oid;
	      func->type_enum = PT_TYPE_OBJECT;
	      func->data_type = parser_new_node (parser, PT_DATA_TYPE);
	    }
	  else
	    {
	      return NULL;
	    }
	  if (func->data_type)
	    {
	      func->data_type->type_enum = PT_TYPE_OBJECT;
	      func->data_type->info.data_type.entity = parser_copy_tree_list (parser, flat);
	      for (tmp = func->data_type->info.data_type.entity; tmp != NULL; tmp = tmp->next)
		{
		  tmp->info.name.meta_class = PT_META_CLASS;
		}
	    }
	  else
	    {
	      return NULL;
	    }


	  if (flat)
	    {
	      func->data_type->info.data_type.virt_object = flat->info.name.virt_object;
	      func->data_type->info.data_type.virt_type_enum = flat->info.name.virt_type_enum;
	    }

	  if (how == HIDDEN_CLASSOID_NAME)
	    {
	      func->info.function.hidden_column = 1;
	    }

	  node = func;		/* return the function */
	}
      else
	{
	  node = oid;		/* return the oid */
	}
    }
  else
    {
      node = mq_oid (parser, spec);
    }

  return node;
}


/*
 * pt_add_oid_to_select_list () - augment a statement's select_list
 *                                to select the oid
 *   return:  none
 *   parser(in): the parser context used to derive statement
 *   statement(in/out): a SELECT/UNION/DIFFERENCE/INTERSECTION statement
 *   how(in):
 */

static PT_NODE *
pt_add_oid_to_select_list (PARSER_CONTEXT * parser, PT_NODE * statement, VIEW_HANDLING how)
{
  PT_NODE *oid, *from;

  if (!statement)
    {
      return NULL;
    }

  if (PT_IS_QUERY_NODE_TYPE (statement->node_type))
    {
      PT_NODE *p, *ord;

      /*
       * It would be nice to make this adjustment more automatic by
       * actually counting the number of "invisible" columns and keeping a
       * running adjustment bias, but right now there doesn't seem to be a
       * way of distinguishing invisible columns from ordinary ones, short
       * of just knowing which style of adjustment is being made (single
       * oid or multiple oids).  If a new style is added, this code will
       * need to be extended.
       */
      p = statement->info.query.order_by;
      while (p)
	{
	  ord = p->info.sort_spec.expr;
	  assert (ord->node_type == PT_VALUE);

	  /* adjust value */
	  p->info.sort_spec.pos_descr.pos_no += 1;
	  p->info.sort_spec.expr->info.value.data_value.i += 1;
	  p->info.sort_spec.expr->info.value.db_value.data.i += 1;

	  /* not needed any more */
	  p->info.sort_spec.expr->info.value.text = NULL;

	  p = p->next;

	}
    }

  if (statement->node_type == PT_SELECT)
    {
      /* value query doesn't have oid attr */
      if (!PT_IS_VALUE_QUERY (statement))
	{
	  statement->info.query.oids_included = DB_ROW_OIDS;
	  from = statement->info.query.q.select.from;
	  if (from && from->node_type == PT_SPEC)
	    {
	      oid = pt_spec_to_oid_attr (parser, from, how);
	      if (oid)
		{
		  /* prepend oid to the statement's select_list */
		  oid->next = statement->info.query.q.select.list;
		  statement->info.query.q.select.list = oid;
		}
	    }
	}
    }
  else if (statement->node_type == PT_UNION || statement->node_type == PT_INTERSECTION
	   || statement->node_type == PT_DIFFERENCE)
    {

      statement->info.query.oids_included = DB_ROW_OIDS;
      statement->info.query.q.union_.arg1 =
	pt_add_oid_to_select_list (parser, statement->info.query.q.union_.arg1, how);
      statement->info.query.q.union_.arg2 =
	pt_add_oid_to_select_list (parser, statement->info.query.q.union_.arg2, how);

      if (statement->info.query.q.union_.select_list != NULL)
	{
	  /* after adding oid, we need to get select_list again */
	  parser_free_tree (parser, statement->info.query.q.union_.select_list);
	  statement->info.query.q.union_.select_list = NULL;

	  (void) pt_get_select_list (parser, statement);
	}
    }

  return statement;
}

/*
 * pt_add_row_oid () - augment a statement's select_list to select the row oid
 *   return:  none
 *   parser(in): the parser context used to derive statement
 *   statement(in/out): a SELECT/UNION/DIFFERENCE/INTERSECTION statement
 */

PT_NODE *
pt_add_row_oid (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  return pt_add_oid_to_select_list (parser, statement, VIEW_OID);
}

/*
 * pt_add_row_oid_name () - augment a statement's select_list to
 * 			    select the row oid
 *   return:  none
 *   parser(in): the parser context used to derive statement
 *   statement(in/out): a SELECT/UNION/DIFFERENCE/INTERSECTION statement
 */

PT_NODE *
pt_add_row_oid_name (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  return pt_add_oid_to_select_list (parser, statement, OID_NAME);
}

/*
 * pt_add_row_classoid_name () - augment a statement's select_list
 * 				 to select the row oid
 *   return:  none
 *   parser(in): the parser context used to derive statement
 *   statement(in/out): a SELECT/UNION/DIFFERENCE/INTERSECTION statement
 */

PT_NODE *
pt_add_row_classoid_name (PARSER_CONTEXT * parser, PT_NODE * statement, int server_op)
{
  if (server_op)
    {
      return pt_add_oid_to_select_list (parser, statement, CLASSOID_NAME);
    }
  else
    {
      return pt_add_oid_to_select_list (parser, statement, HIDDEN_CLASSOID_NAME);
    }
}

/*
 * pt_compile () - Semantic check and convert parse tree into executable form
 *   return:
 *   parser(in):
 *   statement(in/out):
 */
PT_NODE *
pt_compile (PARSER_CONTEXT * parser, PT_NODE * volatile statement)
{
  PT_NODE *next;

  PT_SET_JMP_ENV (parser);

  if (statement)
    {
      next = statement->next;
      statement->next = NULL;

      statement = pt_semantic_check (parser, statement);

      /* restore link */
      if (statement)
	{
	  statement->next = next;
	}
    }

  PT_CLEAR_JMP_ENV (parser);

  return statement;
}


/*
 * pt_class_pre_fetch () - minimize potential deadlocks by prefetching
 *      the classes the statement will need in the correct lock mode
 *   return:
 *   parser(in):
 *   statement(in):
 *
 * Note :
 * This routine will not avoid deadlock altogether because classes which
 * are implicitly accessed via path expressions are not know at this time.
 * We are assured however that these implicit classes will be read only
 * classes and will not need lock escalation.  These implicit classes
 * will be locked during semantic analysis.
 *
 * This routine will only prefetch for the following statement types:
 * UPDATE, DELETE, INSERT, SELECT, UNION, DIFFERENCE, INTERSECTION.
 *
 * There are two types of errors:
 * 1) lock timeout.  In this case, we set an error and return.
 * 2) unknown class.  In this case, the user has a semantic error.
 *    We need to continue with semantic analysis so that the proper
 *    error msg will be returned.  Thus WE DO NOT set an error for this case.
 */

PT_NODE *
pt_class_pre_fetch (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_CLASS_LOCKS lcks;
  PT_NODE *node = NULL;
  LOCK lock_rr_tran = NULL_LOCK;
  LC_FIND_CLASSNAME find_result;

  lcks.classes = NULL;
  lcks.only_all = NULL;
  lcks.locks = NULL;
  lcks.flags = NULL;

  /* we don't pre fetch for non query statements */
  if (statement == NULL)
    {
      return NULL;
    }

  switch (statement->node_type)
    {
    case PT_DELETE:
    case PT_INSERT:
    case PT_UPDATE:
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_MERGE:
      if (TM_TRAN_ISOLATION () >= TRAN_REPEATABLE_READ && TM_TRAN_REP_READ_LOCK () == NULL_LOCK)
	{
	  lock_rr_tran = S_LOCK;
	}
      break;
    default:
      if (TM_TRAN_ISOLATION () >= TRAN_REPEATABLE_READ)
	{
	  if (TM_TRAN_REP_READ_LOCK () == NULL_LOCK)
	    {
	      lock_rr_tran = S_LOCK;
	    }
	  if (statement->node_type == PT_ALTER && statement->info.alter.code == PT_ADD_ATTR_MTHD)
	    {
	      for (node = statement->info.alter.alter_clause.attr_mthd.attr_def_list; node; node = node->next)
		{
		  if (node->info.attr_def.constrain_not_null)
		    {
		      lock_rr_tran = X_LOCK;
		      break;
		    }
		}
	    }
	  if (tran_lock_rep_read (lock_rr_tran) != NO_ERROR)
	    {
	      return NULL;
	    }
	}

      if (statement->node_type == PT_CREATE_ENTITY && statement->info.create_entity.entity_type == PT_CLASS)
	{
	  (void) parser_walk_tree (parser, statement, NULL, NULL, pt_resolve_cte_specs, NULL);
	}

      return statement;
    }

  /* specs referring a CTE will have an entity name, just like a normal class;
   * in order to not try and prefetch (and possibly fail) such classes, we must first resolve such specs */
  (void) parser_walk_tree (parser, statement, NULL, NULL, pt_resolve_cte_specs, NULL);

  lcks.num_classes = 0;

  /* pt_count_entities() will give us too large a count if a class is mentioned more than once, but this will not hurt
   * us. */
  (void) parser_walk_tree (parser, statement, pt_count_entities, &lcks.num_classes, NULL, NULL);

  if (lcks.num_classes == 0)
    {
      return statement;		/* caught in semantic check */
    }

  /* allocate the arrays */
  lcks.allocated_count = lcks.num_classes;
  lcks.classes = (char **) malloc ((lcks.num_classes + 1) * sizeof (char *));
  if (lcks.classes == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (char *));
      goto cleanup;
    }

  lcks.only_all = (int *) malloc (lcks.num_classes * sizeof (int));
  if (lcks.only_all == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (int));
      goto cleanup;
    }

  lcks.locks = (LOCK *) malloc (lcks.num_classes * sizeof (LOCK));
  if (lcks.locks == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (DB_FETCH_MODE));
      goto cleanup;
    }
  memset (lcks.classes, 0, (lcks.num_classes + 1) * sizeof (char *));

  lcks.flags = (LC_PREFETCH_FLAGS *) malloc (lcks.num_classes * sizeof (LC_PREFETCH_FLAGS));
  if (lcks.flags == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (DB_FETCH_MODE));
      goto cleanup;
    }
  memset (lcks.flags, 0, lcks.num_classes * sizeof (LC_PREFETCH_FLAGS));

  /* reset so parser_walk_tree can step through arrays */
  lcks.num_classes = 0;

  (void) parser_walk_tree (parser, statement, pt_find_lck_classes, &lcks, NULL, NULL);

  if (!pt_has_error (parser)
      && (find_result =
	  locator_lockhint_classes (lcks.num_classes, (const char **) lcks.classes, lcks.locks, lcks.only_all,
				    lcks.flags, true, lock_rr_tran)) != LC_CLASSNAME_EXIST)
    {
      if (find_result == LC_CLASSNAME_ERROR
	  && (er_errid () == ER_LK_UNILATERALLY_ABORTED || er_errid () == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED))
	{
	  /*
	   * Transaction has been aborted, the dirty objects and cached
	   * locks has been cleared in current client during the above
	   * locator_lockhint_classes () process. Therefore, must return from
	   * this function immediately, otherwise the released 'statement'
	   * (or other resources) will cause the unexpected problems.
	   */
	  goto cleanup;
	}

      PT_ERRORc (parser, statement, db_error_string (3));
    }

  /* free already assigned parser->lcks_classes if exist */
  parser_free_lcks_classes (parser);

  /* parser->lcks_classes will be freed at parser_free_parser() */
  parser->lcks_classes = lcks.classes;
  parser->num_lcks_classes = lcks.num_classes;
  lcks.classes = NULL;

  (void) parser_walk_tree (parser, statement, pt_set_class_chn, NULL, NULL, NULL);

cleanup:
  if (lcks.classes)
    {
      free_and_init (lcks.classes);
    }

  if (lcks.only_all)
    {
      free_and_init (lcks.only_all);
    }

  if (lcks.locks)
    {
      free_and_init (lcks.locks);
    }
  if (lcks.flags)
    {
      free_and_init (lcks.flags);
    }
  return statement;
}

/*
 * pt_set_class_chn() -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(out):
 */
static PT_NODE *
pt_set_class_chn (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *class_;
  MOP clsmop = NULL;
  int is_class = 0;

  if (node->node_type == PT_SPEC)
    {
      for (class_ = node->info.spec.flat_entity_list; class_; class_ = class_->next)
	{
	  clsmop = class_->info.name.db_object;
	  if (clsmop == NULL)
	    {
	      continue;
	    }
	  is_class = db_is_class (clsmop);
	  if (is_class < 0)
	    {
	      PT_ERRORc (parser, class_, er_msg ());
	      continue;
	    }
	  if (is_class == 0)
	    {
	      continue;
	    }

	  if (locator_flush_class (clsmop) != NO_ERROR)
	    {
	      PT_ERRORc (parser, class_, er_msg ());
	    }

	  class_->info.name.db_object_chn = locator_get_cache_coherency_number (clsmop);
	}
    }

  return node;
}

/*
 * pt_count_entities () - If the node is an entity spec, bump counter
 *   return:
 *   parser(in):
 *   node(in): the node to check, leave node unchanged
 *   arg(out): count of entities
 *   continue_walk(in):
 */
static PT_NODE *
pt_count_entities (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  int *cnt = (int *) arg;

  if (node->node_type == PT_SPEC)
    {
      (*cnt)++;
      if (node->info.spec.partition != NULL)
	{
	  (*cnt)++;
	}
    }

  return node;
}

/*
 * pt_add_lock_class () - add class locks in the prefetch structure
 *   return : error code or NO_ERROR
 *   parser (in)    : parser context
 *   lcks (in/out)  : pointer to PT_CLASS_LOCKS structure
 *   spec (in)	    : spec to add in locks list.
 *   flags(in)	    : the scope for which this class is added (either for
 *		      locking or for count optimization)
 */
int
pt_add_lock_class (PARSER_CONTEXT * parser, PT_CLASS_LOCKS * lcks, PT_NODE * spec, LC_PREFETCH_FLAGS flags)
{
  int len = 0;

  if (lcks->num_classes >= lcks->allocated_count)
    {
      /* Need to allocate more space in the locks array. Do not free locks array if memory allocation fails, it will be
       * freed by the caller of this function */
      size_t new_size = lcks->allocated_count + 1;

      /* expand classes */
      char **const realloc_ptr_classes = (char **) realloc (lcks->classes, new_size * sizeof (char *));
      if (realloc_ptr_classes == NULL)
	{
	  PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		      new_size * sizeof (char *));
	  return ER_FAILED;
	}
      lcks->classes = realloc_ptr_classes;

      /* expand only_all */
      int *const realloc_ptr_only_all = (int *) realloc (lcks->only_all, new_size * sizeof (int));
      if (realloc_ptr_only_all == NULL)
	{
	  PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY, new_size * sizeof (int));
	  return ER_FAILED;
	}
      lcks->only_all = realloc_ptr_only_all;

      /* expand locks */
      LOCK *const realloc_ptr_locks = (LOCK *) realloc (lcks->locks, new_size * sizeof (LOCK));
      if (realloc_ptr_locks == NULL)
	{
	  PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY, new_size * sizeof (LOCK));
	  return ER_FAILED;
	}
      lcks->locks = realloc_ptr_locks;

      /* flags */
      LC_PREFETCH_FLAGS *const realloc_ptr_flags
	= (LC_PREFETCH_FLAGS *) realloc (lcks->flags, new_size * sizeof (LC_PREFETCH_FLAGS));
      if (realloc_ptr_flags == NULL)
	{
	  PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY,
		      new_size * sizeof (LC_PREFETCH_FLAGS));
	  return ER_FAILED;
	}
      lcks->flags = realloc_ptr_flags;

      lcks->allocated_count++;
    }

  /* need to lowercase the class name so that the lock manager can find it. */
  len = (int) strlen (spec->info.spec.entity_name->info.name.original);
  /* parser->lcks_classes[n] will be freed at parser_free_parser() */
  lcks->classes[lcks->num_classes] = (char *) calloc (1, len + 1);
  if (lcks->classes[lcks->num_classes] == NULL)
    {
      PT_ERRORmf (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUT_OF_MEMORY, len + 1);
      return MSGCAT_RUNTIME_OUT_OF_MEMORY;
    }

  sm_downcase_name (spec->info.spec.entity_name->info.name.original, lcks->classes[lcks->num_classes], len + 1);

  if (spec->info.spec.only_all == PT_ONLY)
    {
      lcks->only_all[lcks->num_classes] = 0;
    }
  else
    {
      lcks->only_all[lcks->num_classes] = 1;
    }

  if (flags & LC_PREF_FLAG_LOCK)
    {
      lcks->locks[lcks->num_classes] = locator_fetch_mode_to_lock (lcks->lock_type, LC_CLASS, LC_FETCH_CURRENT_VERSION);
    }
  else
    {
      lcks->locks[lcks->num_classes] = NA_LOCK;
    }
  lcks->flags[lcks->num_classes] = flags;

  lcks->num_classes++;

  return NO_ERROR;
}

/*
 * pt_find_lck_classes () - identifies classes and adds an unique entry in the
 *                          prefetch structure with the SCH-S lock mode
 *   return:
 *   parser(in):
 *   node(in): the node to check, returns node unchanged
 *   arg(in/out): pointer to PT_CLASS_LOCKS structure
 *   continue_walk(in):
 */
static PT_NODE *
pt_find_lck_classes (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_CLASS_LOCKS *lcks = (PT_CLASS_LOCKS *) arg;

  lcks->lock_type = DB_FETCH_READ;

  /* Temporary disable count optimization. To enable it just restore the condition and also remove deactivation in
   * qexec_evaluate_aggregates_optimize */
  if (false /* node->node_type == PT_SELECT */ )
    {
      /* count optimization */
      PT_NODE *list = node->info.query.q.select.list;
      PT_NODE *from = node->info.query.q.select.from;

      /* Check if query is of form 'SELECT count(*) from t' */
      if (list != NULL && list->next == NULL && list->node_type == PT_FUNCTION
	  && list->info.function.function_type == PT_COUNT_STAR && from != NULL && from->next == NULL
	  && from->info.spec.entity_name != NULL && from->info.spec.entity_name->node_type == PT_NAME
	  && node->info.query.q.select.where == NULL)
	{
	  /* only add to the array, if not there already in this lock mode. */
	  if (!pt_in_lck_array (lcks, from->info.spec.entity_name->info.name.original, LC_PREF_FLAG_COUNT_OPTIM))
	    {
	      if (pt_add_lock_class (parser, lcks, from, LC_PREF_FLAG_COUNT_OPTIM) != NO_ERROR)
		{
		  *continue_walk = PT_STOP_WALK;
		  return node;
		}
	    }
	}
    }

  /* if its not an entity, there's nothing left to do */
  if (node->node_type != PT_SPEC)
    {
      /* if its not an entity, there's nothing left to do */
      return node;
    }

  /* check if this is a parenthesized entity list */
  if (!node->info.spec.entity_name || (node->info.spec.entity_name->node_type != PT_NAME))
    {
      return node;
    }

  if (node->info.spec.partition != NULL)
    {
      /* add specified lock on specified partition */
      node = pt_find_lck_class_from_partition (parser, node, lcks);
      if (node == NULL)
	{
	  *continue_walk = PT_STOP_WALK;
	  return NULL;
	}
    }
  /* only add to the array, if not there already in this lock mode. */
  if (!pt_in_lck_array (lcks, node->info.spec.entity_name->info.name.original, LC_PREF_FLAG_LOCK))
    {
      if (pt_add_lock_class (parser, lcks, node, LC_PREF_FLAG_LOCK) != NO_ERROR)
	{
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}
    }

  return node;
}

/*
 * pt_find_lck_class_from_partition - add pre-fetch locks for specified
 *				      partition
 * return: spec
 * parser (in) : parser context
 * node	  (in) : partition spec
 * locks (in)  : locks array
 */
static PT_NODE *
pt_find_lck_class_from_partition (PARSER_CONTEXT * parser, PT_NODE * node, PT_CLASS_LOCKS * locks)
{
  int error = NO_ERROR;
  const char *entity_name = NULL;
  const char *partition_name = NULL;

  if (node == NULL || node->info.spec.partition == NULL)
    {
      return node;
    }

  entity_name = node->info.spec.entity_name->info.name.original;
  partition_name = pt_partition_name (parser, entity_name, node->info.spec.partition->info.name.original);
  if (partition_name == NULL)
    {
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "allocate memory");
	  return NULL;
	}
    }

  if (!pt_in_lck_array (locks, partition_name, LC_PREF_FLAG_LOCK))
    {
      /* Set lock on specified partition. Only add to the array if not there already in this lock mode. */
      node->info.spec.entity_name->info.name.original = partition_name;
      error = pt_add_lock_class (parser, locks, node, LC_PREF_FLAG_LOCK);

      /* restore spec name */
      node->info.spec.entity_name->info.name.original = entity_name;
      if (error != NO_ERROR)
	{
	  return NULL;
	}
    }

  return node;
}

/*
 * pt_in_lck_array () -
 *   return: true if string found in array with given lockmode, false otherwise
 *   lcks(in):
 *   str(in):
 */
static int
pt_in_lck_array (PT_CLASS_LOCKS * lcks, const char *str, LC_PREFETCH_FLAGS flags)
{
  int i, no_lock_idx = -1;
  LOCK chk_lock;

  chk_lock = locator_fetch_mode_to_lock (lcks->lock_type, LC_CLASS, LC_FETCH_CURRENT_VERSION);
  for (i = 0; i < lcks->num_classes; i++)
    {
      if (intl_identifier_casecmp (str, lcks->classes[i]) == 0)
	{
	  if (flags & LC_PREF_FLAG_LOCK)
	    {
	      if (lcks->flags[i] & LC_PREF_FLAG_LOCK)
		{
		  if (lcks->locks[i] == chk_lock)
		    {
		      return true;
		    }
		}
	      else
		{
		  no_lock_idx = i;
		}
	    }
	  else if (flags & LC_PREF_FLAG_COUNT_OPTIM)
	    {
	      lcks->flags[i] = (LC_PREFETCH_FLAGS) (lcks->flags[i] | LC_PREF_FLAG_COUNT_OPTIM);
	      return true;
	    }
	}
    }
  if (no_lock_idx >= 0)
    {
      lcks->locks[no_lock_idx] = chk_lock;
      lcks->flags[no_lock_idx] = (LC_PREFETCH_FLAGS) (lcks->flags[no_lock_idx] | LC_PREF_FLAG_LOCK);
      return true;
    }

  return false;			/* not found */
}

/*
 * remove_appended_trigger_info () - remove appended trigger info
 *   msg(in/out):
 *   with_evaluate(in):
 */
static void
remove_appended_trigger_info (char *msg, int with_evaluate)
{
  size_t i;
  const char *scope_str = "SCOPE___ ";
  const char *from_on_str = " FROM ON ";
  const char *eval_prefix = "EVALUATE ( ";
  const char *eval_suffix = " ) ";
  const char single_quote_char = '\'';
  const char semicolon = ';';
  char *p = NULL;

  if (msg == NULL)
    {
      return;
    }

  if (with_evaluate)
    {
      p = strstr (msg, eval_prefix);
      if (p != NULL)
	{
	  p = (char *) memmove (p, p + strlen (eval_prefix), strlen (p) - strlen (eval_prefix) + 1);
	}

      p = strstr (msg, eval_suffix);
      if (p != NULL)
	{
	  p = (char *) memmove (p, p + strlen (eval_suffix), strlen (p) - strlen (eval_suffix) + 1);
	}
    }

  p = strstr (msg, scope_str);
  if (p != NULL)
    {
      p = (char *) memmove (p, p + strlen (scope_str), strlen (p) - strlen (scope_str) + 1);
    }

  p = strstr (msg, from_on_str);
  if (p != NULL)
    {
      for (i = 0; p[i] != single_quote_char && i < strlen (p); i++)
	;

      if (i > 0 && p[i - 1] == semicolon)
	{
	  p = (char *) memmove (p, p + i, strlen (p) - i + 1);
	}
    }
}

/*
 * pt_compile_trigger_stmt () - Compiles the trigger_stmt so that it can be
 * 	executed by pt_exec_trigger_stmt
 *   return:
 *   parser(in): the parser context
 *   trigger_stmt(in): trigger_stmt to compile
 *   class_op(in): class name to resolve name1 and name2 to
 *   name1(in): name to resolve
 *   name2(in): name to resolve
 *
 */

PT_NODE *
pt_compile_trigger_stmt (PARSER_CONTEXT * parser, const char *trigger_stmt, DB_OBJECT * class_op, const char *name1,
			 const char *name2)
{
  char *stmt_str = NULL;
  const char *class_name;
  PT_NODE **statement_p, *statement;
  int is_update_object;
  PT_NODE *err_node;
  int with_evaluate;

  assert (parser != NULL);

  if (!trigger_stmt)
    return NULL;

  /* is this an update object statement? if so it gets more complex */
  is_update_object = (intl_mbs_ncasecmp (trigger_stmt, "update object", strlen ("update object")) == 0);

  if (is_update_object)
    {
      if (class_op == NULL)
	{
	  return NULL;		/* deleted object */
	}

      /* The name that could be an argument to UPDATE OBJECT will always be the first name supplied here. Don't need to
       * initialize a label as we'll convert the PT_PARAMETER node into a PT_TRIGGER_OID later. */
      if (name1 != NULL)
	{
	  name1 = name2;
	  name2 = NULL;
	}
    }

  /* wrap a scope around the statement */
  stmt_str = pt_append_string (parser, stmt_str, "SCOPE___ ");
  stmt_str = pt_append_string (parser, stmt_str, trigger_stmt);

  class_name = NULL;
  if (class_op && name1 != NULL)
    {
      class_name = db_get_class_name (class_op);
      if (!class_name)
	{
	  return (PT_NODE *) 0;
	}

      stmt_str = pt_append_string (parser, stmt_str, " FROM ON ");
      if (name1)
	{
	  stmt_str = pt_append_string (parser, stmt_str, " [");
	  stmt_str = pt_append_string (parser, stmt_str, class_name);
	  stmt_str = pt_append_string (parser, stmt_str, "] ");
	  stmt_str = pt_append_string (parser, stmt_str, name1);
	}

      if (name2)
	{
	  if (!name1)
	    {
	      return (PT_NODE *) 0;
	    }
	  stmt_str = pt_append_string (parser, stmt_str, ", [");
	  stmt_str = pt_append_string (parser, stmt_str, class_name);
	  stmt_str = pt_append_string (parser, stmt_str, "] ");
	  stmt_str = pt_append_string (parser, stmt_str, name2);
	}
    }
  stmt_str = pt_append_string (parser, stmt_str, ";");

  statement_p = parser_parse_string_use_sys_charset (parser, stmt_str);
  if (statement_p == NULL || *statement_p == NULL)
    return NULL;
  statement = *statement_p;

  /* If this is an update object statement, setup a spec to allow binding to the :obj parameter.  This code was copied
   * from some other pt_ function, should consider making this a subroutine if it is generally useful. */
  if (is_update_object)
    {
      PT_NODE *upd, *entity, *entity_name;

      upd = statement->info.scope.stmt->info.trigger_action.expression;
      entity = parser_new_node (parser, PT_SPEC);
      entity->info.spec.id = (UINTPTR) entity;
      entity->line_number = upd->line_number;
      entity->column_number = upd->column_number;
      entity->info.spec.entity_name = parser_new_node (parser, PT_NAME);
      entity_name = entity->info.spec.entity_name;
      entity_name->info.name.spec_id = entity->info.spec.id;
      entity_name->info.name.meta_class = PT_CLASS;
      entity_name->info.name.original = (const char *) db_get_class_name (class_op);

      entity->info.spec.only_all = PT_ONLY;
      entity->info.spec.range_var = parser_copy_tree (parser, entity_name);
      if (entity->info.spec.range_var == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "parser_copy_tree");
	  return NULL;
	}

      entity->info.spec.range_var->info.name.resolved = NULL;
      upd->info.update.spec = entity;
    }

  statement = pt_compile (parser, statement);

  /* Remove those info we append, which users can't understand them */
  if (pt_has_error (parser))
    {
      err_node = pt_get_errors (parser);
      with_evaluate = strstr (trigger_stmt, "EVALUATE ( ") != NULL ? true : false;
      while (err_node)
	{
	  remove_appended_trigger_info (err_node->info.error_msg.error_message, with_evaluate);
	  err_node = err_node->next;
	}
    }

  /* We need to do view translation here on the expression to be executed. */
  if (statement)
    {
      statement->info.scope.stmt->info.trigger_action.expression =
	mq_translate (parser, statement->info.scope.stmt->info.trigger_action.expression);
      /*
       * Trigger statement node must use the datetime information of the
       * node corresponding the action to be made.
       */
      if (statement->info.scope.stmt && statement->info.scope.stmt->info.trigger_action.expression)
	{
	  statement->flag.si_datetime |= statement->info.scope.stmt->info.trigger_action.expression->flag.si_datetime;
	}
    }

  /* split the delete statement */
  if (statement != NULL && statement->info.scope.stmt->info.trigger_action.expression != NULL
      && statement->info.scope.stmt->info.trigger_action.expression->node_type == PT_DELETE)
    {
      if (pt_split_delete_stmt (parser, statement->info.scope.stmt->info.trigger_action.expression) != NO_ERROR)
	{
	  return NULL;
	}
    }

  return statement;
}


/*
 * pt_set_trigger_obj_pre () - sets pt_trigger_exec_info's object1 and object2
 *   return:
 *   parser(in):  the parser context
 *   node(in): trigger_stmt statement node
 *   arg(in): a struct pt_trigger_exec_info
 *   walk_on(in/out): flag that tells when to stop traversal
 *
 * Note :
 * Handling UPDATE OBJECT statements is a bit different from regular
 * update statements.  The "obj" argument for the update will actually
 * start in the parse tree with metaclass PT_PARAMETER and the spec_id
 * will be zero the first time we walk over the tree.  This gets changed
 * to a PT_TRIGGER_OID so at exec time we don't have to deal with setting
 * a value for this label.
 *
 * In the usual case, we just look for PT_NAME nodes with spec_id's that
 * match the spec_id's given to the correlation names in the outer scope.
 */
static PT_NODE *
pt_set_trigger_obj_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  TRIGGER_EXEC_INFO *exec_info = (TRIGGER_EXEC_INFO *) arg;


  if (exec_info->is_update_object)
    {
      /* Its an "update object" statement, the object target will initially be a PT_PARAMETER meta_class node. When we
       * find one of these, convert it to be a normal PT_TRIGGER_OID node so we avoid ambiguities in name resolution
       * and don't have to actually set a :obj label for run time evaluation. Note that since the meta_class of the
       * node changes after the first time through here, the following clause has to be smart about recognzing this in
       * the future with a spec_id of zero. */
      switch (node->node_type)
	{
	case PT_SELECT:
	  node->info.query.xasl = (void *) 0;
	  break;

	case PT_UPDATE:
	  /* this is the root of the update statement, the "object" field must be set to a non-NULL value in order for
	   * do_update() to believe that this is an update object statement. Given that, I'm not sure if it is
	   * necessary to be setting the parameter value in the object_parameter field as well but it doesn't seem to
	   * hurt. */
	  node->info.update.object = exec_info->object1;
	  break;

	case PT_NAME:
	  if (node->info.name.meta_class == PT_PARAMETER)
	    {
	      /* it could be our :obj or :new parameter, ignore spec_id, it could * be zero or could be resolved
	       * depending on where we are. this shouldn't matter, the symbolic names tell us what this is. */
	      if ((exec_info->name1 != NULL && strcmp (node->info.name.original, exec_info->name1) == 0)
		  || (exec_info->name2 != NULL && strcmp (node->info.name.original, exec_info->name2) == 0))
		{
		  /* its a parameter reference to one of our magic names */
		  node->info.name.db_object = exec_info->object1;
		  node->info.name.meta_class = PT_TRIGGER_OID;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	    }
	  else
	    {
	      /* its some other non-paremter name node */
	      if (exec_info->spec_id1 && node->info.name.spec_id == exec_info->spec_id1)
		{
		  node->info.name.db_object = exec_info->object1;
		  node->info.name.meta_class = PT_TRIGGER_OID;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	      else if (exec_info->spec_id2 && node->info.name.spec_id == exec_info->spec_id2)
		{
		  node->info.name.db_object = exec_info->object2;
		  node->info.name.meta_class = PT_TRIGGER_OID;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	      else if (node->info.name.spec_id == 0 && node->info.name.meta_class == PT_TRIGGER_OID)
		{
		  /* this was our former PT_PARAMETER node that we hacked into a PT_TRIGGER_OID above. */
		  node->info.name.db_object = exec_info->object1;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	    }
	  break;

	case PT_DOT_:
	  exec_info->path_expr_level++;
	  break;

	default:
	  break;
	}
    }
  else
    {
      /* its a "normal" update/insert/delete statement surrounded by a SCOPE___ block with one or more correlation
       * names. */
      switch (node->node_type)
	{
	case PT_SELECT:
	  node->info.query.xasl = (void *) 0;
	  break;

	case PT_NAME:
	  if (exec_info->spec_id1 && node->info.name.spec_id == exec_info->spec_id1)
	    {
	      node->info.name.db_object = exec_info->object1;
	      node->info.name.meta_class = PT_TRIGGER_OID;
	      if (exec_info->path_expr_level)
		{
		  exec_info->trig_corr_path = true;
		}
	    }
	  else if (exec_info->spec_id2 && node->info.name.spec_id == exec_info->spec_id2)
	    {
	      node->info.name.db_object = exec_info->object2;
	      node->info.name.meta_class = PT_TRIGGER_OID;
	      if (exec_info->path_expr_level)
		{
		  exec_info->trig_corr_path = true;
		}
	    }
	  break;

	case PT_DOT_:
	  exec_info->path_expr_level++;
	  break;

	default:
	  break;
	}
    }

  return node;
}

/*
 * pt_set_trigger_obj_post () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   walk_on(in):
 */
static PT_NODE *
pt_set_trigger_obj_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *temp, *next;
  TRIGGER_EXEC_INFO *exec_info = (TRIGGER_EXEC_INFO *) arg;

  if (node->node_type != PT_DOT_)
    {
      return node;
    }

  exec_info->path_expr_level--;
  if (!exec_info->path_expr_level && exec_info->trig_corr_path)
    {
      /* This is a path expression rooted in a trigger correlation variable We need to replace this path expression by
       * evaluating it */
      exec_info->trig_corr_path = false;

      /* save the next pointer or it will be lost */
      next = node->next;
      node->next = NULL;
      temp = pt_eval_value_path (parser, node);
      parser_free_tree (parser, node);
      node = temp;

      /* pt_eval_value_path can return NULL, so be careful. */
      if (node)
	{
	  node->next = next;
	}
    }

  return node;
}


/*
 * pt_exec_trigger_stmt () - Executes the trigger_stmt after setting object1
 *                           and object2.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): the parser context
 *   trigger_stmt(in): trigger_stmt to exec
 *   object1(in): object to associate with compiled name1
 *   object2(in): object to associate with compiled name2
 *   result(out): result of the trigger_stmt
 *
 * Note :
 * Special handling for the "update object" statement has made this rather
 * complex.  It would be worth considering changing the way. we parse update
 * object statements so that the target can be an correlation name
 * as well as a parameter.
 */
int
pt_exec_trigger_stmt (PARSER_CONTEXT * parser, PT_NODE * trigger_stmt, DB_OBJECT * object1, DB_OBJECT * object2,
		      DB_VALUE * result)
{
  int error = NO_ERROR;
  PT_NODE *node;
  PT_NODE *tmp_trigger = NULL;
  TRIGGER_EXEC_INFO exec_info;
  DB_VALUE **src;
  int unhide1, unhide2;
  int server_info_bits;

  assert (parser != NULL && trigger_stmt != NULL && trigger_stmt->node_type == PT_SCOPE);

  server_info_bits = 0;		/* init */

  /* set sys_date, sys_time, sys_timestamp, sys_datetime values for trigger statement. */
  if (trigger_stmt->flag.si_datetime)
    {
      server_info_bits |= SI_SYS_DATETIME;
    }

  if (trigger_stmt->flag.si_tran_id)
    {
      server_info_bits |= SI_LOCAL_TRANSACTION_ID;
    }

  /* request to the server */
  if (server_info_bits)
    {
      error = qp_get_server_info (parser, server_info_bits);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* initialize our parser_walk_tree state */
  (void) memset (&exec_info, 0, sizeof (exec_info));
  exec_info.object1 = object1;
  exec_info.object2 = object2;

  if (trigger_stmt->info.scope.stmt != NULL && trigger_stmt->info.scope.stmt->node_type == PT_TRIGGER_ACTION)
    {
      node = trigger_stmt->info.scope.stmt->info.trigger_action.expression;
      if (node != NULL && node->node_type == PT_UPDATE && node->info.update.object_parameter != NULL)
	{
	  PT_NODE *temp;

	  exec_info.is_update_object = true;

	  /* make sure the spec we created up in pt_compile_trigger_stmt gets updated to have the actual class of this
	   * instance, not sure that's really necessary. */
	  temp = node->info.update.spec->info.spec.flat_entity_list;
	  temp->info.name.db_object = db_get_class (object1);

	  /* first object will be the parameter */
	  temp = node->info.update.object_parameter;
	  exec_info.spec_id1 = 0;	/* nothing yet */
	  exec_info.name1 = temp->info.name.original;

	  /* second object will be in the scope */
	  node = trigger_stmt->info.scope.from;
	  if (node != NULL && node->info.spec.entity_name != NULL)
	    {
	      exec_info.spec_id2 = node->info.spec.entity_name->info.name.spec_id;
	      exec_info.name2 = node->info.spec.range_var->info.name.original;
	    }
	}
      else
	{
	  node = trigger_stmt->info.scope.from;
	  /* both names will be in the scope */
	  /* first name, "obj" or "new" */
	  if (node != NULL && node->info.spec.entity_name != NULL)
	    {
	      exec_info.spec_id1 = node->info.spec.entity_name->info.name.spec_id;
	      exec_info.name1 = node->info.spec.range_var->info.name.original;
	    }

	  /* second name, "obj" or "old" */
	  if (node != NULL && (node = node->next) != NULL && node->info.spec.entity_name != NULL)
	    {
	      exec_info.spec_id2 = node->info.spec.entity_name->info.name.spec_id;
	      exec_info.name2 = node->info.spec.range_var->info.name.original;
	    }
	}
    }

  /* We need to copy the trigger statement because pt_set_trigger_obj_post() may change the statement by evaluating
   * path expressions rooted by trigger correlation variable names. */
  tmp_trigger = parser_copy_tree (parser, trigger_stmt);
  if (tmp_trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  /* Due to tree copy, the spec ids are broken. Reset spec ids */
  tmp_trigger = mq_reset_ids_in_statement (parser, tmp_trigger);
  if (tmp_trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  (void) parser_walk_tree (parser, tmp_trigger->info.scope.stmt, pt_set_trigger_obj_pre, (void *) &exec_info,
			   pt_set_trigger_obj_post, (void *) &exec_info);

  unhide1 = ws_hide_new_old_trigger_obj (object1);
  unhide2 = ws_hide_new_old_trigger_obj (object2);

  error = do_scope (parser, tmp_trigger);
  if (unhide1)
    {
      ws_unhide_new_old_trigger_obj (object1);
    }
  if (unhide2)
    {
      ws_unhide_new_old_trigger_obj (object2);
    }

  /* Rather than cloning, simply transfer the contents of the DB_VALUE in the "etc" field to the user supplied
   * container. Be sure to free the "etc" value when we're done, otherwise it becomes dangling the next time we
   * evaluate this trigger expresssion. */
  src = NULL;
  if (tmp_trigger->info.scope.stmt->node_type == PT_TRIGGER_ACTION)
    {
      src = (DB_VALUE **) (&tmp_trigger->info.scope.stmt->info.trigger_action.expression->etc);
    }
  else if (tmp_trigger->info.scope.stmt->node_type == PT_EVALUATE)
    {
      src = (DB_VALUE **) (&tmp_trigger->info.scope.stmt->etc);
    }

  if (src == NULL || *src == NULL)
    {
      db_make_null (result);
    }
  else
    {
      /* transfer the contents without cloning since we're going to free the source container */
      *result = **src;

      /* free this so it doesn't become garbage the next time */
      db_make_null (*src);
      db_value_free (*src);
    }

  /* reset the parser values */
  if (trigger_stmt->flag.si_datetime)
    {
      db_make_null (&parser->sys_datetime);
      db_make_null (&parser->sys_epochtime);
    }
  if (trigger_stmt->flag.si_tran_id)
    {
      db_make_null (&parser->local_transaction_id);
    }

  parser_free_tree (parser, tmp_trigger);

  return error;
}

/*
 * pt_name_occurs_in_from_list() - counts the number of times a name
 * appears as an exposed name in a list of entity_spec's
 *   return:
 *   parser(in):
 *   name(in):
 *   from_list(in):
 */
int
pt_name_occurs_in_from_list (PARSER_CONTEXT * parser, const char *name, PT_NODE * from_list)
{
  PT_NODE *spec;
  int i = 0;

  if (!name || !from_list)
    {
      return i;
    }

  for (spec = from_list; spec != NULL; spec = spec->next)
    {
      if (spec->info.spec.range_var && spec->info.spec.range_var->info.name.original
	  && (intl_identifier_casecmp (name, spec->info.spec.range_var->info.name.original) == 0))
	{
	  i++;
	}
    }

  return i;
}
