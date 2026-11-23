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

// Shared heap-stat observations and table cleanup for real SQL tests.

#ifndef _TEST_OOS_SQL_HEAP_FIXTURE_HPP_
#define _TEST_OOS_SQL_HEAP_FIXTURE_HPP_

#include "test_oos_sql_common.hpp"

namespace
{
  enum show_heap_oos_column
  {
    COL_TABLE_NAME = 0,
    COL_CLASS_OID,
    COL_HEAP_VOLUME_ID,
    COL_HEAP_FILE_ID,
    COL_HEAP_HEADER_PAGE_ID,
    COL_HAS_OOS_FILE,
    COL_OOS_VOLUME_ID,
    COL_OOS_FILE_ID,
    COL_OOS_NUM_USER_PAGES,
    COL_OOS_PAGE_SIZE,
    COL_OOS_NUM_RECS,
    COL_OOS_RECS_SUMLEN,
    COL_OOS_PHYSICAL_BYTES,
    COL_OOS_UNUSED_BYTES
  };

  static int
  show_heap_oos_query (const char *sql, DB_QUERY_RESULT **result)
  {
    int rc = exec_sql_with_result (sql, result);
    if (rc < 0)
      {
	return rc;
      }
    if (*result == nullptr)
      {
	return ER_FAILED;
      }

    rc = db_query_first_tuple (*result);
    if (rc != DB_CURSOR_SUCCESS)
      {
	db_query_end (*result);
	*result = nullptr;
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  static int
  get_int_column (DB_QUERY_RESULT *result, int column, int *out_val)
  {
    DB_VALUE val;
    int rc;

    db_make_null (&val);
    rc = db_query_get_tuple_value (result, column, &val);
    if (rc != NO_ERROR)
      {
	return rc;
      }

    DB_TYPE type = db_value_type (&val);
    if (type == DB_TYPE_INTEGER)
      {
	*out_val = db_get_int (&val);
      }
    else if (type == DB_TYPE_BIGINT)
      {
	*out_val = (int) db_get_bigint (&val);
      }
    else if (type == DB_TYPE_SHORT)
      {
	*out_val = (int) db_get_short (&val);
      }
    else
      {
	rc = ER_FAILED;
      }

    db_value_clear (&val);
    return rc;
  }
}

class OosSqlHeapFixture : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_part");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_part");
      db_commit_transaction ();
    }
};

#endif // _TEST_OOS_SQL_HEAP_FIXTURE_HPP_
