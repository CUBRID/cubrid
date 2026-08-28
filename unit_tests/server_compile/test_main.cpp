/*
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
 * test_main.cpp - the client parser works inside a
 *                 SERVER_MODE binary
 *
 * Links against the SERVER_MODE libcubrid (the same library cub_server uses)
 * and parses one SQL statement with no database and no server around it.
 * This pins the milestone-0 claim that the client compiler half is genuinely
 * compiled into the server library, not stubbed out.
 */

#include <arpa/inet.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "authenticate.h"
#include "authenticate_password.hpp"
#include "cas_protocol.h"
#include "client_session_context.hpp"
#include "driver_session.hpp"
#include "language_support.h"
#include "parser.h"
#include "thread_manager.hpp"
#include "work_space.h"

/* au lives in the session-scoped client context installed by the
 * thread's activation bracket, not in a process singleton */
static int
test_au_context_bracket (void)
{
  client_session_context ctx_a;
  client_session_context ctx_b;

  csc_activate (&ctx_a);
  if (au_ctx () != &ctx_a.au_context)
    {
      fprintf (stderr, "FAIL: au_ctx did not resolve to the activated context\n");
      return 1;
    }

  /* a fresh context defaults to disable_auth_check == true; start from false
   * so the macro round trip below actually transitions the flag */
  ctx_a.au_context.disable_auth_check = false;
  int save;
  AU_SAVE_AND_DISABLE (save);
  if (!ctx_a.au_context.disable_auth_check)
    {
      fprintf (stderr, "FAIL: AU_SAVE_AND_DISABLE missed the activated context\n");
      return 1;
    }
  AU_RESTORE (save);
  if (ctx_a.au_context.disable_auth_check)
    {
      fprintf (stderr, "FAIL: AU_RESTORE missed the activated context\n");
      return 1;
    }
  csc_deactivate ();

  /* rebinding the bracket must rebind au wholesale: ctx_b still carries the
   * fresh-context default (disable_auth_check == true), not ctx_a's false */
  csc_activate (&ctx_b);
  if (au_ctx () != &ctx_b.au_context || !Au_disable)
    {
      fprintf (stderr, "FAIL: second bracket leaked state from the first\n");
      return 1;
    }
  csc_deactivate ();

  return 0;
}

/* the password check primitive the in-server login (#118 D3) rests on works
 * inside a SERVER_MODE binary: a stored SHA2-encrypted password matches its
 * plaintext and rejects any other */
static int
test_password_match (void)
{
  char stored[AU_MAX_PASSWORD_BUF + 4];

  encrypt_password_sha2_512 ("a6_secret", stored);
  if (!match_password ("a6_secret", stored))
    {
      fprintf (stderr, "FAIL: correct password did not match its stored encryption\n");
      return 1;
    }
  if (match_password ("a6_wrong", stored) || match_password ("", stored))
    {
      fprintf (stderr, "FAIL: wrong password matched the stored encryption\n");
      return 1;
    }
  return 0;
}

/* the workspace lives in the session context (#123 D2): the ws_* globals of
 * the CS build are macros over the activated bracket here, so rebinding the
 * bracket must rebind the whole MOP closed system */
static int
test_ws_context_bracket (void)
{
  client_session_context ctx_a;
  client_session_context ctx_b;

  csc_activate (&ctx_a);
  if (csc_ws () != &ctx_a.ws)
    {
      fprintf (stderr, "FAIL: csc_ws did not resolve to the activated context\n");
      return 1;
    }
  ws_Mop_table_size = 7;
  ws_Num_dirty_mop = 3;
  csc_deactivate ();

  csc_activate (&ctx_b);
  if (ws_Mop_table_size != 0 || ws_Num_dirty_mop != 0)
    {
      fprintf (stderr, "FAIL: second bracket sees the first session's workspace\n");
      return 1;
    }
  csc_deactivate ();

  csc_activate (&ctx_a);
  if (ws_Mop_table_size != 7 || ws_Num_dirty_mop != 3)
    {
      fprintf (stderr, "FAIL: first session's workspace state did not survive rebinding\n");
      return 1;
    }
  ws_Mop_table_size = 0;
  ws_Num_dirty_mop = 0;
  csc_deactivate ();

  return 0;
}

