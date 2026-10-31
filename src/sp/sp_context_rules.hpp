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
 * sp_context_rules.hpp - what a stored procedure may do, judged by where it was invoked
 *
 * A stored procedure runs on the PL server, but its SQL, COMMIT and ROLLBACK are carried out by this
 * client through callbacks, and a callback always arrives while the client is waiting for a server request:
 *
 *   sp_In_sql_depth      callbacks being handled that arrived while a query was executing, i.e. the procedure
 *                        was invoked inside a SQL statement; the callbacks of a CALL are not counted
 *   sp_In_trigger_depth  trigger conditions and actions executing right now
 */

#ifndef _SP_CONTEXT_RULES_HPP_
#define _SP_CONTEXT_RULES_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "parse_tree.h"

enum sp_stmt_category
{
  SP_STMT_OTHER,
  SP_STMT_TCL,			/* COMMIT, ROLLBACK, SAVEPOINT, 2PC (PREPARE TO COMMIT, ATTACH), SET (except SET @var) */
  SP_STMT_DDL,			/* including a method call (ON CLASS or the target as the first argument) */
  SP_STMT_DML
};

extern int sp_In_sql_depth;
extern int sp_In_trigger_depth;

#if defined (SA_MODE)
/* true while the request being served in process is a query execution, false for a CALL */
extern bool sp_Sa_query_request;
#endif /* SA_MODE */

/* The category of a statement a stored procedure asked this client to run; stmt_type is its CUBRID_STMT_TYPE. */
extern sp_stmt_category sp_stmt_category_of (const PT_NODE *statement, PT_NODE_TYPE stmt_type);

/* Decides whether a statement of the category is allowed where the procedure is executing. Returns NO_ERROR or
 * sets and returns the error. */
extern int sp_check_statement_allowed (sp_stmt_category stmt_category, int stmt_type);

extern void sp_reset_context (void);

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
