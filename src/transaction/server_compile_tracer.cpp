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
#include <thread>

#include "db.h"
#include "db_client_type.hpp"
#include "dbtype.h"
#include "db_value_printer.hpp"
#include "error_manager.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

static void tracer_main (char *server_name, char *sql, char *out_path);

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

  std::thread (tracer_main, strdup (server_name), strdup (sql), strdup (out)).detach ();
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

  // register this foreign thread with the thread manager (same ritual as
  // connection_worker.cpp)
  cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();
  if (entry_p == NULL)
    {
      tracer_log (fp, "M0_TRACER: FAIL claim_entry");
      return;
    }
  entry_p->register_id ();
  entry_p->type = TT_SERVER;
  entry_p->tran_index = -1;
  entry_p->m_status = cubthread::entry::status::TS_RUN;
  entry_p->shutdown = false;
  entry_p->get_error_context ().register_thread_local ();

  int err = db_restart_ex ("m0_tracer", server_name, "DBA", "", NULL, DB_CLIENT_TYPE_DEFAULT);
  if (err != NO_ERROR)
    {
      tracer_log (fp, "M0_TRACER: FAIL db_restart_ex err=%d msg=[%s]", err, er_msg () ? er_msg () : "");
      goto retire;
    }
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

    if (result != NULL)
      {
	if (db_query_first_tuple (result) == DB_CURSOR_SUCCESS)
	  {
	    DB_VALUE value;
	    if (db_query_get_tuple_value (result, 0, &value) == NO_ERROR)
	      {
		fprintf (fp, "M0_TRACER: first value = ");
		db_fprint_value (fp, &value);
		fputc ('\n', fp);
		fflush (fp);
		db_value_clear (&value);
	      }
	  }
	db_query_end (result);
      }
    db_close_session (session);
    (void) db_commit_transaction ();
    tracer_log (fp, "M0_TRACER: SUCCESS");
  }

retire:
  // milestone-0: leave the client context alive (see file header); just retire
  // the thread entry
  entry_p->get_error_context ().deregister_thread_local ();
  entry_p->unregister_id ();
  cubthread::get_manager ()->retire_entry (*entry_p);
  if (fp != stderr)
    {
      fclose (fp);
    }
  free (server_name);
  free (sql);
  free (out_path);
}

#endif /* SERVER_MODE */
