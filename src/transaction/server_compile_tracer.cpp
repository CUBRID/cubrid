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
 * smoke of stage A4.  Client boot is not yet reentrant (A5), so registration
 * and unregistration are serialized here; the SQL work itself runs
 * concurrently.
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

/* client boot/shutdown is not reentrant until A5 — one session in it at a time */
static std::mutex tracer_Boot_mutex;

static FILE *tracer_Fp = NULL;
static std::mutex tracer_Log_mutex;

void
boot_tracer_start_if_requested (const char *server_name)
{
  const char *sql = std::getenv ("CUBRID_M0_TRACER_SQL");
  if (sql == NULL || *sql == '\0')
    {
      return;
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

/* run one in-process client session: register, compile/execute/fetch the
 * statement, commit, unregister.  Returns true when every stage succeeded. */
static bool
tracer_session (int sid, const char *server_name, const char *sql)
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

  {
    /* boot is not reentrant until A5 */
    std::lock_guard<std::mutex> boot_guard (tracer_Boot_mutex);
    err = db_restart_ex ("m0_tracer", server_name, "DBA", "", NULL, DB_CLIENT_TYPE_DEFAULT);
  }
  if (err != NO_ERROR)
    {
      tracer_log ("M0_TRACER: S%d FAIL db_restart_ex err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
      goto retire;
    }
  registered = true;
  tracer_log ("M0_TRACER: S%d in-process client registered (0-hop)", sid);

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

  /* RR isolation routes compilation through the RR transaction lock (pt_class_pre_fetch); harness opt-in */
  if (const char *iso_env = std::getenv ("CUBRID_M0_TRACER_ISOLATION"))
    {
      if (strcmp (iso_env, "RR") == 0)
	{
	  if (db_set_isolation (TRAN_REPEATABLE_READ) != NO_ERROR)
	    {
	      tracer_log ("M0_TRACER: S%d FAIL db_set_isolation msg=[%s]", sid, er_msg () ? er_msg () : "");
	      goto retire;
	    }
	  tracer_log ("M0_TRACER: S%d isolation set to REPEATABLE READ", sid);
	}
      else if (*iso_env != '\0')
	{
	  tracer_log ("M0_TRACER: S%d FAIL unknown CUBRID_M0_TRACER_ISOLATION=[%s]", sid, iso_env);
	  goto retire;
	}
    }

  {
    DB_SESSION *session = db_open_buffer (sql);
    if (session == NULL)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_open_buffer msg=[%s]", sid, er_msg () ? er_msg () : "");
	goto retire;
      }

    int stmt_id = db_compile_statement (session);
    if (stmt_id < 0)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_compile_statement err=%d msg=[%s]", sid, stmt_id,
		    er_msg () ? er_msg () : "");
	db_close_session (session);
	goto retire;
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
	    goto retire;
	  }
	tracer_log ("M0_TRACER: S%d bound %d host variable(s)", sid, bind_cnt);
      }

    DB_QUERY_RESULT *result = NULL;
    err = db_execute_statement (session, stmt_id, &result);
    if (err < 0)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_execute_statement err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
	db_close_session (session);
	goto retire;
      }
    tracer_log ("M0_TRACER: S%d executed, row_count=%d", sid, err);

    /* the gate contract is connect→prepare→execute→fetch→commit: every stage
     * must fail loudly, or a fetch/commit regression sails through as SUCCESS */
    if (result == NULL)
      {
	tracer_log ("M0_TRACER: S%d FAIL no result to fetch", sid);
	db_close_session (session);
	goto retire;
      }
    err = db_query_first_tuple (result);
    if (err != DB_CURSOR_SUCCESS)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_query_first_tuple err=%d msg=[%s]", sid, err, er_msg () ? er_msg () : "");
	db_query_end (result);
	db_close_session (session);
	goto retire;
      }
    DB_VALUE value;
    err = db_query_get_tuple_value (result, 0, &value);
    if (err != NO_ERROR)
      {
	tracer_log ("M0_TRACER: S%d FAIL db_query_get_tuple_value err=%d msg=[%s]", sid, err,
		    er_msg () ? er_msg () : "");
	db_query_end (result);
	db_close_session (session);
	goto retire;
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
	goto retire;
      }
    succeeded = true;
  }

retire:
  // conn and entry must not outlive this thread dirty: an orphan conn in
  // css_Active_conn_anchor corrupts Main_entry_p at shutdown (assert in
  // thread_entry suspend), and retire_entry() returns the entry to the pool
  // as-is — scrub the stamped fields first
  if (registered)
    {
      /* log the tran out like a real disconnect: a tdes left ACTIVE at shutdown aborts inline on the main thread and corrupts Main_entry_p */
      std::lock_guard<std::mutex> boot_guard (tracer_Boot_mutex);
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

  // SUCCESS cues the harness to stop the server, so it must be the very last
  // act — logging it before the sessions' teardown re-opens the shutdown race
  // that teardown closes
  if (!spawn_failed && ok_count.load () == sessions)
    {
      tracer_log ("M0_TRACER: SUCCESS");
    }
  else
    {
      tracer_log ("M0_TRACER: FAIL %d/%d sessions succeeded", ok_count.load (), sessions);
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