/* the parser state is per-thread - concurrent parses on distinct
 * threads must neither corrupt each other nor diverge from a single-thread
 * parse of the same statement.  Statements are chosen to exercise the state
 * that used to be process-global: hint table (with arguments), the grammar's
 * save/restore stacks (subquery, group/order by), lexer string/number
 * buffers, and host-variable counters. */
static const char *const concurrent_stmts[] = {
  "SELECT 1",
  "SELECT /*+ ORDERED USE_NL(a b) */ a.i, b.j FROM a, b WHERE a.i = b.i",
  "SELECT x, COUNT(*) FROM t WHERE x IN (SELECT y FROM u WHERE u.k > 10) GROUP BY x ORDER BY 2",
  "INSERT INTO t (a, b) VALUES (1, 'abc'), (2, 'def')",
  "UPDATE t SET a = a + 1 WHERE b BETWEEN 1 AND 10",
  "SELECT 'it''s a test', 3.14159e2 FROM t WHERE a = ? AND b = ?",
};
static const int concurrent_stmt_count = sizeof (concurrent_stmts) / sizeof (concurrent_stmts[0]);

/* parse one statement on the calling thread; return its printed tree, or an
 * empty string on failure */
static std::string
parse_to_string (const char *sql)
{
  PARSER_CONTEXT *parser = parser_create_parser ();
  if (parser == NULL)
    {
      return std::string ();
    }
  PT_NODE **stmts = parser_parse_string (parser, sql);
  std::string out;
  if (stmts != NULL && stmts[0] != NULL && !pt_has_error (parser))
    {
      char *printed = parser_print_tree (parser, stmts[0]);
      if (printed != NULL)
	{
	  out = printed;
	}
    }
  parser_free_parser (parser);
  return out;
}

static int
test_concurrent_parse (void)
{
  const int thread_count = 8;
  const int iterations = 40;

  /* single-thread ground truth */
  std::string expected[concurrent_stmt_count];
  for (int s = 0; s < concurrent_stmt_count; s++)
    {
      expected[s] = parse_to_string (concurrent_stmts[s]);
      if (expected[s].empty ())
	{
	  fprintf (stderr, "FAIL: reference parse failed: %s\n", concurrent_stmts[s]);
	  return 1;
	}
    }

  std::atomic<int> failures (0);
  std::vector<std::thread> threads;
  for (int t = 0; t < thread_count; t++)
    {
      threads.emplace_back ([t, &expected, &failures] ()
      {
	client_session_context ctx;
	csc_activate (&ctx);
	for (int i = 0; i < iterations && failures.load () == 0; i++)
	  {
	    /* offset the rotation per thread so different statements parse
	     * concurrently, not the same one in lockstep */
	    int s = (t + i) % concurrent_stmt_count;
	    std::string got = parse_to_string (concurrent_stmts[s]);
	    if (got != expected[s])
	      {
		fprintf (stderr, "FAIL: thread %d iter %d: parse diverged for: %s\n  expected: %s\n  got: %s\n",
			 t, i, concurrent_stmts[s], expected[s].c_str (), got.empty () ? "(parse error)" : got.c_str ());
		failures.fetch_add (1);
	      }
	  }
	csc_deactivate ();
      });
    }
  for (auto &th : threads)
    {
      th.join ();
    }

  return failures.load () == 0 ? 0 : 1;
}

/* user input must not build a tree deep enough to overflow the recursive
 * tree walkers (#128 D5): nesting past the parser's fixed limit (1024) is a
 * statement error, while a moderately nested statement still parses.  The
 * guard must hold at the exact boundary and on the constructs that build
 * PT_EXPR chains outside parser_make_expression (COALESCE, CASE). */
