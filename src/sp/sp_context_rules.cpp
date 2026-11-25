/*
 *
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
 * sp_context_rules.cpp - what a stored procedure may do, checked against where it was invoked
 */

#include "sp_context_rules.hpp"

#include "authenticate.h"
#include "class_object.h"
#include "db.h"
#include "error_manager.h"
#include "execute_schema.h"
#include "locator_cl.h"
#include "parser.h"
#include "schema_manager.h"
#include "virtual_object.h"
#include "work_space.h"
#include "xasl.h"
#include "xasl_stream.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

CUB_THREAD_LOCAL int sp_In_sql_depth = 0;
CUB_THREAD_LOCAL int sp_In_trigger_depth = 0;
CUB_THREAD_LOCAL int sp_Trigger_call_depth = 0;
CUB_THREAD_LOCAL bool sp_In_statement_savepoint = false;
CUB_THREAD_LOCAL bool sp_Object_lock_only = false;

enum sp_frame_type
{
  SP_FRAME_SELECT,
  SP_FRAME_DML,
  SP_FRAME_TRIGGER_ACTION,
  SP_FRAME_ROW_TRIGGER,
  SP_FRAME_STATEMENT_TRIGGER
};

struct sp_frame
{
  sp_frame_type type = SP_FRAME_DML;
  int depth = 0;
  std::vector<OID> class_oids;
  std::vector<OID> cascade_class_oids;
  std::vector<OID> hierarchy_class_oids;
};

static CUB_THREAD_LOCAL std::vector<sp_frame> sp_Frames;
static CUB_THREAD_LOCAL unsigned int sp_Frames_version = 0;

/* classes a client write was allowed for, while sp_Frames_version and sp_In_sql_depth stay */
static CUB_THREAD_LOCAL std::vector<std::pair<OID, bool>> sp_Allowed_writes;
static CUB_THREAD_LOCAL unsigned int sp_Allowed_writes_version = 0;
static CUB_THREAD_LOCAL int sp_Allowed_writes_depth = 0;

/* what an XASL writes, by its XASL_ID: the SHA-1 and the time it was stored */
#define SP_XASL_WRITES_MAX 256
using sp_xasl_writes_key = std::array<INT32, 7>;
static CUB_THREAD_LOCAL std::map<sp_xasl_writes_key, xasl_stmt_info> sp_Xasl_writes;

#if defined (SA_MODE)
CUB_THREAD_LOCAL bool sp_Sa_query_request = false;
#endif /* SA_MODE */

sp_stmt_category
sp_stmt_category_of (const PT_NODE *statement, PT_NODE_TYPE stmt_type)
{
  if (pt_is_ddl_node_type (stmt_type))
    {
      return SP_STMT_DDL;
    }

  switch (stmt_type)
    {
    case PT_COMMIT_WORK:
    case PT_ROLLBACK_WORK:
    case PT_SAVEPOINT:
    case PT_PREPARE_TO_COMMIT:
    case PT_2PC_ATTACH:
    case PT_SET_XACTION:
    case PT_SET_SYS_PARAMS:
    case PT_SET_OPT_LVL:
    case PT_SET_TRIGGER:
    case PT_SET_NAMES:
    case PT_SET_TIMEZONE:
      return SP_STMT_TCL;

    /* SET @v and SET TRACE do not change how the outer statement runs */
    case PT_SET_SESSION_VARIABLES:
    case PT_QUERY_TRACE:
      return SP_STMT_OTHER;

    case PT_METHOD_CALL:
      if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
	{
	  /* EXECUTE of a prepared method call
	   *   PREPARE s FROM 'CALL login (...) ON CLASS db_user';
	   *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'EXECUTE s'
	   */
	  return statement->info.execute.is_method_on_target ? SP_STMT_DDL : SP_STMT_OTHER;
	}
      /* a method call run directly
       *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'CALL add_user (''u'') ON CLASS db_user'
       *                         --     or EXECUTE IMMEDIATE 'CALL add_user (CLASS db_user, ''u'')'
       */
      return (statement != NULL && statement->node_type == PT_METHOD_CALL
	      && statement->info.method_call.on_call_target != NULL) ? SP_STMT_DDL : SP_STMT_OTHER;

    case PT_INSERT:
    case PT_UPDATE:
    case PT_DELETE:
    case PT_MERGE:
    case PT_EXECUTE_TRIGGER:
      return SP_STMT_DML;

    case PT_SELECT:
      if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
	{
	  /* EXECUTE of a prepared INCR
	   *   PREPARE s FROM 'SELECT INCR (cnt) FROM t2';
	   *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'EXECUTE s'
	   */
	  return statement->info.execute.is_click_counter ? SP_STMT_DML : SP_STMT_OTHER;
	}
      /* INCR run directly
       *   SELECT f () FROM t;   -- f runs SELECT INCR (cnt) FROM t2
       */
      return (statement != NULL && statement->flag.is_click_counter) ? SP_STMT_DML : SP_STMT_OTHER;

    default:
      return SP_STMT_OTHER;
    }
}

