/*
 * Copyright 2008 Search Solution Corporation
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

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "json_builder.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
  /* dump and release in one step, so a case cannot forget the release and make
   * the ownership check at the end of the next case fail instead */
  std::string
  dump_and_release (cub_json_t *root)
  {
    char *s = cub_json_dumps (root);
    std::string out = (s == NULL) ? std::string ("(null)") : std::string (s);
    if (s != NULL)
      {
	free (s);
      }
    cub_json_decref (root);
    return out;
  }

  /* keys long enough to miss RapidJSON's inline short-string slot, so the pool
   * allocations interleave the way the real plan dump makes them */
  std::string
  long_key (int i)
  {
    char buf[64];
    snprintf (buf, sizeof (buf), "SUBQUERY (uncorrelated) #%02d", i);
    return std::string (buf);
  }
}

TEST_CASE ("A stored node stays usable", "[json_builder]")
{
  /* what the plan dump relies on: put an empty object in the parent, then keep
   * adding members through the handle it already has */
  cub_json_t *parent = cub_json_object ();
  cub_json_t *proc = cub_json_object ();

  REQUIRE (cub_json_object_set_new (parent, "BUILDLIST_PROC", proc) == 0);
  REQUIRE (cub_json_object_set_new (proc, "time", cub_json_integer (12)) == 0);
  REQUIRE (cub_json_object_set_new (proc, "fetch", cub_json_integer (34)) == 0);

  REQUIRE (dump_and_release (parent) == "{\n  \"BUILDLIST_PROC\": {\n    \"time\": 12,\n    \"fetch\": 34\n  }\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("A stored node survives its container regrowing", "[json_builder]")
{
  /* RapidJSON grows a container by reallocating its member array, and its pool
   * allocator copies rather than frees, so a handle that remembered a raw
   * address would write into the abandoned block from the seventeenth member
   * on - and the write would vanish with no error anywhere */
  for (int members = 8; members <= 40; members += 8)
    {
      cub_json_t *parent = cub_json_object ();
      cub_json_t *child = cub_json_object ();

      REQUIRE (cub_json_object_set_new (parent, "SCAN", child) == 0);
      for (int i = 0; i < members; i++)
	{
	  REQUIRE (cub_json_object_set_new (parent, long_key (i).c_str (),
					    cub_json_string ("a value long enough to allocate")) == 0);
	}
      REQUIRE (cub_json_object_set_new (child, "late", cub_json_string ("kept")) == 0);

      REQUIRE (dump_and_release (parent).find ("\"late\": \"kept\"") != std::string::npos);
      REQUIRE (cub_json_owned_count () == 0);
    }
}

TEST_CASE ("A stored node survives its array regrowing", "[json_builder]")
{
  for (int elements = 8; elements <= 40; elements += 8)
    {
      cub_json_t *array = cub_json_array ();
      cub_json_t *first = cub_json_object ();

      REQUIRE (cub_json_array_append_new (array, first) == 0);
      for (int i = 0; i < elements; i++)
	{
	  REQUIRE (cub_json_array_append_new (array, cub_json_string ("a value long enough to allocate")) == 0);
	}
      REQUIRE (cub_json_object_set_new (first, "late", cub_json_string ("kept")) == 0);

      REQUIRE (dump_and_release (array).find ("\"late\": \"kept\"") != std::string::npos);
      REQUIRE (cub_json_owned_count () == 0);
    }
}

TEST_CASE ("A stored node survives deep nesting", "[json_builder]")
{
  cub_json_t *root = cub_json_object ();
  cub_json_t *level = root;

  for (int d = 0; d < 32; d++)
    {
      cub_json_t *next = cub_json_object ();
      REQUIRE (cub_json_object_set_new (level, "SCAN", next) == 0);
      level = next;
    }
  REQUIRE (cub_json_object_set_new (level, "access", cub_json_string ("temp")) == 0);

  REQUIRE (dump_and_release (root).find ("\"access\": \"temp\"") != std::string::npos);
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("A container of the wrong kind is left alone", "[json_builder]")
{
  /* the multi-spec SCAN of a class hierarchy hands an array to code that goes
   * on to set members on it; turning it into an object would drop the specs */
  cub_json_t *array = cub_json_array ();
  REQUIRE (cub_json_array_append_new (array, cub_json_string ("t1 (heap)")) == 0);
  REQUIRE (cub_json_array_append_new (array, cub_json_string ("t2 (index)")) == 0);

  REQUIRE (cub_json_object_set_new (array, "SCAN_PROC", cub_json_object ()) == -1);
  REQUIRE (dump_and_release (array) == "[\n  \"t1 (heap)\",\n  \"t2 (index)\"\n]");
  REQUIRE (cub_json_owned_count () == 0);

  cub_json_t *object = cub_json_object ();
  REQUIRE (cub_json_object_set_new (object, "time", cub_json_integer (1)) == 0);
  REQUIRE (cub_json_array_append_new (object, cub_json_string ("x")) == -1);
  REQUIRE (dump_and_release (object) == "{\n  \"time\": 1\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("A failed store still takes the value over", "[json_builder]")
{
  /* a node that is neither stored nor released holds the whole thread's pool
   * open, so every entry point that can fail has to account for its value */
  REQUIRE (cub_json_owned_count () == 0);

  REQUIRE (cub_json_object_set_new (NULL, "rewritten query", cub_json_string ("select 1")) == -1);
  REQUIRE (cub_json_owned_count () == 0);

  REQUIRE (cub_json_array_append_new (NULL, cub_json_integer (7)) == -1);
  REQUIRE (cub_json_owned_count () == 0);

  cub_json_t *object = cub_json_object ();
  REQUIRE (cub_json_object_set_new (object, NULL, cub_json_integer (7)) == -1);
  /* the one case that does not take the value over: it is the container */
  REQUIRE (cub_json_object_set_new (object, "self", object) == -1);
  REQUIRE (cub_json_array_append_new (object, object) == -1);
  REQUIRE (dump_and_release (object) == "{}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("Repeating a key replaces its value", "[json_builder]")
{
  cub_json_t *root = cub_json_object ();
  REQUIRE (cub_json_object_set_new (root, "hash", cub_json_true ()) == 0);
  REQUIRE (cub_json_object_set_new (root, "sort", cub_json_false ()) == 0);
  REQUIRE (cub_json_object_set_new (root, "hash", cub_json_boolean (0)) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"hash\": false,\n  \"sort\": false\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("A string that is not UTF-8 is refused", "[json_builder]")
{
  /* RapidJSON's writer copies the bytes through with the default flags, and a
   * client parsing the trace would reject the whole document */
  const char euckr[] = { (char) 0xC7, (char) 0xD1, (char) 0xB1, (char) 0xB9, 0 };	/* EUC-KR */

  REQUIRE (cub_json_string (euckr) == NULL);
  REQUIRE (cub_json_string (NULL) == NULL);
  REQUIRE (cub_json_string ("\xed\xa0\x80") == NULL);	/* a surrogate half */
  REQUIRE (cub_json_string ("\xc0\xaf") == NULL);	/* an overlong slash */
  REQUIRE (cub_json_string ("\xf5\x80\x80\x80") == NULL);	/* past U+10FFFF */
  REQUIRE (cub_json_owned_count () == 0);

  cub_json_t *ok = cub_json_string ("\xed\x95\x9c\xea\xb5\xad");	/* the same word in UTF-8 */
  REQUIRE (ok != NULL);
  cub_json_decref (ok);
  REQUIRE (cub_json_owned_count () == 0);

  cub_json_t *root = cub_json_object ();
  REQUIRE (cub_json_object_set_new (root, "table", cub_json_string (euckr)) == -1);
  REQUIRE (dump_and_release (root) == "{}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("A number JSON cannot carry is refused", "[json_builder]")
{
  /* the writer stops on one of these and hands back a fragment, not a document */
  REQUIRE (cub_json_real (std::nan ("")) == NULL);
  REQUIRE (cub_json_real (HUGE_VAL) == NULL);
  REQUIRE (cub_json_real (-HUGE_VAL) == NULL);
  REQUIRE (cub_json_owned_count () == 0);

  cub_json_t *root = cub_json_object ();
  REQUIRE (cub_json_object_set_new (root, "collision_rate", cub_json_real (std::nan (""))) == -1);
  REQUIRE (cub_json_object_set_new (root, "rate", cub_json_real (12.5)) == 0);
  REQUIRE (dump_and_release (root) == "{\n  \"rate\": 12.5\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("Integers keep their full range", "[json_builder]")
{
  cub_json_t *root = cub_json_object ();
  REQUIRE (cub_json_object_set_new (root, "max", cub_json_integer (9223372036854775807LL)) == 0);
  REQUIRE (cub_json_object_set_new (root, "min", cub_json_integer (-9223372036854775807LL - 1)) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"max\": 9223372036854775807,\n  \"min\": -9223372036854775808\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("Empty containers are written as empty", "[json_builder]")
{
  cub_json_t *root = cub_json_object ();
  REQUIRE (cub_json_object_set_new (root, "empty_obj", cub_json_object ()) == 0);
  REQUIRE (cub_json_object_set_new (root, "empty_arr", cub_json_array ()) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"empty_obj\": {},\n  \"empty_arr\": []\n}");
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("cub_json_pack covers the formats the callers use", "[json_builder]")
{
  SECTION ("an object under one key")
  {
    cub_json_t *scan = cub_json_object ();
    REQUIRE (cub_json_object_set_new (scan, "table", cub_json_string ("t1")) == 0);
    REQUIRE (dump_and_release (cub_json_pack ("{s:o}", "TABLE SCAN", scan))
	     == "{\n  \"TABLE SCAN\": {\n    \"table\": \"t1\"\n  }\n}");
  }

  SECTION ("two objects in an array")
  {
    cub_json_t *outer = cub_json_object ();
    cub_json_t *inner = cub_json_object ();
    REQUIRE (cub_json_object_set_new (outer, "x", cub_json_integer (1)) == 0);
    REQUIRE (cub_json_object_set_new (inner, "y", cub_json_integer (2)) == 0);
    REQUIRE (dump_and_release (cub_json_pack ("{s:[o,o]}", "NESTED LOOPS (inner join)", outer, inner))
	     == "{\n  \"NESTED LOOPS (inner join)\": [\n    {\n      \"x\": 1\n    },\n    {\n      \"y\": 2\n    }\n  ]\n}");
  }

  SECTION ("the parallel index scan header")
  {
    cub_json_t *p = cub_json_pack ("{s:I, s:s, s:s, s:s, s:s, s:s}", "parallel_workers", (cub_json_int_t) 4,
				   "time", "0..1", "readkeys", "0..2", "filteredkeys", "0..3",
				   "rows", "0..4", "gather", "mergeable list");
    REQUIRE (dump_and_release (p)
	     == "{\n  \"parallel_workers\": 4,\n  \"time\": \"0..1\",\n  \"readkeys\": \"0..2\","
	     "\n  \"filteredkeys\": \"0..3\",\n  \"rows\": \"0..4\",\n  \"gather\": \"mergeable list\"\n}");
  }

  SECTION ("mixed integer widths")
  {
    cub_json_t *p = cub_json_pack ("{s:i, s:I, s:I}", "time", 7, "fetch", (cub_json_int_t) 8,
				   "ioread", (cub_json_int_t) 9);
    REQUIRE (dump_and_release (p) == "{\n  \"time\": 7,\n  \"fetch\": 8,\n  \"ioread\": 9\n}");
  }

  SECTION ("a double and a boolean")
  {
    cub_json_t *p = cub_json_pack ("{s:f, s:b}", "rate", 12.5, "enabled", 1);
    REQUIRE (dump_and_release (p) == "{\n  \"rate\": 12.5,\n  \"enabled\": true\n}");
  }

  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("cub_json_pack accounts for what it read when it failed", "[json_builder]")
{
  cub_json_t *scan = cub_json_object ();
  REQUIRE (cub_json_object_set_new (scan, "table", cub_json_string ("t1")) == 0);

  SECTION ("a value kind it does not describe")
  {
    /* the walk stops before it reads the argument, so the caller still owns it */
    REQUIRE (cub_json_pack ("{s:z}", "key", scan) == NULL);
    cub_json_decref (scan);
  }

  SECTION ("a missing value")
  {
    REQUIRE (cub_json_pack ("{s:o, s:o}", "one", scan, "two", (cub_json_t *) NULL) == NULL);
  }

  SECTION ("a missing element")
  {
    REQUIRE (cub_json_pack ("{s:[o,o]}", "join", scan, (cub_json_t *) NULL) == NULL);
  }

  SECTION ("an unterminated object")
  {
    REQUIRE (cub_json_pack ("{s:o", "one", scan) == NULL);
  }

  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("cub_json_pack refuses a format that is not an object", "[json_builder]")
{
  REQUIRE (cub_json_pack ("[o]") == NULL);
  REQUIRE (cub_json_pack (NULL) == NULL);
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("cub_json_loads round trips a dump", "[json_builder]")
{
  const char *text = "{\n  \"table\": \"t1\",\n  \"cost\": 12\n}";

  cub_json_t *stats = cub_json_object ();
  cub_json_t *plan = cub_json_loads (text);
  REQUIRE (plan != NULL);
  REQUIRE (cub_json_object_set_new (stats, "Query Plan", plan) == 0);

  REQUIRE (dump_and_release (stats) == "{\n  \"Query Plan\": {\n    \"table\": \"t1\",\n    \"cost\": 12\n  }\n}");
  REQUIRE (cub_json_owned_count () == 0);

  REQUIRE (cub_json_loads ("{ not json") == NULL);
  REQUIRE (cub_json_loads (NULL) == NULL);
  REQUIRE (cub_json_owned_count () == 0);
}

TEST_CASE ("cub_json_dumps refuses a node it cannot write", "[json_builder]")
{
  REQUIRE (cub_json_dumps (NULL) == NULL);
  REQUIRE (cub_json_owned_count () == 0);
}