static int
test_nesting_depth_guard (void)
{
  /* "SELECT 1+1+...+1": each '+' adds one nesting level */
  struct
  {
    const char *label;
    int terms;
    bool expect_ok;
  } plus_cases[] = {
    {"at-limit (1024)", 1024, true},
    {"over-limit (1025)", 1025, false},
  };

  for (auto &c : plus_cases)
    {
      std::string sql = "SELECT 1";
      for (int i = 0; i < c.terms; i++)
	{
	  sql += "+1";
	}
      bool ok = !parse_to_string (sql.c_str ()).empty ();
      if (ok != c.expect_ok)
	{
	  fprintf (stderr, "FAIL: '+' chain %s: expected %s, got %s\n", c.label,
		   c.expect_ok ? "accept" : "reject", ok ? "accept" : "reject");
	  return 1;
	}
    }

  /* COALESCE desugars its argument list into a nested chain without going
   * through parser_make_expression - the guard must still see it */
  {
    std::string wide = "SELECT COALESCE(1";
    for (int i = 0; i < 2000; i++)
      {
	wide += ",1";
      }
    wide += ")";
    if (!parse_to_string (wide.c_str ()).empty ())
      {
	fprintf (stderr, "FAIL: 2001-arg COALESCE parsed without error\n");
	return 1;
      }

    std::string sane = "SELECT COALESCE(1";
    for (int i = 0; i < 200; i++)
      {
	sane += ",1";
      }
    sane += ")";
    if (parse_to_string (sane.c_str ()).empty ())
      {
	fprintf (stderr, "FAIL: 201-arg COALESCE was rejected\n");
	return 1;
      }
  }

  /* deeply nested searched CASE - the when-clause and chain constructors
   * also bypass parser_make_expression */
  {
    const int levels = 1100;
    std::string deep;
    deep.reserve (levels * 32);
    deep = "SELECT ";
    for (int i = 0; i < levels; i++)
      {
	deep += "CASE WHEN 1=1 THEN ";
      }
    deep += "0";
    for (int i = 0; i < levels; i++)
      {
	deep += " ELSE 0 END";
      }
    if (!parse_to_string (deep.c_str ()).empty ())
      {
	fprintf (stderr, "FAIL: %d-level nested CASE parsed without error\n", levels);
	return 1;
      }
  }

  return 0;
}

/* the thread manager installs a per-thread alternate signal stack (#128 D6)
 * so the SA_ONSTACK crash handlers survive stack exhaustion; registration of
 * the main thread happened in cubthread::initialize below */
static int
test_crash_signal_stack_installed (void)
{
  stack_t ss;

  if (sigaltstack (NULL, &ss) != 0)
    {
      fprintf (stderr, "FAIL: sigaltstack query failed\n");
      return 1;
    }
  if ((ss.ss_flags & SS_DISABLE) != 0 || ss.ss_sp == NULL || ss.ss_size == 0)
    {
      fprintf (stderr, "FAIL: no alternate signal stack installed on a registered thread\n");
      return 1;
    }
  return 0;
}

/* allocation-failure contract (#128 D8): on a thread with no session bracket
 * there is no transaction to abort - the callback must return without
 * touching transaction state */
static int
test_ws_abort_transaction_no_bracket (void)
{
  ws_abort_transaction ();
  return 0;
}

/* the driver-facing pure helpers of the adoption endpoint (stage B1): db_info
 * parsing, the V12-single protocol gate, and the connect-reply layout the
 * JDBC/CCI drivers decode */
