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
 * sp_context_rules.hpp - what a stored procedure may do, judged by the statement or trigger that invoked it
 *
 * A stored procedure runs on the PL server, but its SQL, COMMIT and ROLLBACK are carried out by this
 * client through callbacks. The callback alone does not say which statement invoked the procedure, so
 * every statement that can reach a procedure records itself here while it runs:
 *
 *   sp_Stmt_stack    the statements executing right now that contain a stored procedure call, outermost
 *                    first; a DML entry also lists the classes the statement writes
 *   sp_Trigger_stack the triggers executing right now, with the class the triggering statement modifies
 *
 * A statement without a stored procedure call is never recorded, so it pays only a flag test.
 * sp_check_statement_allowed () reads the two stacks and applies the Oracle rules for a subprogram
 * invoked from SQL or from a trigger.
 */

#ifndef _SP_CONTEXT_RULES_HPP_
#define _SP_CONTEXT_RULES_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <vector>

#include "dbtype_def.h"
#include "parse_tree.h"

struct sp_stmt_entry
{
  CUBRID_STMT_TYPE stmt_type;	/* SELECT, INSERT, UPDATE, DELETE or MERGE */
  std::vector<DB_OBJECT *> target_classes;	/* classes the statement writes to; empty for SELECT and a
						 * single-row INSERT VALUES */
  std::vector<DB_OBJECT *> read_classes;	/* classes the statement only reads; filled for a statement that
						 * calls a stored procedure */
  bool has_sp_call;		/* the statement itself calls a stored procedure */
};

struct sp_trigger_ctx
{
  DB_OBJECT *class_mop;		/* class the trigger is on; NULL for a COMMIT, ROLLBACK, ABORT or TIMEOUT trigger */
  bool is_row_level;		/* only a row-level trigger makes its class mutating */
  int stmt_index;		/* sp_Stmt_stack entry of the statement that fired the trigger; -1 when none */
};

extern std::vector<sp_stmt_entry> sp_Stmt_stack;
extern std::vector<sp_trigger_ctx> sp_Trigger_stack;

/* Pushes the statement on entry when it contains a stored procedure call; the destructor pops it on
 * every exit path, so a goto or an error return cannot leave a stale entry behind. */
class sp_stmt_guard
{
  public:
    sp_stmt_guard (const PT_NODE *statement);
    ~sp_stmt_guard ();

    sp_stmt_guard (const sp_stmt_guard &) = delete;
    sp_stmt_guard &operator= (const sp_stmt_guard &) = delete;

  private:
    bool m_pushed;
};

/* Judges a trigger's condition or action statement before it runs: when a statement that calls a stored
 * procedure is executing above, the trigger may not touch a mutating table either. Returns NO_ERROR or sets
 * and returns ER_SP_TABLE_MUTATING. */
extern int sp_check_trigger_statement (PARSER_CONTEXT *parser, PT_NODE *statement);

/* Records on a DML statement that calls a stored procedure the classes it writes and the classes it only
 * reads, after view translation; the guard copies them when the statement executes. */
extern void sp_stmt_record_classes (PARSER_CONTEXT *parser, PT_NODE *statement);

/* The classes a statement writes (INSERT target unless a single-row VALUES, UPDATE / DELETE written specs,
 * MERGE INTO), read from the node fields. */
extern void sp_stmt_written_classes (const PT_NODE *statement, std::vector<DB_OBJECT *> &classes);

/* Collecting every class a statement references, read or written: the query handler that compiles a procedure's
 * statement opens its list with sp_ref_tables_start () and closes it with sp_ref_tables_stop (); in between, name
 * resolution fills it through sp_ref_tables_add () and view translation adds the tables behind each view through
 * sp_ref_tables_add_tree (). The handler passes the list to sp_check_statement_allowed (). A partition is
 * recorded as its partitioned table. */
extern void sp_ref_tables_start (std::vector<DB_OBJECT *> *ref_tables);
extern void sp_ref_tables_stop (void);
extern void sp_ref_tables_suspend (void);
extern void sp_ref_tables_resume (void);
extern void sp_ref_tables_add (DB_OBJECT *class_mop);
extern void sp_ref_tables_add_tree (PARSER_CONTEXT *parser, PT_NODE *tree);

extern void sp_trigger_push (DB_OBJECT *class_mop, bool is_row_level);
extern void sp_trigger_pop (void);

/* Decides whether a statement the stored procedure asked this client to run is allowed where the
 * procedure is executing. stmt_type is a CUBRID_STMT_TYPE; ref_tables lists every class the statement
 * references, written_tables the classes it writes. Returns NO_ERROR or sets and returns the error. */
extern int sp_check_statement_allowed (int stmt_type, bool is_ddl, const std::vector<DB_OBJECT *> &ref_tables,
				       const std::vector<DB_OBJECT *> &written_tables);

/* The same decision for an object the procedure reads or modifies through the OID API, as a statement of
 * stmt_type on the object's class. A NULL or classless object is left to the caller's own error. */
extern int sp_check_object_allowed (DB_OBJECT *obj, int stmt_type);

#endif /* _SP_CONTEXT_RULES_HPP_ */
