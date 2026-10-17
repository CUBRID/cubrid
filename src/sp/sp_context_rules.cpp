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

#include <algorithm>
#include <cassert>

#include "db.h"
#include "error_manager.h"
#include "execute_schema.h"
#include "parser.h"

std::vector<sp_stmt_entry> sp_Stmt_stack;
std::vector<sp_trigger_ctx> sp_Trigger_stack;

/* the list being filled while a procedure's statement is compiled; NULL for every other compile */
static std::vector<DB_OBJECT *> *sp_Ref_tables = NULL;
static int sp_Ref_tables_suspended = 0;

struct sp_stmt_collect_ctx
{
  const PT_NODE *statement;
  std::vector<DB_OBJECT *> written;
  std::vector<DB_OBJECT *> read;
};

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
  if (spec == NULL || spec->node_type != PT_SPEC)
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

/*
 * sp_spec_is_written () - whether the statement writes the spec
 *   return: bool
 *   statement(in): a DML node
 *   spec(in): a PT_SPEC anywhere in the statement
 */
static bool
sp_spec_is_written (const PT_NODE *statement, const PT_NODE *spec)
{
  switch (statement->node_type)
    {
    case PT_INSERT:
      return spec == statement->info.insert.spec && !sp_stmt_is_single_row_values (statement);
    case PT_UPDATE:
      return (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE) != 0;
    case PT_DELETE:
      return (spec->info.spec.flag & PT_SPEC_FLAG_DELETE) != 0;
    case PT_MERGE:
      return spec == statement->info.merge.into;
    default:
      return false;
    }
}

/*
 * sp_stmt_type_of () - the CUBRID_STMT_TYPE the guard records for a node, or CUBRID_STMT_NONE
 */
static CUBRID_STMT_TYPE
sp_stmt_type_of (const PT_NODE *statement)
{
  switch (statement->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_DO:
      return CUBRID_STMT_SELECT;
    case PT_INSERT:
      return CUBRID_STMT_INSERT;
    case PT_UPDATE:
      return CUBRID_STMT_UPDATE;
    case PT_DELETE:
      return CUBRID_STMT_DELETE;
    case PT_MERGE:
      return CUBRID_STMT_MERGE;
    default:
      return CUBRID_STMT_NONE;
    }
}

void
sp_stmt_written_classes (const PT_NODE *statement, std::vector<DB_OBJECT *> &classes)
{
  const PT_NODE *spec_list;

  switch (statement->node_type)
    {
    case PT_INSERT:
      spec_list = statement->info.insert.spec;
      break;
    case PT_UPDATE:
      spec_list = statement->info.update.spec;
      break;
    case PT_DELETE:
      spec_list = statement->info.delete_.spec;
      break;
    case PT_MERGE:
      spec_list = statement->info.merge.into;
      break;
    default:
      return;
    }

  for (const PT_NODE *spec = spec_list; spec != NULL; spec = spec->next)
    {
      if (spec->node_type == PT_SPEC && sp_spec_is_written (statement, spec))
	{
	  sp_stmt_add_spec_classes (spec, classes);
	}
    }
}

/*
 * sp_stmt_ref_classes () - the classes recorded on a DML node, or NULL for any other node
 */
static PT_SP_REF_CLASSES *
sp_stmt_ref_classes (PT_NODE *statement)
{
  switch (statement->node_type)
    {
    case PT_INSERT:
      return &statement->info.insert.sp_ref_classes;
    case PT_UPDATE:
      return &statement->info.update.sp_ref_classes;
    case PT_DELETE:
      return &statement->info.delete_.sp_ref_classes;
    case PT_MERGE:
      return &statement->info.merge.sp_ref_classes;
    default:
      return NULL;
    }
}