static PT_NODE *sp_collect_every_spec (PARSER_CONTEXT *parser, PT_NODE *node, void *arg, int *continue_walk);

static void
sp_add_classes_of_spec (PARSER_CONTEXT *parser, const PT_NODE *spec, std::vector<DB_OBJECT *> &classes)
{
  if (spec == NULL || spec->node_type != PT_SPEC)
    {
      return;
    }

  for (const PT_NODE *name = spec->info.spec.flat_entity_list; name != NULL; name = name->next)
    {
      if (name->node_type != PT_NAME || name->info.name.db_object == NULL)
	{
	  continue;
	}

      classes.push_back (name->info.name.db_object);
      if (db_is_vclass (name->info.name.db_object) > 0)
	{
	  PARSER_CONTEXT *view_parser = sm_virtual_queries (parser, name->info.name.db_object);

	  if (view_parser != NULL && view_parser->view_cache != NULL)
	    {
	      (void) parser_walk_tree (view_parser, view_parser->view_cache->vquery_for_query_in_gdb,
				       sp_collect_every_spec, &classes, NULL, NULL);
	    }
	}
    }
}

static PT_NODE *
sp_collect_written_spec (PARSER_CONTEXT *parser, PT_NODE *node, void *arg, int *continue_walk)
{
  std::vector<DB_OBJECT *> *written_classes = (std::vector<DB_OBJECT *> *) arg;

  switch (node->node_type)
    {
    case PT_INSERT:
      sp_add_classes_of_spec (parser, node->info.insert.spec, *written_classes);
      break;
    case PT_MERGE:
      sp_add_classes_of_spec (parser, node->info.merge.into, *written_classes);
      break;
    case PT_SPEC:
      if ((node->info.spec.flag & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE)) != 0)
	{
	  sp_add_classes_of_spec (parser, node, *written_classes);
	}
      break;
    default:
      break;
    }

  return node;
}

static PT_NODE *
sp_collect_every_spec (PARSER_CONTEXT *parser, PT_NODE *node, void *arg, int *continue_walk)
{
  if (node->node_type == PT_SPEC)
    {
      sp_add_classes_of_spec (parser, node, * (std::vector<DB_OBJECT *> *) arg);
    }

  return node;
}

static bool
sp_updates_class_attrs (const PT_NODE *statement)
{
  const PT_NODE *assignment = statement->info.update.assignment;
  const PT_NODE *lhs = (assignment != NULL && assignment->node_type == PT_EXPR) ? assignment->info.expr.arg1 : NULL;

  if (lhs != NULL && PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }

  return lhs != NULL && lhs->node_type == PT_NAME && lhs->info.name.meta_class == PT_META_ATTR;
}

bool
sp_stmt_may_cascade (const PT_NODE *statement)
{
  if (statement->node_type == PT_SCOPE && statement->info.scope.stmt != NULL
      && statement->info.scope.stmt->node_type == PT_TRIGGER_ACTION
      && statement->info.scope.stmt->info.trigger_action.expression != NULL)
    {
      return sp_stmt_may_cascade (statement->info.scope.stmt->info.trigger_action.expression);
    }

  if (statement->node_type == PT_INSERT)
    {
      return statement->info.insert.do_replace || statement->info.insert.odku_assignments != NULL;
    }

  if (statement->node_type == PT_UPDATE && sp_updates_class_attrs (statement))
    {
      return false;
    }

  if (statement->node_type == PT_MERGE)
    {
      return statement->info.merge.update.assignment != NULL
	     && ! (statement->info.merge.flags & PT_MERGE_INFO_INSERT_ONLY);
    }

  return true;
}

