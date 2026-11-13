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
 * sp_context_rules.hpp - what a stored procedure may do, checked against where it was invoked
 *
 * A stored procedure runs on the PL server, but its SQL, COMMIT and ROLLBACK are carried out by this
 * client through callbacks, and a callback always arrives while the client is waiting for a server request:
 *
 *   sp_In_sql_depth            callbacks being handled that arrived while a query was executing, i.e. the
 *                              procedure was invoked inside a SQL statement. The callbacks of a CALL are not counted,
 *                              a trigger action CALL counts one
 *   sp_In_trigger_depth        trigger conditions and actions executing right now
 *   sp_Trigger_call_depth      the sp_In_sql_depth a procedure called by a trigger action CALL runs at, 0 when none
 *   sp_In_statement_savepoint  whether the statement savepoint of an outer statement is taken
 *   sp_Frames                  the statements and triggers running below the procedure, which rules 2 and 3
 *                              compare its writes with (sp_context_rules.cpp)
 */

#ifndef _SP_CONTEXT_RULES_HPP_
#define _SP_CONTEXT_RULES_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "parse_tree.h"

#include <vector>

enum sp_stmt_category
{
  SP_STMT_OTHER,
  SP_STMT_TCL,			/* COMMIT, ROLLBACK, SAVEPOINT, 2PC, SET (except SET @var, SET TRACE) */
  SP_STMT_DDL,			/* including a method call (ON CLASS or the target as the first argument) */
  SP_STMT_DML
};

extern int sp_In_sql_depth;
extern int sp_In_trigger_depth;
extern int sp_Trigger_call_depth;
extern bool sp_In_statement_savepoint;

#if defined (SA_MODE)
/* true while the request being served in process is a query execution, false for a CALL */
extern bool sp_Sa_query_request;
#endif /* SA_MODE */

/* The category of a statement a stored procedure asked this client to run; stmt_type is its CUBRID_STMT_TYPE. */
extern sp_stmt_category sp_stmt_category_of (const PT_NODE *statement, PT_NODE_TYPE stmt_type);

struct xasl_stmt_info;
struct xasl_id;

/* Decides whether a statement of the category is allowed where the procedure is executing: rules 1 and 2. Returns
 * NO_ERROR or sets and returns the error. */
extern int sp_check_statement_allowed (sp_stmt_category stmt_category, int stmt_type);

/* Rules 2 and 3 for what an XASL a procedure is about to run inside a SQL statement writes. */
extern int sp_check_xasl_allowed (const xasl_stmt_info &stmt_info);

/* Rules 2 and 3 for an object a procedure inside a SQL statement is about to write: an instance it is about to update,
 * delete or lock for write, or a class whose instance it is about to insert or whose class attributes it is about to
 * write or lock for write. */
extern int sp_check_object_write (DB_OBJECT *object);

/* What the XASLs a procedure inside a SQL statement ran write, by XASL_ID, which names an XASL that never changes. */
extern bool sp_find_xasl_writes (const xasl_id *xasl_id, xasl_stmt_info &stmt_info);
extern void sp_remember_xasl_writes (const xasl_id *xasl_id, const xasl_stmt_info &stmt_info);

extern void sp_reset_context (void);

/* Whether the write of a statement can change other rows through a foreign key action (an UPDATE or DELETE part). */
extern bool sp_stmt_may_cascade (const PT_NODE *statement);

#define SP_STATEMENT_SAVEPOINT_NAME "*sp-statement*"

/* Whether a statement runs inside the statement savepoint (db_do_statement_with_savepoint ()). */
extern bool sp_statement_needs_savepoint (const PT_NODE *statement);

/* The frames of the statements running now, pushed and popped by the guards below. */
extern void sp_push_xasl_frame (const xasl_stmt_info &stmt_info);
extern void sp_push_tree_frame (PARSER_CONTEXT *parser, PT_NODE *statement, bool is_trigger_action);
extern void sp_push_class_frame (DB_OBJECT *class_mop, bool cascades);
extern void sp_pop_frame (void);

/* A frame of the statement an XASL was generated for, while the XASL runs. */
class sp_xasl_frame_guard
{
  public:
    sp_xasl_frame_guard (bool enter, const xasl_stmt_info &stmt_info)
      : m_entered (enter)
    {
      if (m_entered)
	{
	  sp_push_xasl_frame (stmt_info);
	}
    }

