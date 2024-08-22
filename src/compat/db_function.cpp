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

#include "db_function.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

const char *
fcode_get_uppercase_name (FUNC_CODE ftype)
{
  switch (ftype)
    {
    case PT_MIN:
      return "MIN";
    case PT_MAX:
      return "MAX";
    case PT_SUM:
      return "SUM";
    case PT_AVG:
      return "AVG";
    case PT_STDDEV:
      return "STDDEV";
    case PT_STDDEV_POP:
      return "STDDEV_POP";
    case PT_STDDEV_SAMP:
      return "STDDEV_SAMP";
    case PT_VARIANCE:
      return "VARIANCE";
    case PT_VAR_POP:
      return "VAR_POP";
    case PT_VAR_SAMP:
      return "VAR_SAMP";
    case PT_COUNT:
      return "COUNT";
    case PT_COUNT_STAR:
      return "COUNT_STAR";
    case PT_CUME_DIST:
      return "CUME_DIST";
    case PT_PERCENT_RANK:
      return "PERCENT_RANK";
    case PT_LEAD:
      return "LEAD";
    case PT_LAG:
      return "LAG";
    case PT_GROUPBY_NUM:
      return "GROUPBY_NUM";
    case PT_AGG_BIT_AND:
      return "BIT_AND";
    case PT_AGG_BIT_OR:
      return "BIT_OR";
    case PT_AGG_BIT_XOR:
      return "BIT_XOR";
    case PT_TOP_AGG_FUNC:
      return "TOP_AGG_FUNC";
    case PT_GROUP_CONCAT:
      return "GROUP_CONCAT";
    case PT_GENERIC:
      return "GENERIC";
    case PT_ROW_NUMBER:
      return "ROW_NUMBER";
    case PT_RANK:
      return "RANK";
    case PT_DENSE_RANK:
      return "DENSE_RANK";
    case PT_NTILE:
      return "NTILE";
    case PT_FIRST_VALUE:
      return "FIRST_VALUE";
    case PT_LAST_VALUE:
      return "LAST_VALUE";
    case PT_NTH_VALUE:
      return "NTH_VALUE";
    case PT_MEDIAN:
      return "MEDIAN";
    case PT_PERCENTILE_CONT:
      return "PERCENTILE_CONT";
    case PT_PERCENTILE_DISC:
      return "PERCENTILE_DISC";
    case PT_JSON_ARRAYAGG:
      return "JSON_ARRAYAGG";
    case PT_JSON_OBJECTAGG:
      return "JSON_OBJECTAGG";

    case F_TABLE_SET:
      return "F_TABLE_SET";
    case F_TABLE_MULTISET:
      return "F_TABLE_MULTISET";
    case F_TABLE_SEQUENCE:
      return "F_TABLE_SEQUENCE";
    case F_TOP_TABLE_FUNC:
      return "F_TOP_TABLE_FUNC";
    case F_MIDXKEY:
      return "F_MIDXKEY";
    case F_SET:
      return "F_SET";
    case F_MULTISET:
      return "F_MULTISET";
    case F_SEQUENCE:
      return "F_SEQUENCE";
    case F_VID:
      return "F_VID";
    case F_GENERIC:
      return "F_GENERIC";
    case F_CLASS_OF:
      return "F_CLASS_OF";
    case F_INSERT_SUBSTRING:
      return "INSERT_SUBSTRING";
    case F_ELT:
      return "ELT";
    case F_BENCHMARK:
      return "BENCHMARK";
    case F_JSON_ARRAY:
      return "JSON_ARRAY";
    case F_JSON_ARRAY_APPEND:
      return "JSON_ARRAY_APPEND";
    case F_JSON_ARRAY_INSERT:
      return "JSON_ARRAY_INSERT";
    case F_JSON_CONTAINS:
      return "JSON_CONTAINS";
    case F_JSON_CONTAINS_PATH:
      return "JSON_CONTAINS_PATH";
    case F_JSON_DEPTH:
      return "JSON_DEPTH";
    case F_JSON_EXTRACT:
      return "JSON_EXTRACT";
    case F_JSON_GET_ALL_PATHS:
      return "JSON_GET_ALL_PATHS";
    case F_JSON_INSERT:
      return "JSON_INSERT";
    case F_JSON_KEYS:
      return "JSON_KEYS";
    case F_JSON_LENGTH:
      return "JSON_LENGTH";
    case F_JSON_MERGE:
      return "JSON_MERGE";
    case F_JSON_MERGE_PATCH:
      return "JSON_MERGE_PATCH";
    case F_JSON_OBJECT:
      return "JSON_OBJECT";
    case F_JSON_PRETTY:
      return "JSON_PRETTY";
    case F_JSON_QUOTE:
      return "JSON_QUOTE";
    case F_JSON_REMOVE:
      return "JSON_REMOVE";
    case F_JSON_REPLACE:
      return "JSON_REPLACE";
    case F_JSON_SEARCH:
      return "JSON_SEARCH";
    case F_JSON_SET:
      return "JSON_SET";
    case F_JSON_TYPE:
      return "JSON_TYPE";
    case F_JSON_UNQUOTE:
      return "JSON_UNQUOTE";
    case F_JSON_VALID:
      return "JSON_VALID";
    case F_REGEXP_COUNT:
      return "REGEXP_COUNT";
    case F_REGEXP_INSTR:
      return "REGEXP_INSTR";
    case F_REGEXP_LIKE:
      return "REGEXP_LIKE";
    case F_REGEXP_REPLACE:
      return "REGEXP_REPLACE";
    case F_REGEXP_SUBSTR:
      return "REGEXP_SUBSTR";
    default:
      return "***UNKNOWN***";
    }
}

