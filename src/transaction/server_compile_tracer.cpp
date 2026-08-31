/*
 *  Copyright 2016 CUBRID Corporation
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/*
 * server_compile_tracer.cpp - in-process compile tracer (test harness)
 *
 * cub_server worker threads compile, execute and fetch one SQL statement in
 * the server address space (0-hop).  Gated by env CUBRID_M0_TRACER_SQL at
 * server boot; output goes to CUBRID_M0_TRACER_OUT.
 * CUBRID_M0_TRACER_SESSIONS (default 1) runs that many concurrent sessions,
 * each with its own client session context (#123 D2) — the multi-session
 * smoke of stage A4.  Boot serialization is the engine's own
 * (boot_restart_client) since A5; sessions here just boot and run.
 * CUBRID_M0_TRACER_SCENARIO selects a scripted multi-session choreography
 * instead of the single statement — "ddl_auth" is the DDL-authorization smoke
 * of stage A6 (grant/revoke across overlapping sessions).
 */

#if defined (SERVER_MODE)

#include "server_compile_tracer.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "client_session_context.hpp"
#include "connection_defs.h"
#include "connection_sr.h"
#include "memory_alloc.h"	// free_and_init
#include "network_interface_cl.h"	// boot_unregister_client
#include "session.h"		// session_adopt_client_context
#include "storage_common.h"	// NULL_TRAN_INDEX
#include "transaction_cl.h"	// tm_Tran_index
#include "db.h"
#include "db_client_type.hpp"
#include "dbtype.h"
#include "db_value_printer.hpp"
#include "error_manager.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

static void tracer_main (char *server_name, char *sql, char *out_path);

/* thread-local server/client boundary flag (see network_interface_sr.cpp) */
extern thread_local unsigned int db_on_server;

static FILE *tracer_Fp = NULL;
static std::mutex tracer_Log_mutex;

void
boot_tracer_start_if_requested (const char *server_name)
{
  const char *sql = std::getenv ("CUBRID_M0_TRACER_SQL");
  const char *scenario = std::getenv ("CUBRID_M0_TRACER_SCENARIO");
  if ((sql == NULL || *sql == '\0') && (scenario == NULL || *scenario == '\0'))
    {
      return;
    }
  if (sql == NULL)
    {
      sql = "";
    }
  const char *out = std::getenv ("CUBRID_M0_TRACER_OUT");
  if (out == NULL || *out == '\0')
    {
      out = "m0_tracer.out";
    }

  char *server_name_dup = strdup (server_name);
  char *sql_dup = strdup (sql);
  char *out_dup = strdup (out);
  if (server_name_dup == NULL || sql_dup == NULL || out_dup == NULL)
    {
      fprintf (stderr, "M0_TRACER: FAIL argument allocation\n");
      free_and_init (server_name_dup);
      free_and_init (sql_dup);
      free_and_init (out_dup);
      return;
    }

  /* std::thread is the engine idiom (connection_worker, coordinator,
   * master_server_monitor all spawn it directly); its constructor throws on
   * resource exhaustion, so catch it — the tracer must fail, not cub_server */
  try
    {
      std::thread (tracer_main, server_name_dup, sql_dup, out_dup).detach ();
    }
  catch (const std::system_error &)
    {
      fprintf (stderr, "M0_TRACER: FAIL thread creation\n");
      free_and_init (server_name_dup);
      free_and_init (sql_dup);
      free_and_init (out_dup);
    }
}

static void
tracer_log (const char *fmt, ...)
{
  std::lock_guard<std::mutex> guard (tracer_Log_mutex);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (tracer_Fp, fmt, ap);
  va_end (ap);
  fputc ('\n', tracer_Fp);
  fflush (tracer_Fp);
}

/* Set up a full in-process client session (thread-manager ritual, socketless
 * conn entry, client context bracket, boot as user/password, session adoption
 * of the context), run body inside it, and tear everything down.  When
 * expect_restart_error is nonzero the boot itself is the test: it must fail
 * with exactly that error and body never runs.  Returns true when every stage
 * behaved as expected. */