static int
test_adoption_wire_helpers (void)
{
  using namespace cubconn::adoption;

  /* -- parse_db_info: normal, empty-user default, unterminated fields -- */
  char db_info[DRIVER_DB_INFO_SIZE];
  memset (db_info, 0, sizeof (db_info));
  memcpy (db_info, "smokedb", 7);
  memcpy (db_info + 32, "dba", 3);
  memcpy (db_info + 64, "secret", 6);
  memcpy (db_info + 96, "jdbc:cubrid:...", 15);
  for (int i = 0; i < 20; i++)
    {
      db_info[608 + i] = (char) i;
    }

  driver_conn_info info;
  if (parse_db_info (db_info, sizeof (db_info), info) != NO_ERROR)
    {
      fprintf (stderr, "FAIL: parse_db_info rejected a well-formed packet\n");
      return 1;
    }
  if (strcmp (info.db_name, "smokedb") != 0 || strcmp (info.db_user, "dba") != 0
      || strcmp (info.db_passwd, "secret") != 0 || info.session_id[5] != 5 || info.is_health_check)
    {
      fprintf (stderr, "FAIL: parse_db_info field extraction\n");
      return 1;
    }

  memset (db_info + 32, 0, 32);	/* empty user defaults to PUBLIC */
  memset (db_info, 'x', 32);	/* dbname with no NUL: must not overrun */
  if (parse_db_info (db_info, sizeof (db_info), info) != NO_ERROR || strcmp (info.db_user, "PUBLIC") != 0
      || strlen (info.db_name) != 32)
    {
      fprintf (stderr, "FAIL: parse_db_info tampered-field handling\n");
      return 1;
    }

  memset (db_info, 0, sizeof (db_info));
  memcpy (db_info, HEALTH_CHECK_DUMMY_DB, strlen (HEALTH_CHECK_DUMMY_DB));
  if (parse_db_info (db_info, sizeof (db_info), info) != NO_ERROR || !info.is_health_check)
    {
      fprintf (stderr, "FAIL: parse_db_info health-check detection\n");
      return 1;
    }

  if (parse_db_info (db_info, sizeof (db_info) - 1, info) == NO_ERROR
      || parse_db_info (NULL, sizeof (db_info), info) == NO_ERROR)
    {
      fprintf (stderr, "FAIL: parse_db_info accepted malformed input\n");
      return 1;
    }

  /* -- parse_driver_protocol: V12 header, pre-9.0 dialect rejected -- */
  char header[DRIVER_HEADER_SIZE] = { 'C', 'U', 'B', 'R', 'K', 5, 0, 0, 0, 0 };
  header[SRV_CON_MSG_IDX_PROTO_VERSION] = (char) (CAS_PROTO_INDICATOR | 12);
  if (parse_driver_protocol (header) != 12)
    {
      fprintf (stderr, "FAIL: parse_driver_protocol V12 header\n");
      return 1;
    }
  header[SRV_CON_MSG_IDX_PROTO_VERSION] = 8;	/* old major-version dialect */
  if (parse_driver_protocol (header) != -1)
    {
      fprintf (stderr, "FAIL: parse_driver_protocol accepted a pre-9.0 dialect\n");
      return 1;
    }

  /* -- build_connect_reply: byte-exact V4 layout with the token in the pid
   * slot (#117 D4) and the slot index echoed 1-based -- */
  char broker_info[DRIVER_BROKER_INFO_SIZE] = { 'C', 1, 2, 3, 4, 5, 6, 7 };
  char session[20];
  for (int i = 0; i < 20; i++)
    {
      session[i] = (char) (0x40 + i);
    }
  char reply[64];
  size_t n = build_connect_reply (0xABCD1234u, 6, broker_info, session, reply, sizeof (reply));
  if (n != 4 + 4 + CAS_CONNECTION_REPLY_SIZE)
    {
      fprintf (stderr, "FAIL: build_connect_reply size %zu\n", n);
      return 1;
    }
  unsigned int v;
  memcpy (&v, reply, 4);
  if (ntohl (v) != (unsigned int) CAS_CONNECTION_REPLY_SIZE || reply[4] != CAS_INFO_STATUS_ACTIVE)
    {
      fprintf (stderr, "FAIL: build_connect_reply prefix\n");
      return 1;
    }
  memcpy (&v, reply + 8, 4);
  if (ntohl (v) != 0xABCD1234u)
    {
      fprintf (stderr, "FAIL: build_connect_reply token slot\n");
      return 1;
    }
  if (memcmp (reply + 12, broker_info, 8) != 0)
    {
      fprintf (stderr, "FAIL: build_connect_reply broker_info echo\n");
      return 1;
    }
  memcpy (&v, reply + 20, 4);
  if (ntohl (v) != 7u)		/* slot 6 echoed 1-based */
    {
      fprintf (stderr, "FAIL: build_connect_reply slot index\n");
      return 1;
    }
  if (memcmp (reply + 24, session, 20) != 0)
    {
      fprintf (stderr, "FAIL: build_connect_reply session blob\n");
      return 1;
    }

  return 0;
}

