
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

/*
 * db_function.hpp
 */

#ifndef _DB_FUNCTION_HPP_
#define _DB_FUNCTION_HPP_

typedef enum
{
  /* aggregate functions */
  PT_MIN = 900, PT_MAX, PT_SUM, PT_AVG,
  PT_STDDEV, PT_VARIANCE,
  PT_STDDEV_POP, PT_VAR_POP,
  PT_STDDEV_SAMP, PT_VAR_SAMP,
  PT_COUNT, PT_COUNT_STAR,
  PT_GROUPBY_NUM,
  PT_AGG_BIT_AND, PT_AGG_BIT_OR, PT_AGG_BIT_XOR,
  PT_GROUP_CONCAT,
  PT_ROW_NUMBER,
  PT_RANK,
  PT_DENSE_RANK,
  PT_NTILE,
  PT_JSON_ARRAYAGG,
  PT_JSON_OBJECTAGG,
  PT_TOP_AGG_FUNC,
  /* only aggregate functions should be below PT_TOP_AGG_FUNC */

  /* analytic only functions */
  PT_LEAD, PT_LAG,

  /* foreign functions */
  PT_GENERIC,

  /* from here down are function code common to parser and xasl */
  /* "table" functions argument(s) are tables */
  F_TABLE_SET, F_TABLE_MULTISET, F_TABLE_SEQUENCE,
  F_TOP_TABLE_FUNC,
  F_MIDXKEY,

  /* "normal" functions, arguments are values */
  F_SET, F_MULTISET, F_SEQUENCE, F_VID, F_GENERIC, F_CLASS_OF,
  F_INSERT_SUBSTRING, F_ELT, F_JSON_OBJECT, F_JSON_ARRAY, F_JSON_MERGE, F_JSON_MERGE_PATCH, F_JSON_INSERT,
  F_JSON_REMOVE, F_JSON_ARRAY_APPEND, F_JSON_GET_ALL_PATHS, F_JSON_REPLACE, F_JSON_SET, F_JSON_KEYS,
  F_JSON_ARRAY_INSERT, F_JSON_SEARCH, F_JSON_CONTAINS_PATH, F_JSON_EXTRACT, F_JSON_CONTAINS, F_JSON_DEPTH,
  F_JSON_LENGTH, F_JSON_PRETTY, F_JSON_QUOTE, F_JSON_TYPE, F_JSON_UNQUOTE, F_JSON_VALID,

  F_REGEXP_COUNT, F_REGEXP_INSTR, F_REGEXP_LIKE, F_REGEXP_REPLACE, F_REGEXP_SUBSTR,

  F_BENCHMARK,

  /* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
  PT_FIRST_VALUE, PT_LAST_VALUE, PT_NTH_VALUE,
  /* aggregate and analytic functions */
  PT_MEDIAN,
  PT_CUME_DIST,
  PT_PERCENT_RANK,
  PT_PERCENTILE_CONT,
  PT_PERCENTILE_DISC
} FUNC_CODE;

# ifdef  __cplusplus
extern "C" {
# endif

const char *fcode_get_lowercase_name (FUNC_CODE ftype);
const char *fcode_get_uppercase_name (FUNC_CODE ftype);

# ifdef  __cplusplus
}
# endif

#endif // _DB_FUNCTION_HPP_