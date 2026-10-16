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
 * sp_context_rules.cpp - what a stored procedure may do, judged by the statement or trigger that invoked it
 */

#include "sp_context_rules.hpp"

#include <cassert>

#include "db.h"
#include "error_manager.h"
#include "execute_schema.h"
#include "parser.h"

std::vector<sp_stmt_entry> sp_Stmt_stack;
std::vector<sp_trigger_ctx> sp_Trigger_stack;

/*
 * sp_class_root () - the class to compare: a partition stands for its partitioned table
 *   return: the partitioned table of a partition, or the class itself
 *   class_mop(in): a class
 *
 * Note: a PARTITION (p) spec resolves to the partition class, while the procedure names the table and a row
 *       trigger reports the table, so both sides are compared by the table. The class is already in the
 *       workspace, so this is a cache lookup.
 */
static DB_OBJECT *
sp_class_root (DB_OBJECT *class_mop)
{
  DB_OBJECT *root = NULL;

  if (do_get_partition_parent (class_mop, &root) == NO_ERROR && root != NULL)
    {
      return root;
    }

  return class_mop;
}

/*
 * sp_stmt_add_spec_classes () - append the classes behind one spec
 *   return: none
 *   spec(in): a PT_SPEC
 *   target_classes(out): classes collected
 *
 * Note: the flat entity list already holds the base class of an updatable view.
 */
static void
sp_stmt_add_spec_classes (const PT_NODE *spec, std::vector<DB_OBJECT *> &target_classes)
{
  if (spec == NULL || spec->node_type != PT_SPEC || !PT_SPEC_IS_ENTITY (spec))
    {
      return;
    }

  for (const PT_NODE *name = spec->info.spec.flat_entity_list; name != NULL; name = name->next)
    {
      if (name->node_type == PT_NAME && name->info.name.db_object != NULL)
	{
	  target_classes.push_back (sp_class_root (name->info.name.db_object));
	}
    }
}

/*
 * sp_stmt_add_written_specs () - append the classes of the specs an UPDATE or DELETE writes
 *   return: none
 *   spec_list(in): the statement's spec list, which also holds the specs that only join in
 *   written_flag(in): PT_SPEC_FLAG_UPDATE or PT_SPEC_FLAG_DELETE, the mark the parser put on the written specs
 *   target_classes(out): classes collected
 */
static void
sp_stmt_add_written_specs (const PT_NODE *spec_list, PT_SPEC_FLAG written_flag,
			   std::vector<DB_OBJECT *> &target_classes)
{
  for (const PT_NODE *spec = spec_list; spec != NULL; spec = spec->next)
    {
      if (spec->node_type == PT_SPEC && (spec->info.spec.flag & written_flag))
	{
	  sp_stmt_add_spec_classes (spec, target_classes);
	}
    }
}

/*
 * sp_stmt_is_single_row_values () - true for INSERT ... VALUES with one row and no subquery
 *   return: bool
 *   statement(in): a PT_INSERT node
 *
 * Note: Oracle exempts this form from the mutating-table rule: no row of the target has been touched yet
 *       when the function in the VALUES clause runs.
 */
static bool
sp_stmt_is_single_row_values (const PT_NODE *statement)
{
  const PT_NODE *values = statement->info.insert.value_clauses;

  return (values != NULL && values->next == NULL && values->node_type == PT_NODE_LIST
	  && values->info.node_list.list_type != PT_IS_SUBQUERY && statement->info.insert.odku_assignments == NULL
	  && !statement->info.insert.do_replace);
}