static bool
in_process_session (int sid, const char *server_name, const char *user, const char *password,
		    int expect_restart_error, const std::function<bool (int)> &body)
{
  int err;
  bool succeeded = false;
  bool registered = false;
  bool adopted = false;
  CSS_CONN_ENTRY *conn = NULL;
  client_session_context *ctx = NULL;

  // register this foreign thread with the thread manager (same ritual as
  // connection_worker.cpp)
  cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();
  if (entry_p == NULL)
    {
      tracer_log ("M0_TRACER: S%d FAIL claim_entry", sid);
      return false;
    }
  entry_p->register_id ();
  entry_p->type = TT_SERVER;
  entry_p->tran_index = -1;
  entry_p->m_status = cubthread::entry::status::TS_RUN;
  entry_p->shutdown = false;
  entry_p->get_error_context ().register_thread_local ();

  /* session-scoped client context; adopted by the server session after
   * registration, freed with it (session_state_uninit) */
  ctx = new client_session_context ();
  csc_activate (ctx);

  /* the server half anchors connection state and the session on
   * thread_p->conn_entry; give this in-process client a socketless entry from
   * the server's own pool (status = CONN_OPEN, fd = INVALID_SOCKET) */
  conn = css_make_conn (INVALID_SOCKET);
  if (conn == NULL)
    {
      tracer_log ("M0_TRACER: S%d FAIL css_make_conn", sid);
      goto retire;
    }
  /* the session layer's debug invariant (session_state_verify_ref_count)
   * counts references by walking css_Active_conn_anchor, so this conn must be
   * discoverable there like any real client's (round-16 core) */
  css_insert_into_active_conn_list (conn);
  entry_p->conn_entry = conn;

  /* this thread now acts as the in-process client: start in client context;
   * enter_server/exit_server (network_interface_cl.c) toggle it per x-call */
  db_on_server = 0;

  /* boot serialization is the engine's own (boot_restart_client) since A5 */
  err = db_restart_ex ("m0_tracer", server_name, user, password, NULL, DB_CLIENT_TYPE_DEFAULT);
  if (expect_restart_error != 0)
    {
      if (err == NO_ERROR)
	{
	  tracer_log ("M0_TRACER: S%d FAIL boot as %s succeeded but error %d was expected", sid, user,
		      expect_restart_error);
	  registered = true;
	}
      else if (err != expect_restart_error)
	{
	  tracer_log ("M0_TRACER: S%d FAIL boot as %s err=%d expected=%d msg=[%s]", sid, user, err,
		      expect_restart_error, er_msg () ? er_msg () : "");
	}
      else
	{
	  tracer_log ("M0_TRACER: S%d boot as %s rejected with %d as expected", sid, user, err);
	  succeeded = true;
	}
      goto retire;
    }
  if (err != NO_ERROR)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_restart_ex err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      goto retire;
    }
  registered = true;
  tracer_log ("M0_TRACER: S%d in-process client registered (0-hop) as %s", sid, user);

  /* the session created during registration becomes the durable owner of the
   * client context (#123 D3) */
  if (session_adopt_client_context (entry_p, ctx) == NO_ERROR)
    {
      adopted = true;
    }
  else
    {
      tracer_log ("M0_TRACER: S%d FAIL session_adopt_client_context", sid);
      goto retire;
    }

  succeeded = body (sid);

retire:
  // conn and entry must not outlive this thread dirty: an orphan conn in
  // css_Active_conn_anchor corrupts Main_entry_p at shutdown (assert in
  // thread_entry suspend), and retire_entry() returns the entry to the pool
  // as-is — scrub the stamped fields first
  if (registered)
    {
      /* log the tran out like a real disconnect: a tdes left ACTIVE at shutdown aborts inline on the main thread and corrupts Main_entry_p */
      (void) boot_unregister_client (tm_Tran_index);
      /* leave the client globals logged-out so exit-path BOOT_IS_CLIENT_RESTARTED() stays a no-op */
      tm_Tran_index = NULL_TRAN_INDEX;
    }
  if (conn != NULL)
    {
      entry_p->conn_entry = NULL;
      css_free_conn (conn);
    }
  entry_p->tran_index = NULL_TRAN_INDEX;
  entry_p->m_status = cubthread::entry::status::TS_DEAD;
  csc_deactivate ();
  if (!adopted)
    {
      /* never reached a session owner — reclaim it here */
      csc_retire_and_delete (ctx);
    }
  entry_p->get_error_context ().deregister_thread_local ();
  entry_p->unregister_id ();
  cubthread::get_manager ()->retire_entry (*entry_p);
  return succeeded;
}