static bool
sp_oid_list_contains (const std::vector<OID> &class_oids, const OID *oid)
{
  for (const OID &class_oid : class_oids)
    {
      if (OID_EQ (&class_oid, oid))
	{
	  return true;
	}
    }

  return false;
}

/*
 * sp_table_oid () - the class a class stands for in the rules: the partitioned table for a partition, or itself
 *   return: the class
 *   class_oid(in): a class
 */
static const OID *
sp_table_oid (const OID *class_oid)
{
  DB_OBJECT *class_mop = ws_mop (class_oid, NULL);
  DB_OBJECT *root = NULL;

  if (class_mop == NULL)
    {
      return class_oid;
    }

  if (do_get_partition_parent (class_mop, &root) != NO_ERROR)
    {
      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	{
	  er_clear ();
	}
      return class_oid;
    }

  return (root != NULL) ? ws_oid (root) : class_oid;
}

static void
sp_oid_list_add (const OID *oid, std::vector<OID> &class_oids)
{
  if (oid != NULL && !OID_ISNULL (oid))
    {
      oid = sp_table_oid (oid);
    }

  if (oid != NULL && !OID_ISNULL (oid) && !sp_oid_list_contains (class_oids, oid))
    {
      class_oids.push_back (*oid);
    }
}

static void
sp_oid_list_add_classes (const std::vector<DB_OBJECT *> &classes, std::vector<OID> &class_oids)
{
  for (DB_OBJECT *class_mop : classes)
    {
      sp_oid_list_add (ws_oid (class_mop), class_oids);
    }
}

/*
 * sp_skip_deleted_class () - after a failure on an object or its class, go on when it was deleted
 *   return: NO_ERROR when it was deleted, otherwise the error set
 */
