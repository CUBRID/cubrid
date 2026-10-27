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

#include "gtest/gtest.h"

#include "dbtype.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "language_support.h"

#include <string>
#include <vector>

// Native cache/decoder seam; item retirement is driven explicitly, without WAL or a server.
extern int bridge_la_oos_cache_store (TRANID tranid, const OID *oid, const char *data, int length);
extern int bridge_la_oos_cache_resolve (TRANID tranid, const OID *oid, int length, TP_DOMAIN *domain, DB_VALUE *value);
extern bool bridge_la_oos_cache_contains (TRANID tranid, const OID *oid);
extern void bridge_la_oos_cache_retire ();
extern void bridge_la_oos_cache_clear ();
extern size_t bridge_la_oos_cache_bytes ();
extern int bridge_la_oos_cache_count ();

namespace
{
  class oos_sql_log_cache : public ::testing::Test
  {
    protected:
      void SetUp () override
      {
	bridge_la_oos_cache_clear ();
      }

      void TearDown () override
      {
	bridge_la_oos_cache_clear ();
      }

      std::vector<char> serialize (DB_VALUE *value)
      {
	const PR_TYPE *type = pr_type_from_id (DB_VALUE_TYPE (value));
	std::vector<char> data (type->get_disk_size_of_value (value));
	OR_BUF buf;
	or_init (&buf, data.data (), data.size ());
	EXPECT_EQ (type->data_writeval (&buf, value), NO_ERROR);
	return data;
      }

      void resolve_bits (TRANID tranid, const OID &oid, const std::vector<char> &data, const std::string &expected)
      {
	DB_VALUE value;
	db_make_null (&value);
	ASSERT_EQ (bridge_la_oos_cache_resolve (tranid, &oid, data.size (), tp_domain_resolve_default (DB_TYPE_VARBIT),
						&value), NO_ERROR);
	int bit_length;
	const char *bits = db_get_bit (&value, &bit_length);
	EXPECT_EQ (bit_length, static_cast<int> (expected.size () * 8));
	EXPECT_EQ (std::string (bits, bit_length / 8), expected);
	pr_clear_value (&value);
      }
  };
}

TEST_F (oos_sql_log_cache, CompletedRowsReleasePayloadsAcrossManyRowGroups)
{
  // Two assembled attributes per row, each larger than one OOS chunk. This seam begins after assembly.
  std::string bytes (32768, 'x');
  DB_VALUE original;
  ASSERT_EQ (db_make_varbit (&original, bytes.size () * 8, bytes.data (), bytes.size () * 8), NO_ERROR);
  const auto data = serialize (&original);
  pr_clear_value (&original);

  for (int row = 0; row < 300; ++row)
    {
      OID first = {row * 2, 1, 1};
      OID second = {row * 2 + 1, 1, 1};
      ASSERT_EQ (bridge_la_oos_cache_store (41, &first, data.data (), data.size ()), NO_ERROR);
      ASSERT_EQ (bridge_la_oos_cache_store (41, &second, data.data (), data.size ()), NO_ERROR);
      ASSERT_NO_FATAL_FAILURE (resolve_bits (41, first, data, bytes));
      // The row and any retry must still see both attributes until item retirement.
      ASSERT_NO_FATAL_FAILURE (resolve_bits (41, first, data, bytes));
      ASSERT_NO_FATAL_FAILURE (resolve_bits (41, second, data, bytes));
      bridge_la_oos_cache_retire ();
      ASSERT_EQ (bridge_la_oos_cache_bytes (), 0u) << "retired row " << row;
      ASSERT_EQ (bridge_la_oos_cache_count (), 0);
    }
}

