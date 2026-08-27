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

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "authenticate.h"
#include "authenticate_password.hpp"
#include "client_session_context.hpp"
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
  return 0;
}