/* compile/execute/fetch the statement in the current session — the standard
 * per-case body (connect→prepare→execute→fetch→commit gate contract) */
static bool
tracer_sql_case (int sid, const char *sql)
{
  int err;

  /* RR isolation routes compilation through the RR transaction lock (pt_class_pre_fetch); harness opt-in */
  if (const char *iso_env = std::getenv ("CUBRID_M0_TRACER_ISOLATION"))
    {
      if (strcmp (iso_env, "RR") == 0)
	{
	  if (db_set_isolation (TRAN_REPEATABLE_READ) != NO_ERROR)
	    {
	      tracer_log ("M0_TRACER: S%d FAIL db_set_isolation msg=[%s]", sid, er_msg () ? er_msg () : "");
	      return false;
	    }
	  tracer_log ("M0_TRACER: S%d isolation set to REPEATABLE READ", sid);
	}
      else if (*iso_env != '\0')
	{
	  tracer_log ("M0_TRACER: S%d FAIL unknown CUBRID_M0_TRACER_ISOLATION=[%s]", sid, iso_env);
	  return false;
	}
    }

  DB_SESSION *session = db_open_buffer (sql);
  if (session == NULL)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_open_buffer msg=[%s]", sid, er_msg () ? er_msg () : "");
      return false;
    }

  int stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_compile_statement err=%d msg=[%s]", sid, stmt_id,
		  er_msg () ? er_msg () : "");
      db_close_session (session);
      return false;
    }
  tracer_log ("M0_TRACER: S%d compiled in server address space, stmt_id=%d", sid, stmt_id);

  /* CUBRID_M0_TRACER_BIND (comma-separated ints): exercises the native DB_VALUE hand-off into the executor */
  const char *bind_env = std::getenv ("CUBRID_M0_TRACER_BIND");
  if (bind_env != NULL && *bind_env != '\0')
    {
      DB_VALUE bind_vals[16];
      int bind_cnt = 0;
      const char *p = bind_env;
      char *endp = NULL;
      while (bind_cnt < 16)
	{
	  long v = strtol (p, &endp, 10);
	  if (endp == p)
	    {
	      break;
	    }
	  db_make_int (&bind_vals[bind_cnt++], (int) v);
	  if (*endp != ',')
	    {
	      break;
	    }
	  p = endp + 1;
	}
      if (db_push_values (session, bind_cnt, bind_vals) != NO_ERROR)
	{
	  tracer_log ("M0_TRACER: S%d FAIL db_push_values msg=[%s]", sid, er_msg () ? er_msg () : "");
	  db_close_session (session);
	  return false;
	}
      tracer_log ("M0_TRACER: S%d bound %d host variable(s)", sid, bind_cnt);
    }

  DB_QUERY_RESULT *result = NULL;
  err = db_execute_statement (session, stmt_id, &result);
  if (err < 0)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_execute_statement err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      db_close_session (session);
      return false;
    }
  tracer_log ("M0_TRACER: S%d executed, row_count=%d", sid, err);

  /* the gate contract is connect→prepare→execute→fetch→commit: every stage
   * must fail loudly, or a fetch/commit regression sails through as SUCCESS */
  if (result == NULL)
    {
      tracer_log ("M0_TRACER: S%d FAIL no result to fetch", sid);
      db_close_session (session);
      return false;
    }
  err = db_query_first_tuple (result);
  if (err != DB_CURSOR_SUCCESS)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_query_first_tuple err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      db_query_end (result);
      db_close_session (session);
      return false;
    }
  DB_VALUE value;
  err = db_query_get_tuple_value (result, 0, &value);
  if (err != NO_ERROR)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_query_get_tuple_value err=%d msg=[%s]", sid, err,
		  er_msg () ? er_msg () : "");
      db_query_end (result);
      db_close_session (session);
      return false;
    }
  {
    std::lock_guard<std::mutex> guard (tracer_Log_mutex);
    fprintf (tracer_Fp, "M0_TRACER: S%d first value = ", sid);
    db_fprint_value (tracer_Fp, &value);
    fputc ('\n', tracer_Fp);
    fflush (tracer_Fp);
  }
  db_value_clear (&value);
  db_query_end (result);
  db_close_session (session);

  err = db_commit_transaction ();
  if (err != NO_ERROR)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_commit_transaction err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      return false;
    }

  /* end the server session explicitly: session_state_uninit then retires
   * this thread's own client context (A5) — the orphan hand-back runs the
   * per-session teardown (sm/Qres/workspace) at bracket exit below */
  err = db_end_session ();
  if (err != NO_ERROR)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_end_session err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      return false;
    }
  return true;
}

