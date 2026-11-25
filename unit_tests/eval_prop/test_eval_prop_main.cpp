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

/*
 * test_eval_prop_main.cpp - completeness gate of the evaluation properties (volatility, SI requirement,
 * allow_default) of operators and functions
 *
 * The consistency checks live next to the tables they audit (type_checking.c, func_type.cpp). This test adds the
 * review list: an operator or function that is neither allowed nor forbidden in a column DEFAULT is "held back",
 * and must be listed here. A new operator or function therefore cannot arrive without a decision, and opening a
 * group for DEFAULT changes its classification and this list in the same change.
 */

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "eval_prop_def.h"
#include "parse_tree.h"
#include "parser.h"

#include <algorithm>
#include <iterator>

// operators held back from a column DEFAULT
static const PT_OP_TYPE held_back_ops[] =
{
  PT_AND, PT_OR, PT_NOT, PT_BETWEEN, PT_NOT_BETWEEN, PT_LIKE, PT_NOT_LIKE, PT_IS_IN, PT_IS_NOT_IN,
  PT_IS_NULL, PT_IS_NOT_NULL, PT_IS, PT_IS_NOT, PT_EXISTS, PT_EQ_SOME, PT_NE_SOME, PT_GE_SOME, PT_GT_SOME,
  PT_LT_SOME, PT_LE_SOME, PT_EQ_ALL, PT_NE_ALL, PT_GE_ALL, PT_GT_ALL, PT_LT_ALL, PT_LE_ALL, PT_EQ, PT_NE,
  PT_GE, PT_GT, PT_LT, PT_LE, PT_NULLSAFE_EQ, PT_GT_INF, PT_LT_INF, PT_SETEQ, PT_SETNEQ, PT_SUPERSETEQ,
  PT_SUPERSET, PT_SUBSET, PT_SUBSETEQ, PT_PRIOR, PT_QPRIOR, PT_CONNECT_BY_ROOT, PT_BIT_NOT, PT_BIT_XOR,
  PT_BIT_AND, PT_BIT_OR, PT_BIT_COUNT, PT_BITSHIFT_LEFT, PT_BITSHIFT_RIGHT, PT_IF, PT_IFNULL, PT_ISNULL,
  PT_XOR, PT_ASSIGN, PT_BETWEEN_AND, PT_BETWEEN_GE_LE, PT_BETWEEN_GE_LT, PT_BETWEEN_GT_LE,
  PT_BETWEEN_GT_LT, PT_BETWEEN_EQ_NA, PT_BETWEEN_INF_LE, PT_BETWEEN_INF_LT, PT_BETWEEN_GE_INF,
  PT_BETWEEN_GT_INF, PT_RANGE, PT_POSITION, PT_SUBSTRING, PT_OCTET_LENGTH, PT_BIT_LENGTH,
  PT_SUBSTRING_INDEX, PT_MD5, PT_SPACE, PT_CHAR_LENGTH, PT_LOWER, PT_UPPER, PT_TRIM, PT_LTRIM, PT_RTRIM,
  PT_LPAD, PT_RPAD, PT_REPLACE, PT_TRANSLATE, PT_REPEAT, PT_ADD_MONTHS, PT_LAST_DAY, PT_MONTHS_BETWEEN,
  PT_TO_DATE, PT_TO_NUMBER, PT_TO_TIME, PT_TO_TIMESTAMP, PT_TO_DATETIME, PT_CURRENT_VALUE, PT_NEXT_VALUE,
  PT_INST_NUM, PT_ROWNUM, PT_ORDERBY_NUM, PT_CONNECT_BY_ISCYCLE, PT_CONNECT_BY_ISLEAF, PT_LEVEL,
  PT_EXTRACT, PT_LIKE_ESCAPE, PT_CASE, PT_LOCAL_TRANSACTION_ID, PT_FLOOR, PT_CEIL, PT_SIGN, PT_POWER,
  PT_ROUND, PT_ABS, PT_TRUNC, PT_CHR, PT_INSTR, PT_PATH_EXPR_SET, PT_ENCRYPT, PT_DECRYPT, PT_COALESCE,
  PT_NVL, PT_NVL2, PT_DECODE, PT_INCR, PT_DECR, PT_LOG, PT_EXP, PT_SQRT, PT_CONCAT_WS,
  PT_FIELD, PT_LEFT, PT_RIGHT, PT_LOCATE, PT_MID, PT_STRCMP, PT_REVERSE, PT_ACOS, PT_ASIN, PT_ATAN,
  PT_ATAN2, PT_COS, PT_SIN, PT_COT, PT_TAN, PT_DEGREES, PT_RADIANS, PT_PI, PT_FORMAT, PT_DISK_SIZE, PT_LN,
  PT_LOG2, PT_LOG10, PT_TIME_FORMAT, PT_TIMESTAMP, PT_FROM_UNIXTIME, PT_ADDDATE, PT_DATE_ADD, PT_SUBDATE,
  PT_DATE_SUB, PT_DATE_FORMAT, PT_DATEF, PT_TIMEF, PT_YEARF, PT_MONTHF, PT_DAYF, PT_HOURF, PT_MINUTEF,
  PT_SECONDF, PT_DAYOFMONTH, PT_WEEKDAY, PT_DAYOFWEEK, PT_DAYOFYEAR, PT_QUARTERF, PT_TODAYS, PT_FROMDAYS,
  PT_TIMETOSEC, PT_SECTOTIME, PT_MAKEDATE, PT_MAKETIME, PT_WEEKF, PT_ROW_COUNT, PT_LAST_INSERT_ID,
  PT_DATEDIFF, PT_TIMEDIFF, PT_STR_TO_DATE, PT_DEFAULTF, PT_OID_OF_DUPLICATE_KEY, PT_LIKE_LOWER_BOUND,
  PT_LIKE_UPPER_BOUND, PT_BIT_TO_BLOB, PT_BLOB_FROM_FILE, PT_BLOB_LENGTH, PT_BLOB_TO_BIT, PT_CHAR_TO_BLOB,
  PT_CHAR_TO_CLOB, PT_CLOB_FROM_FILE, PT_CLOB_LENGTH, PT_CLOB_TO_CHAR, PT_TYPEOF, PT_INDEX_CARDINALITY,
  PT_ESTIMATED_TABLE_ROWS, PT_ESTIMATED_AVG_ROW_LENGTH, PT_ESTIMATED_DATA_LENGTH, PT_ESTIMATED_DATA_FREE,
  PT_DEFINE_VARIABLE, PT_EVALUATE_VARIABLE, PT_EXEC_STATS, PT_ADDTIME, PT_BIN, PT_FINDINSET, PT_HEX,
  PT_ASCII, PT_CONV, PT_RLIKE, PT_NOT_RLIKE, PT_RLIKE_BINARY, PT_NOT_RLIKE_BINARY, PT_TO_ENUMERATION_VALUE,
  PT_INET_ATON, PT_INET_NTOA, PT_CHARSET, PT_COERCIBILITY, PT_COLLATION, PT_WIDTH_BUCKET, PT_TRACE_STATS,
  PT_INDEX_PREFIX, PT_AES_ENCRYPT, PT_AES_DECRYPT, PT_SHA_ONE, PT_SHA_TWO, PT_TO_BASE64, PT_FROM_BASE64,
  PT_SLEEP, PT_DBTIMEZONE, PT_SESSIONTIMEZONE, PT_TZ_OFFSET, PT_NEW_TIME, PT_FROM_TZ, PT_TO_DATETIME_TZ,
  PT_TO_TIMESTAMP_TZ, PT_CRC32, PT_SCHEMA_DEF, PT_CONV_TZ, PT_COLLECTION_TO_STRING
};

