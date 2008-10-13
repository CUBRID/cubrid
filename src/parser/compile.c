/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * comile.c - compile parse tree into executable form
 *
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "dbi.h"
#include "parser.h"
#include "semantic_check.h"
#include "locator_cl.h"
#include "memory_manager_2.h"
#include "schema_manager_3.h"
#include "msgexec.h"
#include "view_transform_1.h"
#include "view_transform_2.h"
#include "intl.h"
#include "server.h"
#include "network_interface_sky.h"
#include "execute_statement_11.h"

/* this must be the last header file included!!! */
#include "dbval.h"

typedef enum view_handling
{
  OID_NAME,
  VIEW_OID,
  CLASSOID_NAME,
  HIDDEN_CLASSOID_NAME
} VIEW_HANDLING;

typedef struct trigger_exec_info TRIGGER_EXEC_INFO;
struct trigger_exec_info
{
  UINTPTR spec_id1;
  UINTPTR spec_id2;
  DB_OBJECT *object1;
  DB_OBJECT *object2;
  const char *name1;
  const char *name2;
  bool is_update_object;
  int path_expr_level;
  int trig_corr_path;		/* path expr rooted by trigger corr name */
};

/* structure used for parser_walk_tree in pt_class_pre_fetch */
typedef struct pt_class_locks PT_CLASS_LOCKS;
struct pt_class_locks
{
  int num_classes;
  DB_FETCH_MODE lock_type;
  char **classes;
  int *only_all;
  LOCK *locks;
};

enum pt_order_by_adjustment
{
  PT_ADD_ONE,
  PT_TIMES_TWO
};


static PT_NODE *pt_spec_to_oid_attr (PARSER_CONTEXT * parser, PT_NODE * spec,
				     VIEW_HANDLING how);
static PT_NODE *pt_add_oid_to_select_list (PARSER_CONTEXT * parser,
					   PT_NODE * statement,
					   VIEW_HANDLING how);