/* run one in-process client session as DBA over the standard SQL case */
static bool
tracer_session (int sid, const char *server_name, const char *sql)
{
  return in_process_session (sid, server_name, "DBA", "", 0,
			     [sql] (int s)
  {
    return tracer_sql_case (s, sql);
  });
}

/* Run one statement in the current session.  expect_err == 0: must succeed
 * (when first_int is non-null the first column of the first row is fetched
 * into it); expect_err < 0: the statement must be rejected — at compile or at
 * execute — with er_errid () == expect_err (and, when expect_msg is given,
 * with that substring in er_msg (), since compile-time rejections all arrive
 * as the generic ER_PT_SEMANTIC wrapping the original message). */
static bool
scenario_exec (int sid, const char *sql, int *first_int, int expect_err, const char *expect_msg = NULL)
{
  DB_SESSION *session = db_open_buffer (sql);
  if (session == NULL)
    {
      tracer_log ("M0_TRACER: S%d FAIL [%s] db_open_buffer msg=[%s]", sid, sql, er_msg () ? er_msg () : "");
      return false;
    }

  bool ok = false;
  int stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      if (expect_err != 0 && er_errid () == expect_err
	  && (expect_msg == NULL || (er_msg () != NULL && strstr (er_msg (), expect_msg) != NULL)))
	{
	  tracer_log ("M0_TRACER: S%d [%s] rejected at compile with %d as expected", sid, sql, expect_err);
	  ok = true;
	}
      else
	{
	  tracer_log ("M0_TRACER: S%d FAIL [%s] compile err=%d msg=[%s]", sid, sql, er_errid (),
		      er_msg () ? er_msg () : "");
	}
      db_close_session (session);
      return ok;
    }

  DB_QUERY_RESULT *result = NULL;
  int err = db_execute_statement (session, stmt_id, &result);
  if (err < 0)
    {
      if (expect_err != 0 && er_errid () == expect_err
	  && (expect_msg == NULL || (er_msg () != NULL && strstr (er_msg (), expect_msg) != NULL)))
	{
	  tracer_log ("M0_TRACER: S%d [%s] rejected at execute with %d as expected", sid, sql, expect_err);
	  ok = true;
	}
      else
	{
	  tracer_log ("M0_TRACER: S%d FAIL [%s] execute err=%d msg=[%s]", sid, sql, er_errid (),
		      er_msg () ? er_msg () : "");
	}
      db_close_session (session);
      return ok;
    }
  if (expect_err != 0)
    {
      tracer_log ("M0_TRACER: S%d FAIL [%s] succeeded but error %d was expected", sid, sql, expect_err);
      if (result != NULL)
	{
	  db_query_end (result);
	}
      db_close_session (session);
      return false;
    }

  ok = true;
  if (first_int != NULL)
    {
      DB_VALUE value;
      ok = false;
      if (result == NULL)
	{
	  tracer_log ("M0_TRACER: S%d FAIL [%s] no result to fetch", sid, sql);
	}
      else if (db_query_first_tuple (result) != DB_CURSOR_SUCCESS)
	{
	  tracer_log ("M0_TRACER: S%d FAIL [%s] first tuple msg=[%s]", sid, sql, er_msg () ? er_msg () : "");
	}
      else if (db_query_get_tuple_value (result, 0, &value) != NO_ERROR)
	{
	  tracer_log ("M0_TRACER: S%d FAIL [%s] tuple value msg=[%s]", sid, sql, er_msg () ? er_msg () : "");
	}
      else
	{
	  *first_int = db_get_int (&value);
	  db_value_clear (&value);
	  ok = true;
	}
    }
  if (result != NULL)
    {
      db_query_end (result);
    }
  db_close_session (session);
  return ok;
}

/* best-effort statement for idempotent cleanup — failures are ignored */
static void
scenario_try (int sid, const char *sql)
{
  DB_SESSION *session = db_open_buffer (sql);
  if (session == NULL)
    {
      return;
    }
  int stmt_id = db_compile_statement (session);
  if (stmt_id >= 0)
    {
      DB_QUERY_RESULT *result = NULL;
      if (db_execute_statement (session, stmt_id, &result) >= 0 && result != NULL)
	{
	  db_query_end (result);
	}
    }
  db_close_session (session);
  (void) sid;
}