// functions held back from a column DEFAULT (the fences of the enumeration included)
static const FUNC_CODE held_back_funcs[] =
{
  PT_MIN, PT_MAX, PT_SUM, PT_AVG, PT_STDDEV, PT_VARIANCE, PT_STDDEV_POP, PT_VAR_POP, PT_STDDEV_SAMP,
  PT_VAR_SAMP, PT_COUNT, PT_COUNT_STAR, PT_GROUPBY_NUM, PT_AGG_BIT_AND, PT_AGG_BIT_OR, PT_AGG_BIT_XOR,
  PT_GROUP_CONCAT, PT_ROW_NUMBER, PT_RANK, PT_DENSE_RANK, PT_NTILE, PT_JSON_ARRAYAGG, PT_JSON_OBJECTAGG,
  PT_TOP_AGG_FUNC, PT_LEAD, PT_LAG, PT_GENERIC, F_TABLE_SET, F_TABLE_MULTISET, F_TABLE_SEQUENCE,
  F_TOP_TABLE_FUNC, F_MIDXKEY, F_SET, F_MULTISET, F_SEQUENCE, F_VID, F_GENERIC, F_CLASS_OF,
  F_INSERT_SUBSTRING, F_JSON_OBJECT, F_JSON_ARRAY, F_JSON_MERGE, F_JSON_MERGE_PATCH, F_JSON_INSERT,
  F_JSON_REMOVE, F_JSON_ARRAY_APPEND, F_JSON_GET_ALL_PATHS, F_JSON_REPLACE, F_JSON_SET, F_JSON_KEYS,
  F_JSON_ARRAY_INSERT, F_JSON_SEARCH, F_JSON_CONTAINS_PATH, F_JSON_EXTRACT, F_JSON_CONTAINS, F_JSON_DEPTH,
  F_JSON_LENGTH, F_JSON_PRETTY, F_JSON_QUOTE, F_JSON_TYPE, F_JSON_UNQUOTE, F_JSON_VALID, F_REGEXP_COUNT,
  F_REGEXP_INSTR, F_REGEXP_LIKE, F_REGEXP_SUBSTR, F_BENCHMARK, PT_FIRST_VALUE, PT_LAST_VALUE, PT_NTH_VALUE,
  PT_MEDIAN, PT_CUME_DIST, PT_PERCENT_RANK, PT_PERCENTILE_CONT, PT_PERCENTILE_DISC
};