static PT_NODE *pt_count_entities (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *arg, int *continue_walk);
static PT_NODE *pt_find_lck_classes (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *arg, int *continue_walk);
static int pt_in_lck_array (PT_CLASS_LOCKS * lcks, const char *str);
static PT_NODE *pt_set_trigger_obj_pre (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
static PT_NODE *pt_set_trigger_obj_post (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *arg,
					 int *continue_walk);

/*
 * pt_spec_to_oid_attr () - Generate an oid attribute from a resolved spec.
 * 			    Can be called any time after name resolution.
 *   return:  a PT_NAME node, or a NULL
 *   parser(in): the parser context used to derive stmt
 *   spec(in/out): an entity spec. requires spec has been resolved
 *   how(in):
 */

static PT_NODE *
pt_spec_to_oid_attr (PARSER_CONTEXT * parser, PT_NODE * spec,
		     VIEW_HANDLING how)
{
  PT_NODE *node = NULL, *oid = NULL;
  PT_NODE *flat;
  PT_NODE *range;

  if (spec->info.spec.range_var == NULL)
    return NULL;

  flat = spec->info.spec.flat_entity_list;
  range = spec->info.spec.range_var;

  if (how == OID_NAME || how == CLASSOID_NAME
      || how == HIDDEN_CLASSOID_NAME || !flat ||
      (!flat->info.name.virt_object
       || mq_is_updatable (flat->info.name.virt_object)))
    {
      /* just generate an oid name, if that is what is asked for
       * or the class is not a proxy and there is no view
       * or the view is updatable
       */
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
	  oid->data_type->info.data_type.entity =
	    parser_copy_tree_list (parser, flat);
	}
      else
	{
	  return NULL;
	}

      if (flat)
	{
	  oid->data_type->info.data_type.virt_object =
	    flat->info.name.virt_object;
	  oid->data_type->info.data_type.virt_type_enum =
	    flat->info.name.virt_type_enum;
	}

      if (how == CLASSOID_NAME || how == HIDDEN_CLASSOID_NAME)
	{
	  PT_NODE *func, *tmp;

	  /* make into a class_of function with the generated OID as
	   * the argument */
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
	      func->data_type->info.data_type.entity =
		parser_copy_tree_list (parser, flat);
	      for (tmp = func->data_type->info.data_type.entity; tmp != NULL;
		   tmp = tmp->next)
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
	      func->data_type->info.data_type.virt_object =
		flat->info.name.virt_object;
	      func->data_type->info.data_type.virt_type_enum =
		flat->info.name.virt_type_enum;
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
pt_add_oid_to_select_list (PARSER_CONTEXT * parser, PT_NODE * statement,
			   VIEW_HANDLING how)
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
	  ord->info.value.data_value.i += 1;
	  p->info.sort_spec.pos_descr.pos_no += 1;
	  p = p->next;
	}
    }

  if (statement->node_type == PT_SELECT)
    {
      statement->info.query.oids_included = 1;	/* DB_ROW_OIDS */
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
  else if (statement->node_type == PT_UNION
	   || statement->node_type == PT_INTERSECTION
	   || statement->node_type == PT_DIFFERENCE)
    {

      statement->info.query.oids_included = 1;	/* DB_ROW_OIDS */
      statement->info.query.q.union_.arg1 =
	pt_add_oid_to_select_list (parser,
				   statement->info.query.q.union_.arg1, how);
      statement->info.query.q.union_.arg2 =
	pt_add_oid_to_select_list (parser,
				   statement->info.query.q.union_.arg2, how);
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
pt_add_row_classoid_name (PARSER_CONTEXT * parser,
			  PT_NODE * statement, int server_op)
{
  if (server_op)
    {
      return pt_add_oid_to_select_list (parser, statement, CLASSOID_NAME);
    }
  else
    {
      return pt_add_oid_to_select_list (parser, statement,
					HIDDEN_CLASSOID_NAME);
    }
}


/*
 * pt_compile () - Semantic check and convert parse tree into executable form
 *   return:
 *   parser(in):
 *   statement(in/out):
 */
PT_NODE *
pt_compile (PARSER_CONTEXT * parser, PT_NODE * statement)
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

  /* we don't pre fetch for non query statements */
  if (!statement)
    return NULL;
  switch (statement->node_type)
    {
    case PT_DELETE:
    case PT_INSERT:
    case PT_UPDATE:
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      break;
    default:
      return statement;
    }

  lcks.num_classes = 0;

  /* pt_count_entities() will give us too large a count if a class is
   * mentioned more than once, but this will not hurt us. */
  (void) parser_walk_tree (parser, statement, pt_count_entities,
			   &lcks.num_classes, NULL, NULL);

  if (lcks.num_classes == 0)
    {
      return statement;		/* caught in semantic check */
    }

  /* allocate the arrays */
  lcks.classes = (char **) malloc ((lcks.num_classes + 1) * sizeof (char *));
  if (lcks.classes == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (char *));
      goto cleanup;
    }

  lcks.only_all = (int *) malloc (lcks.num_classes * sizeof (int));
  if (lcks.only_all == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (int));
      goto cleanup;
    }

  lcks.locks = (LOCK *) malloc (lcks.num_classes * sizeof (LOCK));
  if (lcks.locks == NULL)
    {
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_OUT_OF_MEMORY,
		  lcks.num_classes * sizeof (DB_FETCH_MODE));
      goto cleanup;
    }

  memset (lcks.classes, 0, (lcks.num_classes + 1) * sizeof (char *));

  /* reset so parser_walk_tree can step through arrays */
  lcks.num_classes = 0;

  (void) parser_walk_tree (parser, statement, pt_find_lck_classes, &lcks,
			   NULL, NULL);

  if (!parser->error_msgs &&
      locator_lockhint_classes (lcks.num_classes,
				(const char **) lcks.classes, lcks.locks,
				lcks.only_all, true) != LC_CLASSNAME_EXIST)
    {
      /* lc_lockhint_class has set the error */
      PT_ERRORc (parser, statement, db_error_string (3));
    }

  if (parser->lcks_classes == NULL)
    {
      /* parser->lcks_classes will be freed at parser_free_parser() */
      parser->lcks_classes = lcks.classes;
      parser->num_lcks_classes = lcks.num_classes;
      lcks.classes = NULL;
    }

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
  return statement;
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
pt_count_entities (PARSER_CONTEXT * parser, PT_NODE * node,
		   void *arg, int *continue_walk)
{
  int *cnt = (int *) arg;

  if (node->node_type == PT_SPEC)
    {
      (*cnt)++;
    }

  return node;
}


