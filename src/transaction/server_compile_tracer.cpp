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
 * server_compile_tracer.cpp - wf119 milestone-0 tracer
 *
 * Proves plan B's minimal skeleton: one cub_server worker thread calls the
 * client half (parser/compiler) in the same address space, compiles one SQL
 * statement, hands the XASL to this process's executor and fetches a result —
 * network 0-hop.
 *
 * Gate: env CUBRID_M0_TRACER_SQL is set when cub_server boots.  Output goes to
 * the file named by CUBRID_M0_TRACER_OUT (default: m0_tracer.out in cwd).
 *
 * Known milestone-0 limits (deliberate, see workspace issue #119):
 * - single tracer thread, single in-process client context (multiplexing is
 *   later work: #123/#124)
 * - the client context is never shut down (boot_shutdown_client would finalize
 *   modules shared with the server half)
 */

#if defined (SERVER_MODE)

#include "server_compile_tracer.hpp"

#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <thread>

#include "connection_defs.h"
#include "connection_sr.h"
#include "memory_alloc.h"	// free_and_init
#include "network_interface_cl.h"	// boot_unregister_client
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
tracer_log (FILE * fp, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  vfprintf (fp, fmt, ap);
  va_end (ap);
  fputc ('\n', fp);
  fflush (fp);
}

static void
tracer_main (char *server_name, char *sql, char *out_path)
{
  // let the server finish entering its service loop before we register as an
  // in-process client
  std::this_thread::sleep_for (std::chrono::seconds (2));

  FILE *fp = fopen (out_path, "w");
  if (fp == NULL)
    {
      fp = stderr;
    }
  tracer_log (fp, "M0_TRACER: start db=%s sql=[%s]", server_name, sql);

  int err;
  bool succeeded = false;
  bool registered = false;

  // register this foreign thread with the thread manager (same ritual as
  // connection_worker.cpp)
  cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();
  if (entry_p == NULL)
    {
      tracer_log (fp, "M0_TRACER: FAIL claim_entry");
      if (fp != stderr)
	{
	  fclose (fp);
	}
      free_and_init (server_name);
      free_and_init (sql);
      free_and_init (out_path);
      return;
    }
  entry_p->register_id ();
  entry_p->type = TT_SERVER;
  entry_p->tran_index = -1;
  entry_p->m_status = cubthread::entry::status::TS_RUN;
  entry_p->shutdown = false;
  entry_p->get_error_context ().register_thread_local ();

  /* the server half anchors connection state and (eventually) the session on
   * thread_p->conn_entry; give this in-process client a socketless entry from
   * the server's own pool (status = CONN_OPEN, fd = INVALID_SOCKET) */
  CSS_CONN_ENTRY *conn = css_make_conn (INVALID_SOCKET);
  if (conn == NULL)
    {
      tracer_log (fp, "M0_TRACER: FAIL css_make_conn");
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

  err = db_restart_ex ("m0_tracer", server_name, "DBA", "", NULL, DB_CLIENT_TYPE_DEFAULT);
  if (err != NO_ERROR)
    {
      tracer_log (fp, "M0_TRACER: FAIL db_restart_ex err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
      goto retire;
    }
  registered = true;
  tracer_log (fp, "M0_TRACER: in-process client registered (0-hop)");

  {
    DB_SESSION *session = db_open_buffer (sql);
    if (session == NULL)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_open_buffer msg=[%s]", er_msg () ? er_msg () : "");
	goto retire;
      }

    int stmt_id = db_compile_statement (session);
    if (stmt_id < 0)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_compile_statement err=%d msg=[%s]", stmt_id, er_msg () ? er_msg () : "");
	db_close_session (session);
	goto retire;
      }
    tracer_log (fp, "M0_TRACER: compiled in server address space, stmt_id=%d", stmt_id);

    DB_QUERY_RESULT *result = NULL;
    err = db_execute_statement (session, stmt_id, &result);
    if (err < 0)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_execute_statement err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
	db_close_session (session);
	goto retire;
      }
    tracer_log (fp, "M0_TRACER: executed, row_count=%d", err);

    /* the gate contract is connect→prepare→execute→fetch→commit: every stage
     * must fail loudly, or a fetch/commit regression sails through as SUCCESS */
    if (result == NULL)
      {
	tracer_log (fp, "M0_TRACER: FAIL no result to fetch");
	db_close_session (session);
	goto retire;
      }
    err = db_query_first_tuple (result);
    if (err != DB_CURSOR_SUCCESS)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_query_first_tuple err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
	db_query_end (result);
	db_close_session (session);
	goto retire;
      }
    DB_VALUE value;
    err = db_query_get_tuple_value (result, 0, &value);
    if (err != NO_ERROR)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_query_get_tuple_value err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
	db_query_end (result);
	db_close_session (session);
	goto retire;
      }
    fprintf (fp, "M0_TRACER: first value = ");
    db_fprint_value (fp, &value);
    fputc ('\n', fp);
    fflush (fp);
    db_value_clear (&value);
    db_query_end (result);
    db_close_session (session);

    err = db_commit_transaction ();
    if (err != NO_ERROR)
      {
	tracer_log (fp, "M0_TRACER: FAIL db_commit_transaction err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
	goto retire;
      }
    succeeded = true;
  }

retire:
  // milestone-0: the client context stays alive (see file header), but the
  // conn and the entry must not outlive this thread dirty:
  // - the socketless conn left in css_Active_conn_anchor makes the shutdown
  //   path "close" an orphan client on the main thread, stamping Main_entry_p
  //   with the tracer's tran_index and TS_FREE — the next suspend on the main
  //   entry (shutdown checkpoint's DWB wait) then assert-aborts
  //   (thread_entry.cpp:564; see PR #181 shutdown-SIGABRT diagnosis)
  // - retire_entry() returns the entry to the pool as-is, so scrub the fields
  //   this tracer stamped before another thread reuses it
  if (registered)
    {
      /* log the tran out the way a real disconnect does (frees the tdes /
       * tran index). Committing is not enough: the committed tdes goes back
       * to ACTIVE for the next transaction, and a tran left ACTIVE at
       * shutdown makes log_abort_all_active_transaction push its abort task
       * inline onto the main thread, whose execute() stamps Main_entry_p
       * with TS_FREE — the shutdown checkpoint's DWB wait then asserts
       * (caught by instrumentation, see PR #181). */
      (void) boot_unregister_client (tm_Tran_index);
      /* leave the client globals describing a logged-out context, so any
       * exit-path BOOT_IS_CLIENT_RESTARTED() check stays a no-op; the rest
       * of the context is never reused (milestone-0 file-header limitation) */
      tm_Tran_index = NULL_TRAN_INDEX;
    }
  if (conn != NULL)
    {
      entry_p->conn_entry = NULL;
      css_free_conn (conn);
    }
  entry_p->tran_index = NULL_TRAN_INDEX;
  entry_p->m_status = cubthread::entry::status::TS_DEAD;
  entry_p->get_error_context ().deregister_thread_local ();
  entry_p->unregister_id ();
  cubthread::get_manager ()->retire_entry (*entry_p);
  // SUCCESS is the harness's cue to stop the server, so it must be the very
  // last act — logging it before css_free_conn re-opens the shutdown race
  // this teardown exists to close (the orphan conn corrupting Main_entry_p).
  // A stop racing a still-running tracer remains a documented milestone-0
  // limitation; the harness never does that (it only stops after SUCCESS).
  if (succeeded)
    {
      tracer_log (fp, "M0_TRACER: SUCCESS");
    }
  if (fp != stderr)
    {
      fclose (fp);
    }
  free_and_init (server_name);
  free_and_init (sql);
  free_and_init (out_path);
}

#endif /* SERVER_MODE */