const char *
fcode_get_lowercase_name (FUNC_CODE ftype)
{
  switch (ftype)
    {
    case PT_MIN:
      return "min";
    case PT_MAX:
      return "max";
    case PT_SUM:
      return "sum";
    case PT_AVG:
      return "avg";
    case PT_STDDEV:
      return "stddev";
    case PT_STDDEV_POP:
      return "stddev_pop";
    case PT_STDDEV_SAMP:
      return "stddev_samp";
    case PT_VARIANCE:
      return "variance";
    case PT_VAR_POP:
      return "var_pop";
    case PT_VAR_SAMP:
      return "var_samp";
    case PT_COUNT:
      return "count";
    case PT_COUNT_STAR:
      return "count";
    case PT_CUME_DIST:
      return "cume_dist";
    case PT_PERCENT_RANK:
      return "percent_rank";
    case PT_GROUPBY_NUM:
      return "groupby_num";
    case PT_AGG_BIT_AND:
      return "bit_and";
    case PT_AGG_BIT_OR:
      return "bit_or";
    case PT_AGG_BIT_XOR:
      return "bit_xor";
    case PT_GROUP_CONCAT:
      return "group_concat";
    case PT_ROW_NUMBER:
      return "row_number";
    case PT_RANK:
      return "rank";
    case PT_DENSE_RANK:
      return "dense_rank";
    case PT_LEAD:
      return "lead";
    case PT_LAG:
      return "lag";
    case PT_NTILE:
      return "ntile";
    case PT_FIRST_VALUE:
      return "first_value";
    case PT_LAST_VALUE:
      return "last_value";
    case PT_NTH_VALUE:
      return "nth_value";
    case PT_MEDIAN:
      return "median";
    case PT_PERCENTILE_CONT:
      return "percentile_cont";
    case PT_PERCENTILE_DISC:
      return "percentile_disc";
    case PT_JSON_ARRAYAGG:
      return "json_arrayagg";
    case PT_JSON_OBJECTAGG:
      return "json_objectagg";

    case F_SEQUENCE:
      return "sequence";
    case F_SET:
      return "set";
    case F_MULTISET:
      return "multiset";

    case F_TABLE_SEQUENCE:
      return "sequence";
    case F_TABLE_SET:
      return "set";
    case F_TABLE_MULTISET:
      return "multiset";
    case F_VID:
      return "vid";		/* internally generated only, vid doesn't parse */
    case F_CLASS_OF:
      return "class";
    case F_INSERT_SUBSTRING:
      return "insert";
    case F_ELT:
      return "elt";
    case F_BENCHMARK:
      return "benchmark";
    case F_JSON_ARRAY:
      return "json_array";
    case F_JSON_ARRAY_APPEND:
      return "json_array_append";
    case F_JSON_ARRAY_INSERT:
      return "json_array_insert";
    case F_JSON_CONTAINS:
      return "json_contains";
    case F_JSON_CONTAINS_PATH:
      return "json_contains_path";
    case F_JSON_DEPTH:
      return "json_depth";
    case F_JSON_EXTRACT:
      return "json_extract";
    case F_JSON_GET_ALL_PATHS:
      return "json_get_all_paths";
    case F_JSON_INSERT:
      return "json_insert";
    case F_JSON_KEYS:
      return "json_keys";
    case F_JSON_LENGTH:
      return "json_length";
    case F_JSON_MERGE:
      return "json_merge";
    case F_JSON_MERGE_PATCH:
      return "json_merge_patch";
    case F_JSON_OBJECT:
      return "json_object";
    case F_JSON_PRETTY:
      return "json_pretty";
    case F_JSON_QUOTE:
      return "json_quote";
    case F_JSON_REMOVE:
      return "json_remove";
    case F_JSON_REPLACE:
      return "json_replace";
    case F_JSON_SEARCH:
      return "json_search";
    case F_JSON_SET:
      return "json_set";
    case F_JSON_TYPE:
      return "json_type";
    case F_JSON_UNQUOTE:
      return "json_unquote";
    case F_JSON_VALID:
      return "json_valid";
    case F_REGEXP_COUNT:
      return "regexp_count";
    case F_REGEXP_INSTR:
      return "regexp_instr";
    case F_REGEXP_LIKE:
      return "regexp_like";
    case F_REGEXP_REPLACE:
      return "regexp_replace";
    case F_REGEXP_SUBSTR:
      return "regexp_substr";
    default:
      return "unknown function";
    }
}