/*
 * pt_find_lck_classes () - enters classes in the prefetch structure
 *                          with the correct lock mode
 *   return:
 *   parser(in):
 *   node(in): the node to check, returns node unchanged
 *   arg(in/out): pointer to PT_CLASS_LOCKS structure
 *   continue_walk(in):
 *
 * Note :
 * When encountering a statment starting node (PT_UPDATE, PT_DELETE, PT_INSERT,
 * PT_SELECT, PT_UNION, PT_DIFFERENCE, or PT_INTERSECTION) it will set
 * the correct lock mode for the first entity in the statement.
 * All other entities will be fetched in with a read lock mode.
 *
 * This will only work when the writable class is the first class encountered
 * during the walk on a PT_UPDATE, PT_DELETE, or a PT_INSERT tree.
 * If this is not the case, this routine will request the wrong locks and will
 * aggravate the deadlock situation, not help alleviate it
 */

static PT_NODE *
pt_find_lck_classes (PARSER_CONTEXT * parser, PT_NODE * node,
		     void *arg, int *continue_walk)
{
  size_t len;
  PT_CLASS_LOCKS *lcks = (PT_CLASS_LOCKS *) arg;

  /* Set up the lock type for the first class.  All others will be read
   * read only (see WARNING in function header of pt_find_lck_classes. */
  switch (node->node_type)
    {
    case PT_DELETE:
      lcks->lock_type = DB_FETCH_CLREAD_INSTWRITE;
      break;
    case PT_UPDATE:
      /* If this is actually an UPDATE OBJECT statement, we only want
       * to lock the actual instance being updated; getting a general
       * update lock inhibits concurrency.
       */
      lcks->lock_type = (node->info.update.object_parameter)
	? DB_FETCH_CLREAD_INSTWRITE : DB_FETCH_CLREAD_INSTWRITE;
      break;
    case PT_INSERT:
      lcks->lock_type = DB_FETCH_CLREAD_INSTWRITE;
      break;
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      lcks->lock_type = DB_FETCH_CLREAD_INSTREAD;
      break;
    default:
      break;
    }

  /* if its not an entity, there's nothing left to do */
  if (node->node_type != PT_SPEC)
    {
      return node;
    }

  /* check if this is a parenthesized entity list */
  if (!node->info.spec.entity_name ||
      (node->info.spec.entity_name->node_type != PT_NAME))
    {
      return node;
    }

  /* only add to the array, if not there already in this lock mode.
   * also, don't add to the array if it is a ldb class name. */
  if (!pt_in_lck_array (lcks,
			node->info.spec.entity_name->info.name.original))
    {
      /* need to lowercase the class name so that the lock manager
       * can find it. */
      len = strlen (node->info.spec.entity_name->info.name.original);
      /* parser->lcks_classes[n] will be freed at parser_free_parser() */
      lcks->classes[lcks->num_classes] = (char *) calloc (1, len + 1);
      if (lcks->classes[lcks->num_classes] == NULL)
	{
	  PT_ERROR (parser, node, er_msg ());
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      sm_downcase_name (node->info.spec.entity_name->info.name.original,
			lcks->classes[lcks->num_classes], len + 1);

      if (node->info.spec.only_all == PT_ONLY)
	{
	  lcks->only_all[lcks->num_classes] = 0;
	}
      else
	{
	  lcks->only_all[lcks->num_classes] = 1;
	}

      lcks->locks[lcks->num_classes] =
	locator_fetch_mode_to_lock (lcks->lock_type, LC_CLASS);

      /*
       * Second and subsequent classes must be read only (see warning
       * in the function header.  Set the lock type to read only for
       * the next call.
       */
      lcks->lock_type = DB_FETCH_CLREAD_INSTREAD;
      lcks->num_classes++;
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
pt_in_lck_array (PT_CLASS_LOCKS * lcks, const char *str)
{
  int i;
  LOCK chk_lock;

  chk_lock = locator_fetch_mode_to_lock (lcks->lock_type, LC_CLASS);
  for (i = 0; i < lcks->num_classes; i++)
    {
      if (intl_mbs_casecmp (str, lcks->classes[i]) == 0 &&
	  lcks->locks[i] == chk_lock)
	{
	  return true;
	}
    }

  return false;			/* not found */
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
 * Note :
 * Because of the way "update object" statements are handled,
 * we have to be careful building our scope wrapper.
 * The parameter to update object is represented as a PT_PARAMETER
 * rather than a PT_NAME, this results in an ambiguity when we try
 * to define the "obj" correlation name in the scope when there's
 * already an :obj parameter in the UPDATE OBJECT statement.
 *
 * One solution might be to change :obj from a PT_PARAMETER into
 * a more general object producing expression so the scope wrapper
 * would work, that's a rather dangerous grammar change though that
 * we'd rather not try just now.
 *
 * The alternative is to be smart about the type of statement we're
 * wrapping our scope around and only define the "obj" correlation name
 * if this isn't an update object statement.  Unfortunately, we don't
 * have a parse tree here, we've got text so we actually have to
 * guess at the kind of statement this is by examining the text.
 * One good thing is that this text will have been generated by calling
 * parser_print_tree on a valid parse tree so we don't have to deal with
 * whitespace or comments.
 */

PT_NODE *
pt_compile_trigger_stmt (PARSER_CONTEXT * parser,
			 const char *trigger_stmt,
			 DB_OBJECT * class_op,
			 const char *name1, const char *name2)
{
  char *stmt_str = NULL;
  const char *class_name;
  PT_NODE **statement_p, *statement;
  int is_update_object;

  assert (parser != NULL);

  if (!trigger_stmt)
    return NULL;

  /* is this an update object statement? if so it gets more complex */
  is_update_object = (intl_mbs_ncasecmp (trigger_stmt, "update object",
					 strlen ("update object")) == 0);

  if (is_update_object)
    {
      if (class_op == NULL)
	{
	  return NULL;		/* deleted object */
	}

      /* The name that could be an argument to UPDATE OBJECT will always
       * be the first name supplied here.
       * Don't need to initialize a label as we'll convert the PT_PARAMETER
       * node into a PT_TRIGGER_OID later.
       */
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

      stmt_str = pt_append_string (parser, stmt_str, " FROM ");
      if (name1)
	{
	  stmt_str = pt_append_string (parser, stmt_str, class_name);
	  stmt_str = pt_append_string (parser, stmt_str, " ");
	  stmt_str = pt_append_string (parser, stmt_str, name1);
	}

      if (name2)
	{
	  if (!name1)
	    {
	      return (PT_NODE *) 0;
	    }
	  stmt_str = pt_append_string (parser, stmt_str, ", ");
	  stmt_str = pt_append_string (parser, stmt_str, class_name);
	  stmt_str = pt_append_string (parser, stmt_str, " ");
	  stmt_str = pt_append_string (parser, stmt_str, name2);
	}
    }
  stmt_str = pt_append_string (parser, stmt_str, ";");

  statement_p = parser_parse_string (parser, stmt_str);
  if (statement_p == NULL || *statement_p == NULL)
    return NULL;
  statement = *statement_p;

  /* If this is an update object statement, setup a spec to allow
   * binding to the :obj parameter.  This code was copied from
   * some other pt_ function, should consider making this a subroutine
   * if it is generally useful.
   */
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
      entity_name->info.name.original =
	(const char *) db_get_class_name (class_op);

      entity->info.spec.only_all = PT_ONLY;
      entity->info.spec.range_var = parser_copy_tree (parser, entity_name);
      entity->info.spec.range_var->info.name.resolved = NULL;
      upd->info.update.spec = entity;
    }

  statement = pt_compile (parser, statement);
  /* We need to do view translation here
   * on the expression to be executed. */
  if (statement)
    {
      statement->info.scope.stmt->info.trigger_action.expression =
	mq_translate (parser,
		      statement->info.scope.stmt->info.trigger_action.
		      expression);
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
pt_set_trigger_obj_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			void *arg, int *continue_walk)
{
  TRIGGER_EXEC_INFO *exec_info = (TRIGGER_EXEC_INFO *) arg;


  if (exec_info->is_update_object)
    {
      /* Its an "update object" statement, the object target will initially be
       * a PT_PARAMETER meta_class node. When we find one of these, convert
       * it to be a normal PT_TRIGGER_OID node so we avoid ambiguities in name
       * resolution and don't have to actually set a :obj label for run time
       * evaluation. Note that since the meta_class of the node changes
       * after the first time through here, the following clause has to be
       * smart about recognzing this in the future with a spec_id of zero.
       */
      switch (node->node_type)
	{
	case PT_SELECT:
	  node->info.query.xasl = (void *) 0;
	  break;

	case PT_UPDATE:
	  /* this is the root of the update statement, the "object" field
	   * must be set to a non-NULL value in order for do_update() to
	   * believe that this is an update object statement. Given that,
	   * I'm not sure if it is necessary to be setting the parameter value
	   * in the object_parameter field as well but it doesn't seem to hurt.
	   */
	  node->info.update.object = exec_info->object1;
	  break;

	case PT_NAME:
	  if (node->info.name.meta_class == PT_PARAMETER)
	    {
	      /* it could be our :obj or :new parameter, ignore spec_id,
	       * it could * be zero or could be resolved depending on
	       * where we are. this shouldn't matter,
	       * the symbolic names tell us what this is.
	       */
	      if ((exec_info->name1 != NULL &&
		   strcmp (node->info.name.original, exec_info->name1) == 0)
		  || (exec_info->name2 != NULL
		      && strcmp (node->info.name.original,
				 exec_info->name2) == 0))
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
	      if (exec_info->spec_id1 &&
		  node->info.name.spec_id == exec_info->spec_id1)
		{
		  node->info.name.db_object = exec_info->object1;
		  node->info.name.meta_class = PT_TRIGGER_OID;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	      else if (exec_info->spec_id2 &&
		       node->info.name.spec_id == exec_info->spec_id2)
		{
		  node->info.name.db_object = exec_info->object2;
		  node->info.name.meta_class = PT_TRIGGER_OID;
		  if (exec_info->path_expr_level)
		    {
		      exec_info->trig_corr_path = true;
		    }
		}
	      else if (node->info.name.spec_id == 0 &&
		       node->info.name.meta_class == PT_TRIGGER_OID)
		{
		  /* this was our former PT_PARAMETER node that we hacked into
		   * a PT_TRIGGER_OID above. */
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
      /* its a "normal" update/insert/delete statement surrounded
       * by a SCOPE___ block with one or more correlation names. */
      switch (node->node_type)
	{
	case PT_SELECT:
	  node->info.query.xasl = (void *) 0;
	  break;

	case PT_NAME:
	  if (exec_info->spec_id1 &&
	      node->info.name.spec_id == exec_info->spec_id1)
	    {
	      node->info.name.db_object = exec_info->object1;
	      node->info.name.meta_class = PT_TRIGGER_OID;
	      if (exec_info->path_expr_level)
		{
		  exec_info->trig_corr_path = true;
		}
	    }
	  else if (exec_info->spec_id2 &&
		   node->info.name.spec_id == exec_info->spec_id2)
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
pt_set_trigger_obj_post (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *arg, int *continue_walk)
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
      /* This is a path expression rooted in a trigger correlation variable
       * We need to replace this path expression by evaluating it */
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
pt_exec_trigger_stmt (PARSER_CONTEXT * parser, PT_NODE * trigger_stmt,
		      DB_OBJECT * object1, DB_OBJECT * object2,
		      DB_VALUE * result)
{
  int error = NO_ERROR;
  PT_NODE *node;
  PT_NODE *tmp_trigger = NULL;
  TRIGGER_EXEC_INFO exec_info;
  DB_VALUE **src;
  int unhide1, unhide2;
  SERVER_INFO server_info;

  assert (parser != NULL && trigger_stmt != NULL &&
	  trigger_stmt->node_type == PT_SCOPE);

  server_info.info_bits = 0;

  /* set sys_date, sys_time, sys_timestamp values for trigger statement. */
  if (trigger_stmt->si_timestamp)
    {
      server_info.info_bits |= SI_SYS_TIMESTAMP;
      server_info.value[0] = &parser->sys_timestamp;
    }

  if (trigger_stmt->si_tran_id)
    {
      server_info.info_bits |= SI_LOCAL_TRANSACTION_ID;
      server_info.value[1] = &parser->local_transaction_id;
    }

  /* request to the server */
  if (server_info.info_bits)
    {
      (void) qp_get_server_info (&server_info);
    }

  /* initialize our parser_walk_tree state */
  (void) memset (&exec_info, 0, sizeof (exec_info));
  exec_info.object1 = object1;
  exec_info.object2 = object2;

  /* Figure out whether or not the trigger action is an "update object"
   * statement, we do things differently up in pt_set_trigger_objects
   * if it is.  pt_set_trigger_objects probably ought to do the
   * right thing without this flag.
   * If this is an update object statement, we manufacture a spec
   * on the fly to avoid having to resolve names with pt_resolve_object
   * and pt_resolve_names.  This is due to ambiguities between the
   * PT_PARAMETER node named "obj" and the SCOPE entry named "obj" which
   * happens only in an update object statement.
   */
  if (trigger_stmt->info.scope.stmt != NULL
      && trigger_stmt->info.scope.stmt->node_type == PT_TRIGGER_ACTION)
    {
      node = trigger_stmt->info.scope.stmt->info.trigger_action.expression;
      if (node != NULL && node->node_type == PT_UPDATE
	  && node->info.update.object_parameter != NULL)
	{
	  PT_NODE *temp;

	  exec_info.is_update_object = true;

	  /* make sure the spec we created up in pt_compile_trigger_stmt
	   * gets updated to have the actual class of this instance, not
	   * sure that's really necessary.
	   */
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
	      exec_info.spec_id2 =
		node->info.spec.entity_name->info.name.spec_id;
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
	      exec_info.spec_id1 =
		node->info.spec.entity_name->info.name.spec_id;
	      exec_info.name1 = node->info.spec.range_var->info.name.original;
	    }

	  /* second name, "obj" or "old" */
	  if (node != NULL && (node = node->next) != NULL
	      && node->info.spec.entity_name != NULL)
	    {
	      exec_info.spec_id2 =
		node->info.spec.entity_name->info.name.spec_id;
	      exec_info.name2 = node->info.spec.range_var->info.name.original;
	    }
	}
    }

  /* We need to copy the trigger statement because pt_set_trigger_obj_post()
   * may change the statement by evaluating path expressions rooted by
   * trigger correlation variable names.
   */
  tmp_trigger = parser_copy_tree (parser, trigger_stmt);
  if (tmp_trigger == NULL)
    {
      return er_errid ();
    }

  (void) parser_walk_tree (parser, tmp_trigger->info.scope.stmt,
			   pt_set_trigger_obj_pre, (void *) &exec_info,
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

  /* Rather than cloning, simply transfer the contents of the DB_VALUE
   * in the "etc" field to the user supplied container. Be sure to free
   * the "etc" value when we're done, otherwise it becomes dangling
   * the next time we evaluate this trigger expresssion. */
  src = NULL;
  if (tmp_trigger->info.scope.stmt->node_type == PT_TRIGGER_ACTION)
    {
      src = (DB_VALUE **) & (tmp_trigger->info.scope.stmt->info.
			     trigger_action.expression->etc);
    }
  else if (tmp_trigger->info.scope.stmt->node_type == PT_EVALUATE)
    {
      src = (DB_VALUE **) & (tmp_trigger->info.scope.stmt->etc);
    }

  if (src == NULL || *src == NULL)
    {
      db_make_null (result);
    }
  else
    {
      /* transfer the contents without cloning since we're going to
         free the source container */
      *result = **src;

      /* free this so it doesn't become garbage the next time */
      db_make_null (*src);
      db_value_free (*src);
    }

  parser_free_tree (parser, tmp_trigger);

  return error;
}