sp_stmt_guard::sp_stmt_guard (const PT_NODE *statement)
  : m_pushed (false)
{
  if (statement == NULL || (!statement->flag.has_sp_call && sp_Stmt_stack.empty () && sp_Trigger_stack.empty ()))
    {
      return;
    }

  sp_stmt_entry entry;

  entry.stmt_type = sp_stmt_type_of (statement);
  if (entry.stmt_type == CUBRID_STMT_NONE)
    {
      /* a CALL statement or anything else that is not a query: a procedure it runs is a call alone */
      return;
    }

  const PT_SP_REF_CLASSES *classes = sp_stmt_ref_classes ((PT_NODE *) statement);
  if (classes != NULL && (classes->written != NULL || classes->read != NULL))
    {
      /* sp_stmt_record_classes () recorded these when the statement was compiled */
      entry.target_classes.assign (classes->written, classes->written + classes->written_count);
      entry.read_classes.assign (classes->read, classes->read + classes->read_count);
    }
  else
    {
      /* a statement run inside a procedure or a trigger gives only the classes it writes */
      sp_stmt_written_classes (statement, entry.target_classes);
    }

  entry.has_sp_call = statement->flag.has_sp_call;
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
 * sp_ref_tables_suspend () - stop collecting while a trigger's statement is compiled on behalf of the
 *                            procedure's statement; its classes are not the procedure's references
 */
void
sp_ref_tables_suspend (void)
{
  sp_Ref_tables_suspended++;
}

void
sp_ref_tables_resume (void)
{
  assert (sp_Ref_tables_suspended > 0);
  sp_Ref_tables_suspended--;
}

/*
 * sp_ref_tables_add () - called by name resolution for every class a spec resolves to
 *   return: none
 *   class_mop(in): the class
 */
void
sp_ref_tables_add (DB_OBJECT *class_mop)
{
  if (sp_Ref_tables != NULL && sp_Ref_tables_suspended == 0)
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
  if (sp_Ref_tables == NULL || sp_Ref_tables_suspended > 0 || tree == NULL)
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
 * sp_has_class () - whether the list holds the class
 */
static bool
sp_has_class (const std::vector<DB_OBJECT *> &classes, DB_OBJECT *class_mop)
{
  return std::find (classes.begin (), classes.end (), class_mop) != classes.end ();
}

/*
 * sp_find_table_used_by_outer () - the first of the procedure's tables that an executing statement or trigger uses
 *   return: the class, or NULL
 *   tables(in): classes the stored procedure's statement references (mutating) or writes (!mutating)
 *   mutating(in): true to look for the classes an executing DML statement or a row-level trigger is writing,
 *                 false for the classes an executing DML statement only reads
 */
static DB_OBJECT *
sp_find_table_used_by_outer (const std::vector<DB_OBJECT *> &tables, bool mutating)
{
  for (DB_OBJECT *table : tables)
    {
      if (mutating)
	{
	  for (size_t i = 0; i < sp_Stmt_stack.size (); i++)
	    {
	      if (sp_has_class (sp_Stmt_stack[i].target_classes, table) && !sp_stmt_in_statement_trigger ((int) i, table))
		{
		  return table;
		}
	    }

	  for (const sp_trigger_ctx &trigger : sp_Trigger_stack)
	    {
	      if (trigger.is_row_level && trigger.class_mop == table)
		{
		  return table;
		}
	    }
	}
      else
	{
	  for (const sp_stmt_entry &entry : sp_Stmt_stack)
	    {
	      if (sp_has_class (entry.read_classes, table))
		{
		  return table;
		}
	    }
	}
    }

  return NULL;
}

static bool
sp_outer_has_select (void)
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
 *   written_tables(in): classes the statement writes
 *
 * Note: the rules, in the order they are checked:
 *       1. nothing is executing (a CALL alone): allowed
 *       2. transaction control or DDL inside a statement or a trigger: rejected
 *       3. DML while a query is executing anywhere above: rejected, whatever the table
 *       4. touching a class an executing DML statement or a row-level trigger is writing: rejected
 *       5. writing a class an executing DML statement reads: rejected (CUBRID: the statement's scan holds the
 *          page the procedure's DML needs)
 */
int
sp_check_statement_allowed (int stmt_type, bool is_ddl, const std::vector<DB_OBJECT *> &ref_tables,
			    const std::vector<DB_OBJECT *> &written_tables)
{
  const bool in_statement = !sp_Stmt_stack.empty ();
  const bool in_trigger = !sp_Trigger_stack.empty ();

  if (!in_statement && !in_trigger)
    {
      return NO_ERROR;
    }

  /* rule 1: no transaction control or DDL inside a statement, no DML inside a query */
  if (stmt_type == CUBRID_STMT_COMMIT_WORK || stmt_type == CUBRID_STMT_ROLLBACK_WORK
      || stmt_type == CUBRID_STMT_SAVEPOINT || is_ddl)
    {
      int error = in_trigger ? ER_SP_TCL_DDL_IN_TRIGGER : ER_SP_TCL_DDL_IN_QUERY;

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sp_stmt_entry_name (stmt_type, is_ddl));
      return error;
    }

  const bool is_dml = (stmt_type == CUBRID_STMT_INSERT || stmt_type == CUBRID_STMT_UPDATE
		       || stmt_type == CUBRID_STMT_DELETE || stmt_type == CUBRID_STMT_MERGE);

  if (is_dml && sp_outer_has_select ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DML_IN_QUERY, 0);
      return ER_SP_DML_IN_QUERY;
    }

  /* rule 2: a table the outer statement is modifying */
  DB_OBJECT *mutating = sp_find_table_used_by_outer (ref_tables, true);
  if (mutating != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_MUTATING, 1, db_get_class_name (mutating));
      return ER_SP_TABLE_MUTATING;
    }

  /* rule 3: a table the outer statement is reading */
  DB_OBJECT *referenced = sp_find_table_used_by_outer (written_tables, false);
  if (referenced != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_REFERENCED, 1, db_get_class_name (referenced));
      return ER_SP_TABLE_REFERENCED;
    }

  return NO_ERROR;
}