static int
sp_skip_deleted_class (void)
{
  int error = er_errid ();

  if (error == ER_HEAP_UNKNOWN_OBJECT)
    {
      er_clear ();
      return NO_ERROR;
    }

  if (error == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * sp_fetch_class () - fetch the schema of a class
 *   return: NO_ERROR, or the error set
 *   class_mop(in): the class
 *   class_(out): its schema
 */
static int
sp_fetch_class (DB_OBJECT *class_mop, SM_CLASS **class_)
{
  int error = au_fetch_class_force (class_mop, class_, AU_FETCH_READ);

  if (error == NO_ERROR && *class_ == NULL)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

/*
 * sp_add_affected_classes () - add a class that is written and every class the write reaches
 *   return: NO_ERROR, or the error set
 *   class_mop(in): a class that is written
 *   cascades(in): whether the write deletes or updates rows, so that a foreign key action reaches the referring
 *                 classes
 *   class_oids(in/out): the classes, each once
 */
static int
sp_add_affected_classes (DB_OBJECT *class_mop, bool cascades, std::vector<OID> &class_oids)
{
  DB_OBJECT *root = NULL;
  SM_CLASS *class_ = NULL;
  int error;

  error = do_get_partition_parent (class_mop, &root);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (root != NULL)
    {
      class_mop = root;
    }

  const OID *oid = ws_oid (class_mop);
  if (oid == NULL)
    {
      return NO_ERROR;
    }
  if (sp_oid_list_contains (class_oids, oid))
    {
      return NO_ERROR;
    }
  class_oids.push_back (*oid);

  error = sp_fetch_class (class_mop, &class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!cascades)
    {
      return NO_ERROR;
    }

  SM_CLASS_CONSTRAINT *pk = classobj_find_cons_primary_key (class_->constraints);
  if (pk == NULL)
    {
      return NO_ERROR;
    }

  for (SM_FOREIGN_KEY_INFO *fk = pk->fk_info; fk != NULL; fk = fk->next)
    {
      if (fk->delete_action != SM_FOREIGN_KEY_CASCADE && fk->delete_action != SM_FOREIGN_KEY_SET_NULL
	  && fk->update_action != SM_FOREIGN_KEY_CASCADE && fk->update_action != SM_FOREIGN_KEY_SET_NULL)
	{
	  continue;
	}

      DB_OBJECT *child = ws_mop (&fk->self_oid, NULL);

      error = (child != NULL) ? sp_add_affected_classes (child, true, class_oids) : ER_FAILED;
      if (error != NO_ERROR && (error = sp_skip_deleted_class ()) != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * sp_add_subclasses () - add the subclasses of a class, at every level, except the partitions of a partitioned table
 *   return: NO_ERROR, or the error set
 *   class_mop(in): the class
 *   class_oids(in/out): the classes, each once
 */
static int
sp_add_subclasses (DB_OBJECT *class_mop, std::vector<OID> &class_oids)
{
  SM_CLASS *class_ = NULL;
  int error = sp_fetch_class (class_mop, &class_);

  if (error != NO_ERROR)
    {
      return error;
    }

  if (class_->partition != NULL)
    {
      return NO_ERROR;
    }

  for (DB_OBJLIST *user = class_->users; user != NULL; user = user->next)
    {
      const OID *oid = ws_oid (user->op);
      if (oid == NULL || sp_oid_list_contains (class_oids, oid))
	{
	  continue;
	}
      class_oids.push_back (*oid);
      error = sp_add_subclasses (user->op, class_oids);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

static int
sp_collect_frame_classes (sp_frame &frame)
{
  for (const OID &class_oid : frame.hierarchy_class_oids)
    {
      std::vector<OID> subclasses;
      DB_OBJECT *class_mop = ws_mop (&class_oid, NULL);
      int error = (class_mop != NULL) ? sp_add_subclasses (class_mop, subclasses) : ER_FAILED;

      if (error != NO_ERROR && (error = sp_skip_deleted_class ()) != NO_ERROR)
	{
	  return error;
	}
      const bool cascades = sp_oid_list_contains (frame.cascade_class_oids, &class_oid);
      for (const OID &subclass_oid : subclasses)
	{
	  sp_oid_list_add (&subclass_oid, frame.class_oids);
	  if (cascades)
	    {
	      sp_oid_list_add (&subclass_oid, frame.cascade_class_oids);
	    }
	}
    }
  frame.hierarchy_class_oids.clear ();

  for (const OID &write_oid : frame.cascade_class_oids)
    {
      std::vector<OID> affected;
      DB_OBJECT *class_mop = ws_mop (&write_oid, NULL);
      int error = (class_mop != NULL) ? sp_add_affected_classes (class_mop, true, affected) : ER_FAILED;

      if (error != NO_ERROR && (error = sp_skip_deleted_class ()) != NO_ERROR)
	{
	  return error;
	}
      for (const OID &class_oid : affected)
	{
	  sp_oid_list_add (&class_oid, frame.class_oids);
	}
    }
  frame.cascade_class_oids.clear ();

  return NO_ERROR;
}

static bool
sp_frame_is_trigger (const sp_frame &frame)
{
  return frame.type == SP_FRAME_ROW_TRIGGER || frame.type == SP_FRAME_STATEMENT_TRIGGER;
}

/*
 * sp_in_select_frame () - whether a statement running below the procedure is a query
 *   return: true when it is
 *
 * Note: At a depth, the frame pushed first is the statement the callback came from. The frames above it at that
 *       depth are parts of the same statement, such as the SELECT of an INSERT ... SELECT the client runs, until a
 *       trigger frame starts another statement.
 */
static bool
sp_in_select_frame (void)
{
  int checked_stmt_depth = -1;

  for (const sp_frame &frame : sp_Frames)
    {
      if (sp_frame_is_trigger (frame) || frame.type == SP_FRAME_TRIGGER_ACTION)
	{
	  checked_stmt_depth = -1;
	  continue;
	}
      if (frame.depth >= sp_In_sql_depth || frame.depth == checked_stmt_depth)
	{
	  continue;
	}
      checked_stmt_depth = frame.depth;
      if (frame.type == SP_FRAME_SELECT)
	{
	  return true;
	}
    }

  return false;
}

static int
sp_set_dml_in_query_error (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DML_IN_QUERY, 0);
  return ER_SP_DML_IN_QUERY;
}

static int
sp_set_stmt_table_error (const OID *class_oid)
{
  DB_OBJECT *class_mop = (class_oid != NULL) ? ws_mop (class_oid, NULL) : NULL;
  const char *class_name = (class_mop != NULL) ? db_get_class_name (class_mop) : NULL;

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_REFERENCED, 1, (class_name != NULL) ? class_name : "");
  return ER_SP_TABLE_REFERENCED;
}

/*
 * sp_check_writes () - rules 2 and 3 for a write a procedure invoked inside a SQL statement is about to do: the
 *			 writes of a statement it runs, or of an object it writes
 *   return: NO_ERROR, or the error set
 *   write_class_oids(in): the classes written
 *   cascades(in): whether the write updates or deletes rows
 */
static int
sp_check_writes (const std::vector<OID> &write_class_oids, bool cascades)
{
  std::vector<OID> affected;

  if (write_class_oids.empty ())
    {
      return NO_ERROR;
    }

  assert ((!sp_Frames.empty () && sp_Frames.front ().depth < sp_In_sql_depth) || db_Disable_modifications > 0);
  if (sp_Frames.empty () || sp_Frames.front ().depth >= sp_In_sql_depth)
    {
      return NO_ERROR;
    }

  if (sp_in_select_frame ())
    {
      return sp_set_dml_in_query_error ();
    }

  for (const OID &class_oid : write_class_oids)
    {
      DB_OBJECT *class_mop = ws_mop (&class_oid, NULL);
      int error = (class_mop != NULL) ? sp_add_affected_classes (class_mop, cascades, affected) : ER_FAILED;

      if (error != NO_ERROR && (error = sp_skip_deleted_class ()) != NO_ERROR)
	{
	  return error;
	}
    }

  /* A STATEMENT trigger runs before or after the statement that fired it, not on its rows, so the frames below it at
   * its depth, down to the next trigger frame, are left out. */
  std::vector<int> statement_trigger_depths;

  for (auto it = sp_Frames.rbegin (); it != sp_Frames.rend (); ++it)
    {
      sp_frame &frame = *it;

      if (frame.depth >= sp_In_sql_depth)
	{
	  continue;
	}
      auto skipped = std::find (statement_trigger_depths.begin (), statement_trigger_depths.end (), frame.depth);

      if (frame.type == SP_FRAME_STATEMENT_TRIGGER)
	{
	  if (skipped == statement_trigger_depths.end ())
	    {
	      statement_trigger_depths.push_back (frame.depth);
	    }
	}
      else if (skipped != statement_trigger_depths.end ())
	{
	  if (!sp_frame_is_trigger (frame))
	    {
	      continue;
	    }
	  statement_trigger_depths.erase (skipped);
	}
      int error = sp_collect_frame_classes (frame);
      if (error != NO_ERROR)
	{
	  return error;
	}
      for (const OID &class_oid : frame.class_oids)
	{
	  if (sp_oid_list_contains (affected, &class_oid))
	    {
	      return sp_set_stmt_table_error (&class_oid);
	    }
	}
    }

  return NO_ERROR;
}

static const char *
sp_stmt_entry_name (sp_stmt_category stmt_category, int stmt_type)
{
  switch (stmt_type)
    {
    case PT_COMMIT_WORK:
      return "COMMIT";
    case PT_ROLLBACK_WORK:
      return "ROLLBACK";
    case PT_SAVEPOINT:
      return "SAVEPOINT";
    case PT_PREPARE_TO_COMMIT:
      return "PREPARE TO COMMIT";
    case PT_2PC_ATTACH:
      return "ATTACH";
    case PT_SET_XACTION:
      return "SET TRANSACTION";
    case PT_SET_SYS_PARAMS:
      return "SET SYSTEM PARAMETERS";
    case PT_SET_OPT_LVL:
      return "SET OPTIMIZATION";
    case PT_SET_TRIGGER:
      return "SET TRIGGER";
    case PT_SET_NAMES:
      return "SET NAMES";
    case PT_SET_TIMEZONE:
      return "SET TIME ZONE";
    case PT_METHOD_CALL:
      return "CALL ... ON CLASS";
    default:
      return stmt_category == SP_STMT_DDL ? "DDL" : "the statement";
    }
}

/*
 * sp_check_statement_allowed () - apply rules 1 and 2 to a statement a stored procedure or a C method asked for
 *   return: NO_ERROR, or the error set
 *   stmt_category(in): category of the statement
 *   stmt_type(in): the value db_get_statement_type () returns for the statement, for the error message
 *
 * Note: Rule 1 reports the trigger for anything run while a trigger runs. Rule 3 is checked where the writes are
 *       about to be done: sp_check_xasl_allowed () for an XASL and sp_check_object_write () for an object.
 */
int
sp_check_statement_allowed (sp_stmt_category stmt_category, int stmt_type)
{
  const bool in_sql = (sp_In_sql_depth > 0);

  if (stmt_category == SP_STMT_TCL || stmt_category == SP_STMT_DDL)
    {
      if (sp_In_trigger_depth > 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TCL_DDL_IN_TRIGGER, 1,
		  sp_stmt_entry_name (stmt_category, stmt_type));
	  return ER_SP_TCL_DDL_IN_TRIGGER;
	}

      if (in_sql)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TCL_DDL_IN_QUERY, 1,
		  sp_stmt_entry_name (stmt_category, stmt_type));
	  return ER_SP_TCL_DDL_IN_QUERY;
	}

      return NO_ERROR;
    }

  if (stmt_category == SP_STMT_DML && in_sql && sp_in_select_frame ())
    {
      return sp_set_dml_in_query_error ();
    }

  return NO_ERROR;
}

static void
sp_drop_stale_allowed_writes (void)
{
  if (sp_Allowed_writes_version != sp_Frames_version || sp_Allowed_writes_depth != sp_In_sql_depth)
    {
      sp_Allowed_writes.clear ();
      sp_Allowed_writes_version = sp_Frames_version;
      sp_Allowed_writes_depth = sp_In_sql_depth;
    }
}

static bool
sp_allowed_write_known (const OID *class_oid, bool cascades)
{
  for (const std::pair<OID, bool> &allowed : sp_Allowed_writes)
    {
      if (OID_EQ (&allowed.first, class_oid) && (allowed.second || !cascades))
	{
	  return true;
	}
    }

  return false;
}

/*
 * sp_check_object_write () - rules 2 and 3 for an object a procedure inside a SQL statement is about to write
 *   return: NO_ERROR, or the error set
 *   object(in): an instance it is about to update, delete or lock for write, or a class whose instance it is about
 *               to insert or whose class attributes it is about to write or lock for write
 */
int
sp_check_object_write (DB_OBJECT *object)
{
  if (sp_In_sql_depth <= 0 || object == NULL)
    {
      return NO_ERROR;
    }

  assert ((!sp_Frames.empty () && sp_Frames.front ().depth < sp_In_sql_depth) || db_Disable_modifications > 0);
  if (sp_Frames.empty () || sp_Frames.front ().depth >= sp_In_sql_depth)
    {
      return NO_ERROR;
    }

  DB_OBJECT *class_mop = NULL;
  bool cascades = false;
  int is_class = locator_is_class (object, DB_FETCH_READ);

  if (is_class < 0)
    {
      return sp_skip_deleted_class ();
    }
  if (is_class > 0)
    {
      class_mop = object;
    }
  else
    {
      DB_OBJECT *instance = object;

      if (object->is_vid && !vid_is_base_instance (object))
	{
	  instance = vid_get_referenced_mop (object);
	}

      class_mop = (instance != NULL) ? db_get_class (instance) : NULL;
      if (class_mop == NULL)
	{
	  return sp_skip_deleted_class ();
	}

      cascades = !OID_ISTEMP (ws_oid (instance)) && !sp_Object_lock_only;
    }

  const OID *class_oid = ws_oid (class_mop);

  if (class_oid == NULL)
    {
      return NO_ERROR;
    }

  sp_drop_stale_allowed_writes ();
  if (sp_allowed_write_known (class_oid, cascades))
    {
      return NO_ERROR;
    }

  std::vector<OID> write_class_oids (1, *class_oid);
  int error = sp_check_writes (write_class_oids, cascades);

  if (error == NO_ERROR)
    {
      sp_Allowed_writes.emplace_back (*class_oid, cascades);
    }

  return error;
}

int
sp_check_xasl_allowed (const xasl_stmt_info &stmt_info)
{
  if (sp_In_sql_depth <= 0)
    {
      return NO_ERROR;
    }

  return sp_check_writes (stmt_info.write_class_oids, (stmt_info.stmt_flags & XASL_STMT_WRITE_CASCADES) != 0);
}

void
sp_push_xasl_frame (const xasl_stmt_info &stmt_info)
{
  sp_Frames_version++;
  sp_Frames.emplace_back ();

  sp_frame &frame = sp_Frames.back ();
  frame.type = (stmt_info.write_class_oids.empty () || (stmt_info.stmt_flags & XASL_STMT_FOR_UPDATE) != 0)
	       ? SP_FRAME_SELECT : SP_FRAME_DML;
  frame.depth = sp_In_sql_depth;
  for (const OID &class_oid : stmt_info.class_oids)
    {
      sp_oid_list_add (&class_oid, frame.class_oids);
    }
  for (const OID &class_oid : stmt_info.write_class_oids)
    {
      sp_oid_list_add (&class_oid, frame.class_oids);
      if ((stmt_info.stmt_flags & XASL_STMT_WRITE_CASCADES) != 0)
	{
	  sp_oid_list_add (&class_oid, frame.cascade_class_oids);
	}
    }
}

void
sp_push_tree_frame (PARSER_CONTEXT *parser, PT_NODE *statement, bool is_trigger_action)
{
  sp_Frames_version++;
  sp_Frames.emplace_back ();

  sp_frame &frame = sp_Frames.back ();
  frame.type = is_trigger_action ? SP_FRAME_TRIGGER_ACTION : SP_FRAME_DML;
  frame.depth = sp_In_sql_depth;

  std::vector<DB_OBJECT *> classes;
  std::vector<DB_OBJECT *> written_classes;

  (void) parser_walk_tree (parser, statement, sp_collect_every_spec, &classes, NULL, NULL);
  (void) parser_walk_tree (parser, statement, sp_collect_written_spec, &written_classes, NULL, NULL);
  sp_oid_list_add_classes (classes, frame.class_oids);
  if (sp_stmt_may_cascade (statement))
    {
      sp_oid_list_add_classes (written_classes, frame.cascade_class_oids);
    }
}

void
sp_push_class_frame (DB_OBJECT *class_mop, bool cascades)
{
  sp_Frames_version++;
  sp_Frames.emplace_back ();

  sp_frame &frame = sp_Frames.back ();
  frame.type = (class_mop == NULL) ? SP_FRAME_STATEMENT_TRIGGER : SP_FRAME_ROW_TRIGGER;
  frame.depth = sp_In_sql_depth;
  if (class_mop != NULL)
    {
      sp_oid_list_add (ws_oid (class_mop), frame.class_oids);
      sp_oid_list_add (ws_oid (class_mop), frame.hierarchy_class_oids);
      if (cascades)
	{
	  sp_oid_list_add (ws_oid (class_mop), frame.cascade_class_oids);
	}
    }
}

void
sp_pop_frame (void)
{
  assert (!sp_Frames.empty ());
  sp_Frames_version++;
  if (!sp_Frames.empty ())
    {
      sp_Frames.pop_back ();
    }
}

static sp_xasl_writes_key
sp_xasl_writes_key_of (const XASL_ID *xasl_id)
{
  return sp_xasl_writes_key {{
      xasl_id->sha1.h[0], xasl_id->sha1.h[1], xasl_id->sha1.h[2], xasl_id->sha1.h[3], xasl_id->sha1.h[4],
      xasl_id->time_stored.sec, xasl_id->time_stored.usec
    }
  };
}

bool
sp_find_xasl_writes (const XASL_ID *xasl_id, xasl_stmt_info &stmt_info)
{
  auto found = sp_Xasl_writes.find (sp_xasl_writes_key_of (xasl_id));

  if (found == sp_Xasl_writes.end ())
    {
      return false;
    }

  stmt_info = found->second;
  return true;
}

void
sp_remember_xasl_writes (const XASL_ID *xasl_id, const xasl_stmt_info &stmt_info)
{
  if (sp_Xasl_writes.size () >= SP_XASL_WRITES_MAX)
    {
      sp_Xasl_writes.clear ();
    }

  sp_Xasl_writes[sp_xasl_writes_key_of (xasl_id)] = stmt_info;
}

bool
sp_statement_needs_savepoint (const PT_NODE *statement)
{
  if (statement == NULL || !statement->flag.has_sp_call || sp_In_statement_savepoint
      || (sp_In_sql_depth > 0 && sp_In_sql_depth != sp_Trigger_call_depth))
    {
      return false;
    }

  switch (statement->node_type)
    {
    case PT_INSERT:
    case PT_UPDATE:
    case PT_DELETE:
      return true;
    default:
      return false;
    }
}

void
sp_reset_context (void)
{
  sp_Frames.clear ();
  sp_Frames_version++;
  sp_Allowed_writes.clear ();
  sp_Xasl_writes.clear ();
  sp_In_sql_depth = 0;
  sp_In_trigger_depth = 0;
  sp_Trigger_call_depth = 0;
  sp_In_statement_savepoint = false;
  sp_Object_lock_only = false;
#if defined (SA_MODE)
  sp_Sa_query_request = false;
#endif /* SA_MODE */
}
