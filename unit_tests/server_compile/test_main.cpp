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
 * test_main.cpp - wf119 milestone-0: the client parser works inside a
 *                 SERVER_MODE binary
 *
 * Links against the SERVER_MODE libcubrid (the same library cub_server uses)
 * and parses one SQL statement with no database and no server around it.
 * This pins the milestone-0 claim that the client compiler half is genuinely
 * compiled into the server library, not stubbed out.
 */

#include <cstdio>

#include "language_support.h"
#include "parser.h"

int
main (int, char **)
{
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
  return 0;
}