int
sp_check_object_allowed (DB_OBJECT *obj, int stmt_type)
{
  DB_OBJECT *class_mop = (obj != NULL) ? db_get_class (obj) : NULL;

  if (class_mop == NULL)
    {
      return NO_ERROR;
    }

  const std::vector<DB_OBJECT *> classes { sp_class_root (class_mop) };
  const bool writes = (stmt_type == CUBRID_STMT_UPDATE || stmt_type == CUBRID_STMT_DELETE);

  return sp_check_statement_allowed (stmt_type, false, classes, writes ? classes : std::vector<DB_OBJECT *> ());
}

/*
 * sp_stmt_collect_spec () - parser_walk_tree callback: sort the classes of every spec into the written or the
 *                           read list of the context in arg
 */
static PT_NODE *
sp_stmt_collect_spec (PARSER_CONTEXT *parser, PT_NODE *node, void *arg, int *continue_walk)
{
  sp_stmt_collect_ctx *ctx = (sp_stmt_collect_ctx *) arg;

  if (node->node_type == PT_SPEC)
    {
      sp_stmt_add_spec_classes (node, sp_spec_is_written (ctx->statement, node) ? ctx->written : ctx->read);
    }

  return node;
}

/*
 * sp_stmt_collect () - the classes a statement writes and the classes it only reads
 */
static void
sp_stmt_collect (PARSER_CONTEXT *parser, PT_NODE *statement, sp_stmt_collect_ctx &ctx)
{
  ctx.statement = statement;
  (void) parser_walk_tree (parser, statement, sp_stmt_collect_spec, &ctx, NULL, NULL);
}

static DB_OBJECT **
sp_stmt_alloc_classes (PARSER_CONTEXT *parser, const std::vector<DB_OBJECT *> &classes,
		       const std::vector<DB_OBJECT *> &excluded, int *array_count)
{
  int filled = 0;

  *array_count = 0;
  if (classes.empty ())
    {
      return NULL;
    }

  DB_OBJECT **array = (DB_OBJECT **) parser_alloc (parser, (int) (classes.size () * sizeof (DB_OBJECT *)));
  if (array == NULL)
    {
      return NULL;
    }

  /* a class may repeat; the rules only test membership, so duplicates are left in place */
  for (DB_OBJECT *class_mop : classes)
    {
      if (!sp_has_class (excluded, class_mop))
	{
	  array[filled++] = class_mop;
	}
    }

  *array_count = filled;
  return array;
}

void
sp_stmt_record_classes (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  if (statement == NULL || !statement->flag.has_sp_call)
    {
      return;
    }

  PT_SP_REF_CLASSES *classes = sp_stmt_ref_classes (statement);
  if (classes == NULL)
    {
      return;
    }

  sp_stmt_collect_ctx ctx;

  sp_stmt_collect (parser, statement, ctx);

  classes->written = sp_stmt_alloc_classes (parser, ctx.written, {}, &classes->written_count);
  classes->read = sp_stmt_alloc_classes (parser, ctx.read, ctx.written, &classes->read_count);
}

/*
 * sp_outer_calls_sp () - whether a statement that calls a stored procedure is on the stack
 */
static bool
sp_outer_calls_sp (void)
{
  for (const sp_stmt_entry &entry : sp_Stmt_stack)
    {
      if (entry.has_sp_call)
	{
	  return true;
	}
    }

  return false;
}

int
sp_check_trigger_statement (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  if (statement == NULL || !sp_outer_calls_sp ())
    {
      return NO_ERROR;
    }

  sp_stmt_collect_ctx ctx;

  sp_stmt_collect (parser, statement, ctx);

  std::vector<DB_OBJECT *> ref_tables (ctx.written);
  ref_tables.insert (ref_tables.end (), ctx.read.begin (), ctx.read.end ());

  DB_OBJECT *mutating = sp_find_table_used_by_outer (ref_tables, true);
  if (mutating != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_MUTATING, 1, db_get_class_name (mutating));
      return ER_SP_TABLE_MUTATING;
    }

  DB_OBJECT *referenced = sp_find_table_used_by_outer (ctx.written, false);
  if (referenced != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TABLE_REFERENCED, 1, db_get_class_name (referenced));
      return ER_SP_TABLE_REFERENCED;
    }

  return NO_ERROR;
}