int
main (int, char **)
{
  /* register this thread with the thread manager (same setup as the other
   * unit tests): SERVER_MODE code resolves a NULL thread_p argument through
   * cubthread::get_entry (), which asserts on an unregistered thread —
   * the password primitive's crypt/private-heap calls hit exactly that */
  cubthread::entry *thread_p = NULL;
  cubthread::initialize (thread_p);

  if (lang_init () != NO_ERROR)
    {
      fprintf (stderr, "FAIL: lang_init\n");
      return 1;
    }
  if (lang_set_charset_lang ("en_US.iso88591") != NO_ERROR)
    {
      fprintf (stderr, "FAIL: lang_set_charset_lang\n");
      return 1;
    }

  PARSER_CONTEXT *parser = parser_create_parser ();
  if (parser == NULL)
    {
      fprintf (stderr, "FAIL: parser_create_parser\n");
      return 1;
    }

  PT_NODE **stmts = parser_parse_string (parser, "SELECT 1");
  if (stmts == NULL || stmts[0] == NULL)
    {
      fprintf (stderr, "FAIL: parser_parse_string returned no statement\n");
      parser_free_parser (parser);
      return 1;
    }
  if (stmts[0]->node_type != PT_SELECT)
    {
      fprintf (stderr, "FAIL: node_type %d != PT_SELECT\n", (int) stmts[0]->node_type);
      parser_free_parser (parser);
      return 1;
    }

  parser_free_parser (parser);
  printf ("PASS: SERVER_MODE binary parsed 'SELECT 1' (PT_SELECT)\n");

  if (test_au_context_bracket () != 0)
    {
      return 1;
    }
  printf ("PASS: au resolves through the session client context bracket\n");

  if (test_ws_context_bracket () != 0)
    {
      return 1;
    }
  printf ("PASS: workspace state rebinds with the session context bracket\n");

  if (test_password_match () != 0)
    {
      return 1;
    }
  printf ("PASS: password verification primitive works in the SERVER_MODE binary\n");

  if (test_concurrent_parse () != 0)
    {
      return 1;
    }
  printf ("PASS: %d threads parsed concurrently with per-thread parser state\n", 8);

  if (test_nesting_depth_guard () != 0)
    {
      return 1;
    }
  printf ("PASS: over-limit expression nesting is a statement error, sane nesting parses\n");

  if (test_crash_signal_stack_installed () != 0)
    {
      return 1;
    }
  printf ("PASS: registered thread carries an alternate signal stack for the crash handler\n");

  if (test_ws_abort_transaction_no_bracket () != 0)
    {
      return 1;
    }
  printf ("PASS: ws_abort_transaction outside a session bracket is a no-op\n");

  if (test_adoption_wire_helpers () != 0)
    {
      return 1;
    }
  printf ("PASS: adoption wire helpers (db_info parse, V12 gate, connect reply layout)\n");
  return 0;
}