sp_stmt_guard::sp_stmt_guard (const PT_NODE *statement)
  : m_pushed (false)
{
  if (statement == NULL || (!statement->flag.has_sp_call && sp_Stmt_stack.empty () && sp_Trigger_stack.empty ()))
    {
      return;
    }

  sp_stmt_entry entry;

  switch (statement->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      entry.stmt_type = CUBRID_STMT_SELECT;
      break;

    case PT_INSERT:
      entry.stmt_type = CUBRID_STMT_INSERT;
      if (!sp_stmt_is_single_row_values (statement))
	{
	  sp_stmt_add_spec_classes (statement->info.insert.spec, entry.target_classes);
	}
      break;

    case PT_UPDATE:
      entry.stmt_type = CUBRID_STMT_UPDATE;
      sp_stmt_add_written_specs (statement->info.update.spec, PT_SPEC_FLAG_UPDATE, entry.target_classes);
      break;

    case PT_DELETE:
      entry.stmt_type = CUBRID_STMT_DELETE;
      sp_stmt_add_written_specs (statement->info.delete_.spec, PT_SPEC_FLAG_DELETE, entry.target_classes);
      break;

    case PT_MERGE:
      entry.stmt_type = CUBRID_STMT_MERGE;
      sp_stmt_add_spec_classes (statement->info.merge.into, entry.target_classes);
      break;

    default:
      /* a CALL statement or anything else that is not a query: a procedure it runs is a call alone */
      return;
    }

  sp_Stmt_stack.push_back (std::move (entry));
  m_pushed = true;
}

sp_stmt_guard::~sp_stmt_guard ()
{
  if (m_pushed)
    {
      assert (!sp_Stmt_stack.empty ());
      sp_Stmt_stack.pop_back ();
    }
}

/* the list being filled while a procedure's statement is compiled; NULL for every other compile */
static std::vector<DB_OBJECT *> *sp_Ref_tables = NULL;

void
sp_ref_tables_start (std::vector<DB_OBJECT *> *ref_tables)
{
  assert (sp_Ref_tables == NULL);
  ref_tables->clear ();
  sp_Ref_tables = ref_tables;
}

void
sp_ref_tables_stop (void)
{
  sp_Ref_tables = NULL;
}

/*
 * sp_ref_tables_add () - called by name resolution for every class a spec resolves to
 *   return: none
 *   class_mop(in): the class
 */
void
sp_ref_tables_add (DB_OBJECT *class_mop)
{
  if (sp_Ref_tables != NULL)
    {
      sp_Ref_tables->push_back (sp_class_root (class_mop));
    }
}

/*
 * sp_ref_tables_add_spec () - parser_walk_tree callback: collect the classes of an entity spec
 */
static PT_NODE *
sp_ref_tables_add_spec (PARSER_CONTEXT *parser, PT_NODE *node, void *arg, int *continue_walk)
{
  if (node->node_type == PT_SPEC)
    {
      sp_stmt_add_spec_classes (node, *sp_Ref_tables);
    }

  return node;
}

/*
 * sp_ref_tables_add_tree () - called by view translation with the query of a view the statement being compiled
 *                             reads or writes; collects every class the view's query references
 *   return: none
 *   parser(in): the parser compiling the statement
 *   tree(in): the view's query as kept in the view cache, already expanded, one node per query spec
 *
 * Note: name resolution only sees the view itself; its tables are in the cached query, which enters the
 *       statement here whether or not the cache was rebuilt during this compile.
 */
void
sp_ref_tables_add_tree (PARSER_CONTEXT *parser, PT_NODE *tree)
{
  if (sp_Ref_tables == NULL || tree == NULL)
    {
      return;
    }

  (void) parser_walk_tree (parser, tree, sp_ref_tables_add_spec, NULL, NULL, NULL);
}

void
sp_trigger_push (DB_OBJECT *class_mop, bool is_row_level)
{
  sp_Trigger_stack.push_back (sp_trigger_ctx { class_mop != NULL ? sp_class_root (class_mop) : NULL, is_row_level,
			      (int) sp_Stmt_stack.size () - 1 });
}

void
sp_trigger_pop (void)
{
  assert (!sp_Trigger_stack.empty ());
  sp_Trigger_stack.pop_back ();
}

/*
 * sp_stmt_in_statement_trigger () - whether a statement-level trigger on the class is running for the statement
 *   return: bool
 *   stmt_index(in): sp_Stmt_stack entry of the statement
 *   class_mop(in): a class the statement writes
 */