    ~sp_xasl_frame_guard ()
    {
      if (m_entered)
	{
	  sp_pop_frame ();
	}
    }

    sp_xasl_frame_guard (const sp_xasl_frame_guard &) = delete;
    sp_xasl_frame_guard &operator= (const sp_xasl_frame_guard &) = delete;

  private:
    bool m_entered;
};

/* A frame of a statement the client evaluates without an XASL. Its classes are collected from the tree when it is
 * entered, before the client evaluates the tree. A trigger action (is_trigger_action) leaves whether a query is
 * running to the statements under it. */
class sp_tree_frame_guard
{
  public:
    sp_tree_frame_guard (bool enter, PARSER_CONTEXT *parser, PT_NODE *statement, bool is_trigger_action = false)
      : m_entered (enter)
    {
      if (m_entered)
	{
	  sp_push_tree_frame (parser, statement, is_trigger_action);
	}
    }

    ~sp_tree_frame_guard ()
    {
      if (m_entered)
	{
	  sp_pop_frame ();
	}
    }

    sp_tree_frame_guard (const sp_tree_frame_guard &) = delete;
    sp_tree_frame_guard &operator= (const sp_tree_frame_guard &) = delete;

  private:
    bool m_entered;
};

/* A frame of a trigger while it runs. A row trigger runs while the statement that fired it still runs, so that
 * statement stays compared, and the frame adds the class the trigger is defined on and its subclasses, reaching the
 * referring classes (cascades) when the event updates or deletes. Another trigger gets an empty frame, and the
 * statement that fired it is left out. */
class sp_class_frame_guard
{
  public:
    sp_class_frame_guard (DB_OBJECT *class_mop, bool cascades)
    {
      sp_push_class_frame (class_mop, cascades);
    }

    ~sp_class_frame_guard ()
    {
      sp_pop_frame ();
    }

    sp_class_frame_guard (const sp_class_frame_guard &) = delete;
    sp_class_frame_guard &operator= (const sp_class_frame_guard &) = delete;
};

/* Runs a procedure called by a trigger action CALL one level of sp_In_sql_depth below the trigger. */
class sp_trigger_call_guard
{
  public:
    explicit sp_trigger_call_guard (bool enter)
      : m_entered (enter)
      , m_prev (sp_Trigger_call_depth)
    {
      if (m_entered)
	{
	  sp_In_sql_depth++;
	  sp_Trigger_call_depth = sp_In_sql_depth;
	}
    }

    ~sp_trigger_call_guard ()
    {
      if (m_entered)
	{
	  sp_Trigger_call_depth = m_prev;
	  sp_In_sql_depth--;
	}
    }

    sp_trigger_call_guard (const sp_trigger_call_guard &) = delete;
    sp_trigger_call_guard &operator= (const sp_trigger_call_guard &) = delete;

  private:
    bool m_entered;
    int m_prev;
};

/* Marks the statement savepoint taken while it lives. */
class sp_statement_savepoint_guard
{
  public:
    sp_statement_savepoint_guard ()
      : m_prev (sp_In_statement_savepoint)
    {
      sp_In_statement_savepoint = true;
    }

    ~sp_statement_savepoint_guard ()
    {
      sp_In_statement_savepoint = m_prev;
    }

    sp_statement_savepoint_guard (const sp_statement_savepoint_guard &) = delete;
    sp_statement_savepoint_guard &operator= (const sp_statement_savepoint_guard &) = delete;

  private:
    bool m_prev;
};

/* Counts one level of sp_In_sql_depth or sp_In_trigger_depth while it lives; does nothing when enter is false. */
class sp_depth_guard
{
  public:
    sp_depth_guard (int &depth, bool enter = true)
      : m_depth (depth)
      , m_entered (enter)
    {
      if (m_entered)
	{
	  m_depth++;
	}
    }

    ~sp_depth_guard ()
    {
      if (m_entered)
	{
	  m_depth--;
	}
    }

    sp_depth_guard (const sp_depth_guard &) = delete;
    sp_depth_guard &operator= (const sp_depth_guard &) = delete;

  private:
    int &m_depth;
    bool m_entered;
};

#endif /* _SP_CONTEXT_RULES_HPP_ */