TEST_F (oos_sql_log_cache, RetirementPreservesOtherPendingValuesAndReplacementKeys)
{
  const std::string bytes (16384, 'p');
  DB_VALUE original;
  ASSERT_EQ (db_make_varbit (&original, bytes.size () * 8, bytes.data (), bytes.size () * 8), NO_ERROR);
  const auto data = serialize (&original);
  pr_clear_value (&original);
  OID consumed = {7, 1, 1};
  OID pending = {8, 1, 1};
  ASSERT_EQ (bridge_la_oos_cache_store (51, &pending, data.data (), data.size ()), NO_ERROR);
  ASSERT_EQ (bridge_la_oos_cache_store (51, &consumed, data.data (), data.size ()), NO_ERROR);
  ASSERT_NO_FATAL_FAILURE (resolve_bits (51, consumed, data, bytes));
  // mht_put replaces both key and data. Releasing the consumed old owner must preserve the new mapping.
  ASSERT_EQ (bridge_la_oos_cache_store (52, &consumed, data.data (), data.size ()), NO_ERROR);
  bridge_la_oos_cache_retire ();
  ASSERT_EQ (bridge_la_oos_cache_count (), 2);
  ASSERT_EQ (bridge_la_oos_cache_bytes (), data.size () * 2);
  EXPECT_FALSE (bridge_la_oos_cache_contains (52, &pending));
  EXPECT_FALSE (bridge_la_oos_cache_contains (51, &consumed));
  ASSERT_NO_FATAL_FAILURE (resolve_bits (52, consumed, data, bytes));
  bridge_la_oos_cache_retire ();
  ASSERT_EQ (bridge_la_oos_cache_count (), 1);
  // Appending after removal of the consumed tail must preserve the unresolved owner.
  ASSERT_EQ (bridge_la_oos_cache_store (52, &consumed, data.data (), data.size ()), NO_ERROR);
  ASSERT_NO_FATAL_FAILURE (resolve_bits (51, pending, data, bytes));
  ASSERT_NO_FATAL_FAILURE (resolve_bits (52, consumed, data, bytes));
  bridge_la_oos_cache_retire ();
  EXPECT_EQ (bridge_la_oos_cache_count (), 0);
  EXPECT_EQ (bridge_la_oos_cache_bytes (), 0u);
}

TEST_F (oos_sql_log_cache, PendingValuesSurviveEmptyRetirementAndFullCleanupIsRepeatable)
{
  const char payload[] = "pending";
  OID oid = {9, 1, 1};
  ASSERT_EQ (bridge_la_oos_cache_store (71, &oid, payload, sizeof (payload)), NO_ERROR);
  bridge_la_oos_cache_retire ();
  bridge_la_oos_cache_retire ();
  EXPECT_TRUE (bridge_la_oos_cache_contains (71, &oid));
  EXPECT_EQ (bridge_la_oos_cache_bytes (), sizeof (payload));
  bridge_la_oos_cache_clear ();
  bridge_la_oos_cache_clear ();
  EXPECT_EQ (bridge_la_oos_cache_count (), 0);
  EXPECT_EQ (bridge_la_oos_cache_bytes (), 0u);
  EXPECT_FALSE (bridge_la_oos_cache_contains (71, &oid));
}

TEST_F (oos_sql_log_cache, DecodedVarcharOwnsItsBytesAfterRetirement)
{
  const std::string text = "SQL log retains a copied value after the cache owner is released";
  DB_VALUE original;
  ASSERT_EQ (db_make_varchar (&original, text.size (), text.data (), text.size (), INTL_CODESET_UTF8,
			      LANG_COLL_UTF8_EN_CS), NO_ERROR);
  const auto data = serialize (&original);
  pr_clear_value (&original);
  OID oid = {12, 1, 1};
  ASSERT_EQ (bridge_la_oos_cache_store (61, &oid, data.data (), data.size ()), NO_ERROR);
  DB_VALUE decoded;
  db_make_null (&decoded);
  ASSERT_EQ (bridge_la_oos_cache_resolve (61, &oid, data.size (), tp_domain_resolve_default (DB_TYPE_VARCHAR),
					  &decoded), NO_ERROR);
  bridge_la_oos_cache_retire ();
  EXPECT_EQ (bridge_la_oos_cache_bytes (), 0u);
  EXPECT_EQ (std::string (db_get_string (&decoded), db_get_string_size (&decoded)), text);
  pr_clear_value (&decoded);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  lang_init ();
  tp_init ();
  lang_set_charset_lang ("en_US.utf8");
  return RUN_ALL_TESTS ();
}