TEST_CASE ("the classification of every operator is consistent", "[eval_prop]")
{
  REQUIRE (pt_eval_prop_self_check_ops () == 0);
}

TEST_CASE ("the classification of every function is consistent", "[eval_prop]")
{
  REQUIRE (pt_eval_prop_self_check_funcs () == 0);
}

TEST_CASE ("an operator is held back exactly when it is listed", "[eval_prop]")
{
  for (int op = PT_AND; op < PT_LAST_OPCODE; op++)
    {
      const bool listed = std::find (std::begin (held_back_ops), std::end (held_back_ops), (PT_OP_TYPE) op)
			  != std::end (held_back_ops);
      const bool held_back = pt_eval_prop_op_allow_default (op) == PT_ALLOW_DEFAULT_UNSET;

      INFO ("operator " << op);
      REQUIRE (listed == held_back);
    }
}

TEST_CASE ("a function is held back exactly when it is listed", "[eval_prop]")
{
  for (int fcode = PT_MIN; fcode < PT_LAST_FUNC_CODE; fcode++)
    {
      const bool listed = std::find (std::begin (held_back_funcs), std::end (held_back_funcs), (FUNC_CODE) fcode)
			  != std::end (held_back_funcs);
      const bool held_back = pt_eval_prop_func_allow_default (fcode) == PT_ALLOW_DEFAULT_UNSET;

      INFO ("function " << fcode);
      REQUIRE (listed == held_back);
    }
}
