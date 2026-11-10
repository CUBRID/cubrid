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
  dump_and_release (trace_json_t *root)
  {
    char *s = trace_json_dumps (root);
    std::string out = (s == NULL) ? std::string ("(null)") : std::string (s);
    if (s != NULL)
      {
	free (s);
      }
    trace_json_decref (root);
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
  trace_json_t *parent = trace_json_object ();
  trace_json_t *proc = trace_json_object ();

  REQUIRE (trace_json_object_set_new (parent, "BUILDLIST_PROC", proc) == 0);
  REQUIRE (trace_json_object_set_new (proc, "time", trace_json_integer (12)) == 0);
  REQUIRE (trace_json_object_set_new (proc, "fetch", trace_json_integer (34)) == 0);

  REQUIRE (dump_and_release (parent) == "{\n  \"BUILDLIST_PROC\": {\n    \"time\": 12,\n    \"fetch\": 34\n  }\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("A stored node survives its container regrowing", "[json_builder]")
{
  /* RapidJSON grows a container by reallocating its member array, and its pool
   * allocator copies rather than frees, so a handle that remembered a raw
   * address would write into the abandoned block from the seventeenth member
   * on - and the write would vanish with no error anywhere */
  for (int members = 8; members <= 40; members += 8)
    {
      trace_json_t *parent = trace_json_object ();
      trace_json_t *child = trace_json_object ();

      REQUIRE (trace_json_object_set_new (parent, "SCAN", child) == 0);
      for (int i = 0; i < members; i++)
	{
	  REQUIRE (trace_json_object_set_new (parent, long_key (i).c_str (),
					      trace_json_string ("a value long enough to allocate")) == 0);
	}
      REQUIRE (trace_json_object_set_new (child, "late", trace_json_string ("kept")) == 0);

      REQUIRE (dump_and_release (parent).find ("\"late\": \"kept\"") != std::string::npos);
      REQUIRE (trace_json_owned_count () == 0);
    }
}

TEST_CASE ("A stored node survives its array regrowing", "[json_builder]")
{
  for (int elements = 8; elements <= 40; elements += 8)
    {
      trace_json_t *array = trace_json_array ();
      trace_json_t *first = trace_json_object ();

      REQUIRE (trace_json_array_append_new (array, first) == 0);
      for (int i = 0; i < elements; i++)
	{
	  REQUIRE (trace_json_array_append_new (array, trace_json_string ("a value long enough to allocate")) == 0);
	}
      REQUIRE (trace_json_object_set_new (first, "late", trace_json_string ("kept")) == 0);

      REQUIRE (dump_and_release (array).find ("\"late\": \"kept\"") != std::string::npos);
      REQUIRE (trace_json_owned_count () == 0);
    }
}

TEST_CASE ("A stored node survives deep nesting", "[json_builder]")
{
  trace_json_t *root = trace_json_object ();
  trace_json_t *level = root;

  for (int d = 0; d < 32; d++)
    {
      trace_json_t *next = trace_json_object ();
      REQUIRE (trace_json_object_set_new (level, "SCAN", next) == 0);
      level = next;
    }
  REQUIRE (trace_json_object_set_new (level, "access", trace_json_string ("temp")) == 0);

  REQUIRE (dump_and_release (root).find ("\"access\": \"temp\"") != std::string::npos);
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("An object keeps its keys straight once it carries an index", "[json_builder]")
{
  /* past a threshold an object indexes its keys instead of scanning for them.
   * The index holds positions, not keys, so it has to agree with the container
   * on both sides of that threshold. */
  for (int members = 4; members <= 400; members += 99)
    {
      trace_json_t *root = trace_json_object ();

      for (int i = 0; i < members; i++)
	{
	  REQUIRE (trace_json_object_set_new (root, long_key (i).c_str (), trace_json_integer (i)) == 0);
	}

      /* set ten of them again, spread across the container */
      int step = members / 10 + 1;
      for (int i = 0; i < members; i += step)
	{
	  REQUIRE (trace_json_object_set_new (root, long_key (i).c_str (), trace_json_integer (-i - 1)) == 0);
	}

      /* and one that has never been seen, after the index is in play */
      REQUIRE (trace_json_object_set_new (root, "added last", trace_json_string ("tail")) == 0);

      std::string out = dump_and_release (root);

      for (int i = 0; i < members; i++)
	{
	  std::string key = "\"" + long_key (i) + "\"";
	  size_t first = out.find (key);
	  REQUIRE (first != std::string::npos);
	  REQUIRE (out.find (key, first + key.size ()) == std::string::npos);	/* exactly once */
	}
      for (int i = 0; i < members; i += step)
	{
	  REQUIRE (out.find ("\"" + long_key (i) + "\": " + std::to_string (-i - 1)) != std::string::npos);
	}
      REQUIRE (out.find ("\"added last\": \"tail\"") != std::string::npos);
      REQUIRE (trace_json_owned_count () == 0);
    }
}

TEST_CASE ("An index over a container that repeats a key", "[json_builder]")
{
  /* Only trace_json_loads () produces one, since JSON text may. A repeat has
   * no cell of its own to take, and must not take the one the probe was going
   * to hand the next new key - that key would drop out of the index. */
  const int members = 200;

  /* whether a position is lost there depends on how the key set hashes, so
   * walk a spread of them rather than lean on one */
  for (int shape = 0; shape < 12; shape++)
    {
      auto key_at = [shape] (int i)
      {
	return "SUBQUERY (uncorrelated) " + std::to_string (shape) + "." + std::to_string (i);
      };
      const int repeated = shape * 17 % members;

      std::string text = "{";
      for (int i = 0; i < members; i++)
	{
	  text += "\"" + key_at (i) + "\": " + std::to_string (i) + ", ";
	}
      text += "\"" + key_at (repeated) + "\": 999}";

      trace_json_t *root = trace_json_loads (text.c_str ());
      REQUIRE (root != NULL);

      /* every key again, so that one dropped out of the index shows up as a
       * member stored a second time rather than as a value replaced */
      for (int i = 0; i < members; i++)
	{
	  REQUIRE (trace_json_object_set_new (root, key_at (i).c_str (), trace_json_integer (-i - 1)) == 0);
	}

      std::string out = dump_and_release (root);
      for (int i = 0; i < members; i++)
	{
	  std::string key = "\"" + key_at (i) + "\"";
	  size_t first = out.find (key);
	  REQUIRE (first != std::string::npos);
	  size_t second = out.find (key, first + key.size ());
	  /* the repeated one keeps the second copy the text gave it, and the
	   * store replaced the first; every other key appears once */
	  REQUIRE ((second != std::string::npos) == (i == repeated));
	  REQUIRE (out.compare (first, key.size () + 2 + std::to_string (-i - 1).size (),
				key + ": " + std::to_string (-i - 1)) == 0);
	}
      REQUIRE (trace_json_owned_count () == 0);
    }
}

TEST_CASE ("A container of the wrong kind is left alone", "[json_builder]")
{
  /* the multi-spec SCAN of a class hierarchy hands an array to code that goes
   * on to set members on it; turning it into an object would drop the specs */
  trace_json_t *array = trace_json_array ();
  REQUIRE (trace_json_array_append_new (array, trace_json_string ("t1 (heap)")) == 0);
  REQUIRE (trace_json_array_append_new (array, trace_json_string ("t2 (index)")) == 0);

  REQUIRE (trace_json_object_set_new (array, "SCAN_PROC", trace_json_object ()) == -1);
  REQUIRE (dump_and_release (array) == "[\n  \"t1 (heap)\",\n  \"t2 (index)\"\n]");
  REQUIRE (trace_json_owned_count () == 0);

  trace_json_t *object = trace_json_object ();
  REQUIRE (trace_json_object_set_new (object, "time", trace_json_integer (1)) == 0);
  REQUIRE (trace_json_array_append_new (object, trace_json_string ("x")) == -1);
  REQUIRE (dump_and_release (object) == "{\n  \"time\": 1\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("A failed store still takes the value over", "[json_builder]")
{
  /* a node that is neither stored nor released holds the whole thread's pool
   * open, so every entry point that can fail has to account for its value */
  REQUIRE (trace_json_owned_count () == 0);

  REQUIRE (trace_json_object_set_new (NULL, "rewritten query", trace_json_string ("select 1")) == -1);
  REQUIRE (trace_json_owned_count () == 0);

  REQUIRE (trace_json_array_append_new (NULL, trace_json_integer (7)) == -1);
  REQUIRE (trace_json_owned_count () == 0);

  trace_json_t *object = trace_json_object ();
  REQUIRE (trace_json_object_set_new (object, NULL, trace_json_integer (7)) == -1);
  /* the one case that does not take the value over: it is the container */
  REQUIRE (trace_json_object_set_new (object, "self", object) == -1);
  REQUIRE (trace_json_array_append_new (object, object) == -1);
  REQUIRE (dump_and_release (object) == "{}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("A node a container already took is refused", "[json_builder]")
{
  /* The partition array of a multi-spec SCAN reached here: one spec fills it,
   * the next one finds the handle still standing and stores it again. A
   * container takes a node over by moving it, so the second store would pull
   * the array out from under the spec it belongs to and leave a null there. */
  trace_json_t *root = trace_json_object ();
  trace_json_t *first = trace_json_object ();
  trace_json_t *second = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "t1", first) == 0);
  REQUIRE (trace_json_object_set_new (root, "t2", second) == 0);

  trace_json_t *parts = trace_json_array ();
  REQUIRE (trace_json_array_append_new (parts, trace_json_string ("p0")) == 0);
  REQUIRE (trace_json_object_set_new (first, "PARTITION", parts) == 0);

  REQUIRE (trace_json_object_set_new (second, "PARTITION", parts) == -1);
  REQUIRE (trace_json_array_append_new (parts, trace_json_string ("p1")) == 0);

  /* the array is still where it was stored, and still open to its owner */
  REQUIRE (dump_and_release (root)
	   == "{\n  \"t1\": {\n    \"PARTITION\": [\n      \"p0\",\n      \"p1\"\n    ]\n  },\n  \"t2\": {}\n}");
  REQUIRE (trace_json_owned_count () == 0);

  /* the same node under the same key twice: the store would be RapidJSON
   * assigning a value to itself */
  trace_json_t *object = trace_json_object ();
  trace_json_t *value = trace_json_integer (42);
  REQUIRE (trace_json_object_set_new (object, "rows", value) == 0);
  REQUIRE (trace_json_object_set_new (object, "rows", value) == -1);
  REQUIRE (dump_and_release (object) == "{\n  \"rows\": 42\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("Repeating a key replaces its value", "[json_builder]")
{
  trace_json_t *root = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "hash", trace_json_true ()) == 0);
  REQUIRE (trace_json_object_set_new (root, "sort", trace_json_false ()) == 0);
  REQUIRE (trace_json_object_set_new (root, "hash", trace_json_boolean (0)) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"hash\": false,\n  \"sort\": false\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("A string that is not UTF-8 is refused", "[json_builder]")
{
  /* RapidJSON's writer copies the bytes through with the default flags, and a
   * client parsing the trace would reject the whole document */
  const char euckr[] = { (char) 0xC7, (char) 0xD1, (char) 0xB1, (char) 0xB9, 0 };	/* EUC-KR */

  REQUIRE (trace_json_string (euckr) == NULL);
  REQUIRE (trace_json_string (NULL) == NULL);
  REQUIRE (trace_json_string ("\xed\xa0\x80") == NULL);	/* a surrogate half */
  REQUIRE (trace_json_string ("\xc0\xaf") == NULL);	/* an overlong slash */
  REQUIRE (trace_json_string ("\xf5\x80\x80\x80") == NULL);	/* past U+10FFFF */
  REQUIRE (trace_json_owned_count () == 0);

  trace_json_t *ok = trace_json_string ("\xed\x95\x9c\xea\xb5\xad");	/* the same word in UTF-8 */
  REQUIRE (ok != NULL);
  trace_json_decref (ok);
  REQUIRE (trace_json_owned_count () == 0);

  trace_json_t *root = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "table", trace_json_string (euckr)) == -1);
  REQUIRE (dump_and_release (root) == "{}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("A number JSON cannot carry is refused", "[json_builder]")
{
  /* the writer stops on one of these and hands back a fragment, not a document */
  REQUIRE (trace_json_real (std::nan ("")) == NULL);
  REQUIRE (trace_json_real (HUGE_VAL) == NULL);
  REQUIRE (trace_json_real (-HUGE_VAL) == NULL);
  REQUIRE (trace_json_owned_count () == 0);

  trace_json_t *root = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "collision_rate", trace_json_real (std::nan (""))) == -1);
  REQUIRE (trace_json_object_set_new (root, "rate", trace_json_real (12.5)) == 0);
  REQUIRE (dump_and_release (root) == "{\n  \"rate\": 12.5\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("Integers keep their full range", "[json_builder]")
{
  trace_json_t *root = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "max", trace_json_integer (9223372036854775807LL)) == 0);
  REQUIRE (trace_json_object_set_new (root, "min", trace_json_integer (-9223372036854775807LL - 1)) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"max\": 9223372036854775807,\n  \"min\": -9223372036854775808\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("Empty containers are written as empty", "[json_builder]")
{
  trace_json_t *root = trace_json_object ();
  REQUIRE (trace_json_object_set_new (root, "empty_obj", trace_json_object ()) == 0);
  REQUIRE (trace_json_object_set_new (root, "empty_arr", trace_json_array ()) == 0);

  REQUIRE (dump_and_release (root) == "{\n  \"empty_obj\": {},\n  \"empty_arr\": []\n}");
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_pack covers the formats the callers use", "[json_builder]")
{
  SECTION ("an object under one key")
  {
    trace_json_t *scan = trace_json_object ();
    REQUIRE (trace_json_object_set_new (scan, "table", trace_json_string ("t1")) == 0);
    REQUIRE (dump_and_release (trace_json_pack ("{s:o}", "TABLE SCAN", scan))
	     == "{\n  \"TABLE SCAN\": {\n    \"table\": \"t1\"\n  }\n}");
  }

  SECTION ("two objects in an array")
  {
    trace_json_t *outer = trace_json_object ();
    trace_json_t *inner = trace_json_object ();
    REQUIRE (trace_json_object_set_new (outer, "x", trace_json_integer (1)) == 0);
    REQUIRE (trace_json_object_set_new (inner, "y", trace_json_integer (2)) == 0);
    REQUIRE (dump_and_release (trace_json_pack ("{s:[o,o]}", "NESTED LOOPS (inner join)", outer, inner))
	     == "{\n  \"NESTED LOOPS (inner join)\": [\n    {\n      \"x\": 1\n    },\n    {\n      \"y\": 2\n    }\n  ]\n}");
  }

  SECTION ("the parallel index scan header")
  {
    trace_json_t *p = trace_json_pack ("{s:I, s:s, s:s, s:s, s:s, s:s}", "parallel_workers", (trace_json_int_t) 4,
				       "time", "0..1", "readkeys", "0..2", "filteredkeys", "0..3",
				       "rows", "0..4", "gather", "mergeable list");
    REQUIRE (dump_and_release (p)
	     == "{\n  \"parallel_workers\": 4,\n  \"time\": \"0..1\",\n  \"readkeys\": \"0..2\","
	     "\n  \"filteredkeys\": \"0..3\",\n  \"rows\": \"0..4\",\n  \"gather\": \"mergeable list\"\n}");
  }

  SECTION ("mixed integer widths")
  {
    trace_json_t *p = trace_json_pack ("{s:i, s:I, s:I}", "time", 7, "fetch", (trace_json_int_t) 8,
				       "ioread", (trace_json_int_t) 9);
    REQUIRE (dump_and_release (p) == "{\n  \"time\": 7,\n  \"fetch\": 8,\n  \"ioread\": 9\n}");
  }

  SECTION ("a double and a boolean")
  {
    trace_json_t *p = trace_json_pack ("{s:f, s:b}", "rate", 12.5, "enabled", 1);
    REQUIRE (dump_and_release (p) == "{\n  \"rate\": 12.5,\n  \"enabled\": true\n}");
  }

  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_pack accounts for what it read when it failed", "[json_builder]")
{
  trace_json_t *scan = trace_json_object ();
  REQUIRE (trace_json_object_set_new (scan, "table", trace_json_string ("t1")) == 0);

  SECTION ("a value kind it does not describe")
  {
    /* the walk stops before it reads the argument, so the caller still owns it */
    REQUIRE (trace_json_pack ("{s:z}", "key", scan) == NULL);
    trace_json_decref (scan);
  }

  SECTION ("a missing value")
  {
    REQUIRE (trace_json_pack ("{s:o, s:o}", "one", scan, "two", (trace_json_t *) NULL) == NULL);
  }

  SECTION ("a missing element")
  {
    REQUIRE (trace_json_pack ("{s:[o,o]}", "join", scan, (trace_json_t *) NULL) == NULL);
  }

  SECTION ("an unterminated object")
  {
    REQUIRE (trace_json_pack ("{s:o", "one", scan) == NULL);
  }

  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_pack refuses a format that is not an object", "[json_builder]")
{
  REQUIRE (trace_json_pack ("[o]") == NULL);
  REQUIRE (trace_json_pack (NULL) == NULL);
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_pack does not read past the value that failed", "[json_builder]")
{
  /* The walk stops where it fails and a va_list needs the format to be walked,
   * so the argument behind a NULL "o" is never read and never released. Left
   * owned it holds the thread's pool open, which is what
   * qo_plan_join_print_json () checks its two operands to avoid. */
  trace_json_t *survivor = trace_json_object ();
  REQUIRE (trace_json_object_set_new (survivor, "table", trace_json_string ("t2")) == 0);

  REQUIRE (trace_json_pack ("{s:[o,o]}", "NESTED LOOPS (inner join)", (trace_json_t *) NULL, survivor) == NULL);
  REQUIRE (trace_json_owned_count () == 1);

  trace_json_decref (survivor);
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_loads round trips a dump", "[json_builder]")
{
  const char *text = "{\n  \"table\": \"t1\",\n  \"cost\": 12\n}";

  trace_json_t *stats = trace_json_object ();
  trace_json_t *plan = trace_json_loads (text);
  REQUIRE (plan != NULL);
  REQUIRE (trace_json_object_set_new (stats, "Query Plan", plan) == 0);

  REQUIRE (dump_and_release (stats) == "{\n  \"Query Plan\": {\n    \"table\": \"t1\",\n    \"cost\": 12\n  }\n}");
  REQUIRE (trace_json_owned_count () == 0);

  REQUIRE (trace_json_loads ("{ not json") == NULL);
  REQUIRE (trace_json_loads (NULL) == NULL);
  REQUIRE (trace_json_owned_count () == 0);
}

TEST_CASE ("trace_json_loads takes the text it is given as untrusted", "[json_builder]")
{
  /* it parses whatever was last written to the trace_plan session variable */

  SECTION ("a document too deep to dump is refused")
  {
    /* trace_json_dumps () recurses per level, so trace_json_loads () refuses a
     * document too deep for a 1 MB server stack, as jansson did past its limit */
    const int too_deep = 20000;
    std::string text (too_deep, '[');
    text.append (too_deep, ']');

    REQUIRE (trace_json_loads (text.c_str ()) == NULL);
    REQUIRE (trace_json_owned_count () == 0);

    /* the same nesting with nothing closing it is refused too */
    REQUIRE (trace_json_loads (std::string (too_deep, '[').c_str ()) == NULL);
    REQUIRE (trace_json_owned_count () == 0);

    /* at the limit it still loads and dumps without overflowing */
    const int at_limit = 2048;
    std::string ok (at_limit, '[');
    ok.append (at_limit, ']');

    trace_json_t *within = trace_json_loads (ok.c_str ());
    REQUIRE (within != NULL);
    char *dumped = trace_json_dumps (within);
    REQUIRE (dumped != NULL);
    free (dumped);
    trace_json_decref (within);
    REQUIRE (trace_json_owned_count () == 0);
  }

  SECTION ("a string that is not UTF-8 is refused, as it is on the way in")
  {
    REQUIRE (trace_json_loads ("{\"Query Plan\": \"\xc7\xd1\xb1\xb9\"}") == NULL);
    REQUIRE (trace_json_loads ("{\"\xc7\xd1\xb1\xb9\": 1}") == NULL);
    REQUIRE (trace_json_owned_count () == 0);
  }
}

TEST_CASE ("trace_json_dumps refuses a node it cannot write", "[json_builder]")
{
  REQUIRE (trace_json_dumps (NULL) == NULL);
  REQUIRE (trace_json_owned_count () == 0);
}