static bool
sp_stmt_in_statement_trigger (int stmt_index, DB_OBJECT *class_mop)
{
  for (const sp_trigger_ctx &trigger : sp_Trigger_stack)
    {
      if (!trigger.is_row_level && trigger.stmt_index == stmt_index && trigger.class_mop == class_mop)
	{
	  return true;
	}
    }

  return false;
}

/*
 * sp_stmt_find_mutating () - the first referenced class that an executing statement or trigger is writing
 *   return: the class, or NULL
 *   ref_tables(in): classes the stored procedure's statement references
 */
static DB_OBJECT *
sp_stmt_find_mutating (const std::vector<DB_OBJECT *> &ref_tables)
{
  for (DB_OBJECT *ref : ref_tables)
    {
      for (size_t i = 0; i < sp_Stmt_stack.size (); i++)
	{
	  for (DB_OBJECT *target : sp_Stmt_stack[i].target_classes)
	    {
	      if (target == ref && !sp_stmt_in_statement_trigger ((int) i, target))
		{
		  return ref;
		}
	    }
	}

      for (const sp_trigger_ctx &trigger : sp_Trigger_stack)
	{
	  if (trigger.is_row_level && trigger.class_mop == ref)
	    {
	      return ref;
	    }
	}
    }

  return NULL;
}

static bool
sp_stmt_has_query (void)
{
  for (const sp_stmt_entry &entry : sp_Stmt_stack)
    {
      if (entry.stmt_type == CUBRID_STMT_SELECT)
	{
	  return true;
	}
    }

  return false;
}

static const char *
sp_stmt_entry_name (int stmt_type, bool is_ddl)
{
  switch (stmt_type)
    {
    case CUBRID_STMT_COMMIT_WORK:
      return "COMMIT";
    case CUBRID_STMT_ROLLBACK_WORK:
      return "ROLLBACK";
    case CUBRID_STMT_SAVEPOINT:
      return "SAVEPOINT";
    default:
      return is_ddl ? "DDL" : "the statement";
    }
}

/*
 * sp_check_statement_allowed () - apply the Oracle purity rules to a statement a stored procedure asked for
 *   return: NO_ERROR, or the error set
 *   stmt_type(in): CUBRID_STMT_TYPE of the statement
 *   is_ddl(in): true when the statement is DDL
 *   ref_tables(in): classes the statement references (reads or writes)
 *
 * Note: the rules, in the order they are checked:
 *       1. nothing is executing (a CALL alone): allowed
 *       2. transaction control or DDL inside a statement or a trigger: rejected
 *       3. DML while a query is executing anywhere above: rejected, whatever the table
 *       4. touching a class an executing DML statement or a row-level trigger is writing: rejected
 */
int
sp_check_statement_allowed (int stmt_type, bool is_ddl, const std::vector<DB_OBJECT *> &ref_tables)
{
  const bool in_statement = !sp_Stmt_stack.empty ();
  const bool in_trigger = !sp_Trigger_stack.empty ();

  if (!in_statement && !in_trigger)
    {
      return NO_ERROR;
    }

  if (stmt_type == CUBRID_STMT_COMMIT_WORK || stmt_type == CUBRID_STMT_ROLLBACK_WORK
      || stmt_type == CUBRID_STMT_SAVEPOINT || is_ddl)
    {
      int error = in_trigger ? ER_SP_TCL_DDL_IN_TRIGGER : ER_SP_TCL_DDL_IN_QUERY;

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sp_stmt_entry_name (stmt_type, is_ddl));
      return error;
    }

  const bool is_dml = (stmt_type == CUBRID_STMT_INSERT || stmt_type == CUBRID_STMT_UPDATE
		       || stmt_type == CUBRID_STMT_DELETE || stmt_type == CUBRID_STMT_MERGE);

  if (is_dml && sp_stmt_has_query ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DML_IN_QUERY, 0);
      return ER_SP_DML_IN_QUERY;
    }

  DB_OBJECT *mutating = sp_stmt_find_mutating (ref_tables);
  if (mutating != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_MUTATING, 1, db_get_class_name (mutating));
      return ER_SP_TABLE_MUTATING;
    }

  return NO_ERROR;
}