/* A6 smoke scenario — DDL authorization is enforced server-side:
 *   [1] a wrong password is rejected by the in-server login (#118 D3);
 *   [2] a granted user compiles, keeps and executes a statement;
 *   [3] DBA revokes while that session stays open — the revoke bumps the
 *       class chn (#118 D2), so the kept plan is refused
 *       (ER_QPROC_INVALID_XASLNODE) instead of executing under the revoked
 *       grant, and a re-prepare from text is rejected by the compile-time
 *       authorization check (#118 D1/D6). */
static bool
scenario_ddl_auth (const char *server_name)
{
  bool ok;

  /* S0 (DBA): reset leftovers from a previous run, then schema + user + grant */
  ok = in_process_session (0, server_name, "DBA", "", 0, [] (int sid)
  {
    scenario_try (sid, "DROP TABLE IF EXISTS a6_t1");
    scenario_try (sid, "DROP USER a6_u1");
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL cleanup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (!scenario_exec (sid, "CREATE TABLE a6_t1 (a INT)", NULL, 0)
	|| !scenario_exec (sid, "INSERT INTO a6_t1 VALUES (42)", NULL, 0)
	|| !scenario_exec (sid, "CREATE USER a6_u1 PASSWORD 'a6_p1'", NULL, 0)
	|| !scenario_exec (sid, "GRANT SELECT ON a6_t1 TO a6_u1", NULL, 0))
      {
	return false;
      }
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL setup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (db_end_session () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    return true;
  });
  if (!ok)
    {
      return false;
    }

  /* S1: the in-server login must verify the password itself */
  ok = in_process_session (1, server_name, "a6_u1", "wrong", ER_AU_INVALID_PASSWORD,
			   [] (int)
  {
    return false;
  });
  if (!ok)
    {
      return false;
    }

  /* S2 (a6_u1) overlaps S3 (DBA): the kept statement lives across the revoke */
  std::promise<void> prepared_pr, revoked_pr;
  std::future<void> prepared = prepared_pr.get_future ();
  std::future<void> revoked = revoked_pr.get_future ();
  std::atomic<bool> prepared_signaled (false);
  auto signal_prepared = [&] ()
  {
    if (!prepared_signaled.exchange (true))
      {
	prepared_pr.set_value ();
      }
  };

  bool u1_ok = false;
  std::thread u1_thread ([&] ()
  {
    u1_ok = in_process_session (2, server_name, "a6_u1", "a6_p1", 0, [&] (int sid)
    {
      DB_SESSION *session = db_open_buffer ("SELECT a FROM dba.a6_t1");
      if (session == NULL)
	{
	  tracer_log ("M0_TRACER: S%d FAIL db_open_buffer msg=[%s]", sid, er_msg () ? er_msg () : "");
	  return false;
	}
      bool step_ok = false;
      int stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  tracer_log ("M0_TRACER: S%d FAIL compile under grant err=%d msg=[%s]", sid, stmt_id,
		      er_msg () ? er_msg () : "");
	}
      else
	{
	  DB_QUERY_RESULT *result = NULL;
	  int err = db_execute_and_keep_statement (session, stmt_id, &result);
	  if (err < 0 || result == NULL)
	    {
	      tracer_log ("M0_TRACER: S%d FAIL execute under grant err=%d msg=[%s]", sid, err,
			  er_msg () ? er_msg () : "");
	    }
	  else
	    {
	      DB_VALUE value;
	      if (db_query_first_tuple (result) == DB_CURSOR_SUCCESS
		  && db_query_get_tuple_value (result, 0, &value) == NO_ERROR)
		{
		  if (db_get_int (&value) == 42)
		    {
		      tracer_log ("M0_TRACER: S%d executed under grant, kept statement", sid);
		      step_ok = true;
		    }
		  else
		    {
		      tracer_log ("M0_TRACER: S%d FAIL wrong value under grant", sid);
		    }
		  db_value_clear (&value);
		}
	      else
		{
		  tracer_log ("M0_TRACER: S%d FAIL fetch under grant msg=[%s]", sid, er_msg () ? er_msg () : "");
		}
	      db_query_end (result);
	    }
	  /* tran boundary before the revoke — the per-tran auth cache reset
	   * (#118 D6) is what lets the next compile see the revoke */
	  if (step_ok && db_commit_transaction () != NO_ERROR)
	    {
	      tracer_log ("M0_TRACER: S%d FAIL commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	      step_ok = false;
	    }
	}

      signal_prepared ();
      if (!step_ok)
	{
	  db_close_session (session);
	  return false;
	}
      revoked.wait ();

      /* the kept plan must be refused: without the revoke-side chn bump it
       * would still execute under the revoked grant and return the row */
      step_ok = false;
      DB_QUERY_RESULT *result = NULL;
      int err = db_execute_and_keep_statement (session, stmt_id, &result);
      if (err >= 0)
	{
	  tracer_log ("M0_TRACER: S%d FAIL kept statement still executes after REVOKE", sid);
	  if (result != NULL)
	    {
	      db_query_end (result);
	    }
	}
      else if (er_errid () != ER_QPROC_INVALID_XASLNODE)
	{
	  tracer_log ("M0_TRACER: S%d FAIL kept statement err=%d (expected %d) msg=[%s]", sid, er_errid (),
		      ER_QPROC_INVALID_XASLNODE, er_msg () ? er_msg () : "");
	}
      else
	{
	  tracer_log ("M0_TRACER: S%d kept statement invalidated by REVOKE as expected", sid);
	  step_ok = true;
	}
      db_close_session (session);
      if (!step_ok)
	{
	  return false;
	}

      /* a re-prepare from text — what a driver does on the error above —
       * must hit the compile-time authorization check.  The parser wraps
       * the ER_AU_SELECT_FAILURE raised during name resolution into the
       * generic semantic error, so match the code plus the au message */
      if (!scenario_exec (sid, "SELECT a FROM dba.a6_t1", NULL, ER_PT_SEMANTIC, "not authorized"))
	{
	  return false;
	}
      if (db_abort_transaction () != NO_ERROR)
	{
	  tracer_log ("M0_TRACER: S%d FAIL db_abort_transaction msg=[%s]", sid, er_msg () ? er_msg () : "");
	  return false;
	}
      if (db_end_session () != NO_ERROR)
	{
	  tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	  return false;
	}
      return true;
    });
    /* the session may have failed before the handshake — unblock the main thread */
    signal_prepared ();
  });

  prepared.wait ();
  bool dba_ok = in_process_session (3, server_name, "DBA", "", 0, [] (int sid)
  {
    if (!scenario_exec (sid, "REVOKE SELECT ON a6_t1 FROM a6_u1", NULL, 0))
      {
	return false;
      }
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL revoke commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    tracer_log ("M0_TRACER: S%d revoked and committed", sid);
    if (db_end_session () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    return true;
  });
  revoked_pr.set_value ();
  u1_thread.join ();

  /* S4 (DBA): drop the scenario objects.  DROP USER must succeed — its
   * internal owner-check query binds the user MOP as an OBJECT host var,
   * the folded compile path that #167 fixed — and a swallowed failure here
   * leaves a6_u1 behind, failing the next run's CREATE USER */
  bool cleanup_ok = in_process_session (4, server_name, "DBA", "", 0, [] (int sid)
  {
    scenario_try (sid, "DROP TABLE a6_t1");
    if (!scenario_exec (sid, "DROP USER a6_u1", NULL, 0))
      {
	return false;
      }
    (void) db_commit_transaction ();
    (void) db_end_session ();
    return true;
  });

  return dba_ok && u1_ok && cleanup_ok;
}

/* A7 smoke scenario — the PL callback terminates in-process (#120):
 *   [1] CREATE FUNCTION of a PL/CSQL body with static SQL compiles through the
 *       in-server semantics callbacks (GET_SQL_SEMANTICS, #120 D3(2));
 *   [2] executing the function runs its static SELECT back through the
 *       INTERNAL_JDBC callback (QUERY_PREPARE/EXECUTE, #120 D3(1)) — both legs
 *       must land on this very session's thread, never on a CAS. */
static bool
scenario_plcsql (const char *server_name)
{
  return in_process_session (0, server_name, "DBA", "", 0, [] (int sid)
  {
    scenario_try (sid, "DROP FUNCTION a7_fn");
    scenario_try (sid, "DROP TABLE IF EXISTS a7_t1");
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL cleanup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (!scenario_exec (sid, "CREATE TABLE a7_t1 (a INT)", NULL, 0)
	|| !scenario_exec (sid, "INSERT INTO a7_t1 VALUES (42)", NULL, 0)
	|| !scenario_exec (sid,
			   "CREATE OR REPLACE FUNCTION a7_fn() RETURN INT AS v INT; BEGIN SELECT a INTO v FROM a7_t1; RETURN v; END;",
			   NULL, 0))
      {
	return false;
      }
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL setup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    tracer_log ("M0_TRACER: S%d plcsql function compiled via in-process semantics callback", sid);

    int v = 0;
    if (!scenario_exec (sid, "SELECT a7_fn()", &v, 0))
      {
	return false;
      }
    if (v != 42)
      {
	tracer_log ("M0_TRACER: S%d FAIL plcsql returned %d expected 42", sid, v);
	return false;
      }
    tracer_log ("M0_TRACER: S%d plcsql static SQL returned %d via in-process callback", sid, v);

    scenario_try (sid, "DROP FUNCTION a7_fn");
    scenario_try (sid, "DROP TABLE a7_t1");
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL drop commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (db_end_session () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    return true;
  });
}

/* A7 review smoke — nesting and caught errors through the in-process seam:
 *   a7_inner's static SELECT INTO finds no row and raises NO_DATA_FOUND, the
 *   PL handler catches it and returns a fallback — the caught callback error
 *   must not linger on the worker's er and poison the outer query (er
 *   isolation in method_dispatch); a7_outer calls a7_inner from its own
 *   static SQL, driving the dispatch to libcas depth 2. */
static bool
scenario_plcsql_nested (const char *server_name)
{
  return in_process_session (0, server_name, "DBA", "", 0, [] (int sid)
  {
    scenario_try (sid, "DROP FUNCTION a7_outer");
    scenario_try (sid, "DROP FUNCTION a7_inner");
    scenario_try (sid, "DROP TABLE IF EXISTS a7_t2");
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL cleanup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (!scenario_exec (sid, "CREATE TABLE a7_t2 (a INT)", NULL, 0)
	|| !scenario_exec (sid,
			   "CREATE OR REPLACE FUNCTION a7_inner() RETURN INT AS v INT; BEGIN SELECT a INTO v FROM a7_t2; RETURN v; EXCEPTION WHEN NO_DATA_FOUND THEN RETURN 7; END;",
			   NULL, 0)
	|| !scenario_exec (sid,
			   "CREATE OR REPLACE FUNCTION a7_outer() RETURN INT AS v INT; BEGIN SELECT a7_inner() + 35 INTO v; RETURN v; END;",
			   NULL, 0))
      {
	return false;
      }
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL setup commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }

    int v = 0;
    if (!scenario_exec (sid, "SELECT a7_outer()", &v, 0))
      {
	return false;
      }
    if (v != 42)
      {
	tracer_log ("M0_TRACER: S%d FAIL nested plcsql returned %d expected 42", sid, v);
	return false;
      }
    tracer_log ("M0_TRACER: S%d nested plcsql with caught inner error returned %d", sid, v);

    scenario_try (sid, "DROP FUNCTION a7_outer");
    scenario_try (sid, "DROP FUNCTION a7_inner");
    scenario_try (sid, "DROP TABLE a7_t2");
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL drop commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (db_end_session () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    return true;
  });
}

/* #165 smoke scenario — session variables cross the fold: EXECUTE ... USING
 * @v compiles through do_set_user_host_variables -> db_get_variable, whose
 * fold takes a no-copy reference from session_state under the activation
 * bracket — the path that used to die on the SA-only assert
 * (session_sr.c:307); the plain SELECT leg covers server-side evaluation of
 * the same variable. */
static bool
scenario_session_var (const char *server_name)
{
  return in_process_session (0, server_name, "DBA", "", 0, [] (int sid)
  {
    if (!scenario_exec (sid, "SET @wf165_v = 41", NULL, 0)
	|| !scenario_exec (sid, "PREPARE wf165_st FROM 'SELECT ? + 1'", NULL, 0))
      {
	return false;
      }
    int v = 0;
    if (!scenario_exec (sid, "EXECUTE wf165_st USING @wf165_v", &v, 0))
      {
	return false;
      }
    if (v != 42)
      {
	tracer_log ("M0_TRACER: S%d FAIL execute-using returned %d expected 42", sid, v);
	return false;
      }
    int w = 0;
    if (!scenario_exec (sid, "SELECT @wf165_v + 1", &w, 0))
      {
	return false;
      }
    if (w != 42)
      {
	tracer_log ("M0_TRACER: S%d FAIL select of session variable returned %d expected 42", sid, w);
	return false;
      }
    tracer_log ("M0_TRACER: S%d session variable crossed the fold on both legs, value %d", sid, v);

    if (!scenario_exec (sid, "DEALLOCATE PREPARE wf165_st", NULL, 0)
	|| !scenario_exec (sid, "DROP VARIABLE @wf165_v", NULL, 0))
      {
	return false;
      }
    if (db_commit_transaction () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL commit msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    if (db_end_session () != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_end_session msg=[%s]", sid, er_msg () ? er_msg () : "");
	return false;
      }
    return true;
  });
}

static void
tracer_main (char *server_name, char *sql, char *out_path)
{
  // let the server finish entering its service loop before we register as an
  // in-process client
  std::this_thread::sleep_for (std::chrono::seconds (2));

  tracer_Fp = fopen (out_path, "w");
  if (tracer_Fp == NULL)
    {
      tracer_Fp = stderr;
    }

  const char *scenario = std::getenv ("CUBRID_M0_TRACER_SCENARIO");
  if (scenario != NULL && *scenario != '\0')
    {
      tracer_log ("M0_TRACER: start db=%s scenario=[%s]", server_name, scenario);
      bool ok = false;
      if (strcmp (scenario, "ddl_auth") == 0)
	{
	  ok = scenario_ddl_auth (server_name);
	}
      else if (strcmp (scenario, "plcsql") == 0)
	{
	  ok = scenario_plcsql (server_name);
	}
      else if (strcmp (scenario, "plcsql_nested") == 0)
	{
	  ok = scenario_plcsql_nested (server_name);
	}
      else if (strcmp (scenario, "session_var") == 0)
	{
	  ok = scenario_session_var (server_name);
	}
      else
	{
	  tracer_log ("M0_TRACER: FAIL unknown scenario [%s]", scenario);
	}
      if (ok)
	{
	  tracer_log ("M0_TRACER: SUCCESS");
	}
      else
	{
	  tracer_log ("M0_TRACER: FAIL scenario %s", scenario);
	}
    }
  else
    {
      int sessions = 1;
      if (const char *sess_env = std::getenv ("CUBRID_M0_TRACER_SESSIONS"))
	{
	  sessions = atoi (sess_env);
	  if (sessions < 1 || sessions > 64)
	    {
	      sessions = 1;
	    }
	}

      tracer_log ("M0_TRACER: start db=%s sessions=%d sql=[%s]", server_name, sessions, sql);

      std::atomic<int> ok_count (0);
      std::vector<std::thread> threads;
      bool spawn_failed = false;
      for (int sid = 0; sid < sessions; sid++)
	{
	  try
	    {
	      threads.emplace_back ([sid, server_name, sql, &ok_count] ()
	      {
		if (tracer_session (sid, server_name, sql))
		  {
		    ok_count.fetch_add (1);
		  }
	      });
	    }
	  catch (const std::system_error &)
	    {
	      tracer_log ("M0_TRACER: FAIL thread creation for S%d", sid);
	      spawn_failed = true;
	      break;
	    }
	}
      for (auto &th : threads)
	{
	  th.join ();
	}

      // SUCCESS cues the harness to stop the server, so it must be the very
      // last act — logging it before the sessions' teardown re-opens the
      // shutdown race that teardown closes
      if (!spawn_failed && ok_count.load () == sessions)
	{
	  tracer_log ("M0_TRACER: SUCCESS");
	}
      else
	{
	  tracer_log ("M0_TRACER: FAIL %d/%d sessions succeeded", ok_count.load (), sessions);
	}
    }

  if (tracer_Fp != stderr)
    {
      fclose (tracer_Fp);
      tracer_Fp = NULL;
    }
  free_and_init (server_name);
  free_and_init (sql);
  free_and_init (out_path);
}

#endif /* SERVER_MODE */
