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
 * func_type.cpp
 */

#include "func_type.hpp"
#include "message_catalog.h"
#include "object_primitive.h"
#include "parse_tree.h"
#include "parser.h"
#include "parser_message.h"

#include <algorithm>

//PT_TYPE_MAYBE
// - for the moment I don't see how to eliminate PT_TYPE_MAYBE from functions with multiple signature
// - with PT_TYPE_MAYBE in signature, the final type will not be decided during type checking but later
// - without PT_TYPE_MAYBE in signature you must either:
//   1. choose one signature and apply cast
//   ... but what about "prepare median(?)... execute with date'2018-06-13'... execute with 123"?
//   2. handle from code
//   ... but there are cases when the cast should be made
//   but neither one of them is OK

func_all_signatures sig_ret_int_no_arg =
{
  {PT_TYPE_INTEGER, {}, {}},
};

func_all_signatures sig_ret_int_arg_any =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_ANY}, {}},
};

func_all_signatures sig_ret_int_arg_doc =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC}, {}},
};

func_all_signatures sig_ret_int_arg_str =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_ret_bigint =
{
  {PT_TYPE_BIGINT, {}, {}},
};

func_all_signatures sig_of_percentile_cont =
{
#if 1
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
#else //use double as return type (as documentation says)... but tests are failing (adjust doc or tests)
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_DATETIME}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_NA}, {}},
#endif
};

func_all_signatures sig_of_percentile_disc =
{
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
};

func_all_signatures sig_ret_bigint_arg_discrete =
{
  {PT_TYPE_BIGINT, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_NA}, {}}, //not needed???
};

func_all_signatures sig_of_avg =
{
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
};

func_all_signatures sig_ret_double_arg_number =
{
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
};

func_all_signatures sig_of_count_star =
{
  {PT_TYPE_BIGINT, {}, {}},
};

func_all_signatures sig_of_count =
{
  {PT_TYPE_BIGINT, {PT_GENERIC_TYPE_ANY}, {}},
};

func_all_signatures sig_of_sum =
{
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_SET}, {}},
  {0, {PT_TYPE_MULTISET}, {}},
  {0, {PT_TYPE_SEQUENCE}, {}},
};

func_all_signatures sig_ret_double_arg_r_any =
{
  {PT_TYPE_DOUBLE, {}, {}},
  {PT_TYPE_DOUBLE, {}, {PT_GENERIC_TYPE_ANY}},
};

func_all_signatures sig_of_ntile =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_NUMBER}, {}}, //argument value will be truncated at execution
};

/*cannot define a clear signature because casting depends on actual value
  MEDIAN('123456')     <=> MEDIAN(double) -> double
  MEDIAN('2018-03-14') <=> MEDIAN(date)   -> date  */
func_all_signatures sig_of_median =
{
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER}, {}}, //if ret type is double => tests with median(int) will fail
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_TYPE_MAYBE}, {}}, //DISCUSSION: can we get rid of MAYBE here??? prepare median(?)...execute with date'2018-06-13'
};

func_all_signatures sig_ret_type0_arg_scalar =
{
  {0, {PT_GENERIC_TYPE_SCALAR}, {}},
};

func_all_signatures sig_ret_type0_arg_nr_or_str_discrete =
{
  {0, {PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_ENUMERATION, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
};

func_all_signatures sig_of_group_concat =
{
  {PT_TYPE_VARCHAR, {PT_TYPE_ENUMERATION, PT_GENERIC_TYPE_CHAR}, {}}, //needed because pt_are_equivalent_types(PT_GENERIC_TYPE_CHAR, PT_TYPE_ENUMERATION) and casting to VCHR will affect order
  {PT_TYPE_VARCHAR, {PT_TYPE_ENUMERATION, PT_GENERIC_TYPE_NCHAR}, {}},

//normal cases
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR}, {}},
  {PT_TYPE_VARBIT, {PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT}, {}},

#if 0 //anything else should be casted to separator's type (if possible! makes sense to detect incompatible types when detecting/applying signatures?); NOTE: casting affects the order!!!
  {PT_TYPE_VARCHAR, {1, PT_GENERIC_TYPE_CHAR  }, {}},                          //test
  {PT_TYPE_VARNCHAR, {1, PT_GENERIC_TYPE_NCHAR }, {}},                         //test
#else //anything else should be left untouched (like in the original code), maybe it will be casted later?
#if 0 //it allows group_concat(SET) but it should not!
//{PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_ANY      , PT_GENERIC_TYPE_CHAR  }, {}},
//{PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_ANY      , PT_GENERIC_TYPE_NCHAR }, {}},
#else //OK to keep the order but it allows cast (n)char -> number and it should not because group_concat(n'123', ', ') should be rejected?!
//like that it allows group_concat(n'123', ', ') or group_concat(<nchar field>, ', ') when <nchar field> can be casted to double (acceptable for me)
//but solved in preprocess for compatibility to original behaviour
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NCHAR}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_NCHAR}, {}},
#endif
#endif
};

func_all_signatures sig_of_lead_lag =
{
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {0, {PT_GENERIC_TYPE_BIT}, {}},
  {0, {PT_GENERIC_TYPE_SEQUENCE}, {}},
};

func_all_signatures sig_of_elt =
{
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARCHAR}}, //get_current_result() expects args to be VCHAR, not just equivalent
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARNCHAR}}, //get_current_result() expects args to be VNCHAR, not just equivalent
  {PT_TYPE_NULL, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
};

func_all_signatures sig_of_insert_substring =
{
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_TYPE_VARNCHAR}, {}},

  //{0, {3, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR}, {}}, //for insert(?, i, i, n'nchar')
  //{0, {3, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING}, {}}, //for insert(?, i, i, 'char or anything else')
};

func_all_signatures sig_ret_json_arg_r_jkey_jval_or_empty =
{
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
  {PT_TYPE_JSON, {}, {}}
};

func_all_signatures sig_json_arg_r_jval_or_empty =
{
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_JSON_VAL}},
  {PT_TYPE_JSON, {}, {}}
};

func_all_signatures sig_ret_json_arg_jdoc =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {}},
};

func_all_signatures sig_ret_json_arg_jdoc_r_jdoc =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_DOC}},
};

func_all_signatures sig_ret_json_arg_jdoc_jpath =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_ret_json_arg_jdoc_r_jpath =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_STRING}},
};

func_all_signatures sig_ret_json_arg_jdoc_str_r_jpath =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING}},
};

func_all_signatures sig_ret_json_arg_jdoc_r_jpath_jval =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
};

func_all_signatures sig_of_json_contains =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_JSON_DOC}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_of_json_contains_path =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING}},
};

func_all_signatures sig_of_json_keys =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {}},
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_of_json_length =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_of_json_search =
{
// all signatures: json_doc, one_or_all_str, search_str[, escape_char[, path] ... -> JSON_DOC
// first overload: json_doc, one_or_all_str, search_str:
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
// second overload: json_doc, one_or_all_str, search_str, escape_char
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
// third overload: json_doc, one_or_all_str, search_str, escape_char, path...
  {
    PT_TYPE_JSON,
    {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
    {PT_GENERIC_TYPE_STRING}
  },
};

func_all_signatures sig_of_json_arrayagg =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_VAL}, {}}
};

func_all_signatures sig_of_json_objectagg =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}, {}}
};

func_all_signatures sig_ret_set_arg_r_any =
{
  {PT_TYPE_SET, {}, {PT_GENERIC_TYPE_ANY}},
};

func_all_signatures sig_ret_multiset_arg_r_any =
{
  {PT_TYPE_MULTISET, {}, {PT_GENERIC_TYPE_ANY}},
};

func_all_signatures sig_ret_sequence_arg_r_any =
{
  {PT_TYPE_SEQUENCE, {}, {PT_GENERIC_TYPE_ANY}},
};

func_all_signatures sig_of_generic =
{
  {0, {PT_GENERIC_TYPE_ANY}, {}},
};

func_all_signatures sig_ret_string_arg_jdoc =
{
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_JSON_DOC}, {}},
};

func_all_signatures sig_ret_type0_arg_str =
{
  {0, {PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_of_benchmark =
{
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_GENERIC_TYPE_ANY}, {}},
};

func_all_signatures sig_of_regexp_count =
{
// all signatures: src, pattern [,position, [,match_type ]] -> INTEGER
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
};

func_all_signatures sig_of_regexp_instr =
{
// all signatures: src, pattern [,position [,occurrence [,return_option [,match_type ]]]] -> INTEGER
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
};

func_all_signatures sig_of_regexp_like =
{
// all signatures: src, pattern [,match_type ] -> INTEGER
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_CHAR}, {}},
};

func_all_signatures sig_of_regexp_replace =
{
// all signatures: src, pattern, replacement [,position [,occurrence [, match_type]]] -> STRING
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING}, {}},
};

func_all_signatures sig_of_regexp_substr =
{
// all signatures: src, pattern [,position [,occurrence [, match_type]]] -> STRING
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
};

func_all_signatures *
get_signatures (FUNC_TYPE ft)
{
  switch (ft)
    {
    case PT_MIN:
    case PT_MAX:
      return &sig_ret_type0_arg_scalar;
    case PT_SUM:
      return &sig_of_sum;
    case PT_AVG:
      return &sig_of_avg;
    case PT_STDDEV:
    case PT_VARIANCE:
    case PT_STDDEV_POP:
    case PT_VAR_POP:
    case PT_STDDEV_SAMP:
    case PT_VAR_SAMP:
      return &sig_ret_double_arg_number;
    case PT_COUNT:
      return &sig_of_count;
    case PT_COUNT_STAR:
      return &sig_of_count_star;
    case PT_GROUPBY_NUM:
      return &sig_ret_bigint;
    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
      return &sig_ret_bigint_arg_discrete;
    case PT_GROUP_CONCAT:
      return &sig_of_group_concat;
    case PT_ROW_NUMBER:
    case PT_RANK:
    case PT_DENSE_RANK:
      return &sig_ret_int_no_arg;
    case PT_NTILE:
      return &sig_of_ntile;
    case PT_TOP_AGG_FUNC:
      return nullptr;
    case PT_LEAD:
    case PT_LAG:
      return &sig_of_lead_lag;
    case PT_GENERIC:
      return &sig_of_generic;
    case F_SET:
    case F_TABLE_SET:
      return &sig_ret_set_arg_r_any;
    case F_MULTISET:
    case F_TABLE_MULTISET:
      return &sig_ret_multiset_arg_r_any;
    case F_SEQUENCE:
    case F_TABLE_SEQUENCE:
      return &sig_ret_sequence_arg_r_any;
    case F_TOP_TABLE_FUNC:
      return nullptr;
    case F_MIDXKEY:
      return nullptr;
    case F_VID:
    case F_GENERIC:
    case F_CLASS_OF:
      return nullptr;
    case F_INSERT_SUBSTRING:
      return &sig_of_insert_substring;
    case F_ELT:
      return &sig_of_elt;
    case F_BENCHMARK:
      return &sig_of_benchmark;
    case F_JSON_ARRAY:
      return &sig_json_arg_r_jval_or_empty;
    case F_JSON_ARRAY_APPEND:
    case F_JSON_ARRAY_INSERT:
      return &sig_ret_json_arg_jdoc_r_jpath_jval;
    case F_JSON_CONTAINS:
      return &sig_of_json_contains;
    case F_JSON_CONTAINS_PATH:
      return &sig_of_json_contains_path;
    case F_JSON_DEPTH:
      return &sig_ret_int_arg_doc;
    case F_JSON_EXTRACT:
      return &sig_ret_json_arg_jdoc_r_jpath;
    case F_JSON_GET_ALL_PATHS:
      return &sig_ret_json_arg_jdoc;
    case F_JSON_INSERT:
      return &sig_ret_json_arg_jdoc_r_jpath_jval;
    case F_JSON_KEYS:
      return &sig_of_json_keys;
    case F_JSON_LENGTH:
      return &sig_of_json_length;
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
      return &sig_ret_json_arg_jdoc_r_jdoc;
    case F_JSON_OBJECT:
      return &sig_ret_json_arg_r_jkey_jval_or_empty;
    case F_JSON_PRETTY:
      return &sig_ret_string_arg_jdoc;
    case F_JSON_QUOTE:
      return &sig_ret_type0_arg_str;
    case F_JSON_REMOVE:
      return &sig_ret_json_arg_jdoc_r_jpath;
    case F_JSON_REPLACE:
      return &sig_ret_json_arg_jdoc_r_jpath_jval;
    case F_JSON_SEARCH:
      return &sig_of_json_search;
    case F_JSON_SET:
      return &sig_ret_json_arg_jdoc_r_jpath_jval;
    case F_JSON_TYPE:
      return &sig_ret_string_arg_jdoc;
    case F_JSON_UNQUOTE:
      return &sig_ret_string_arg_jdoc;
    case F_JSON_VALID:
      return &sig_ret_int_arg_any;
    case PT_FIRST_VALUE:
    case PT_LAST_VALUE:
      return &sig_ret_type0_arg_scalar;
    case PT_NTH_VALUE:
      return &sig_ret_type0_arg_nr_or_str_discrete;
    case PT_MEDIAN:
      return &sig_of_median;
    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
      return &sig_ret_double_arg_r_any;
    case PT_PERCENTILE_CONT:
      return &sig_of_percentile_cont;
    case PT_PERCENTILE_DISC:
      return &sig_of_percentile_disc;
    case PT_JSON_ARRAYAGG:
      return &sig_of_json_arrayagg;
    case PT_JSON_OBJECTAGG:
      return &sig_of_json_objectagg;
    case F_REGEXP_COUNT:
      return &sig_of_regexp_count;
    case F_REGEXP_INSTR:
      return &sig_of_regexp_instr;
    case F_REGEXP_LIKE:
      return &sig_of_regexp_like;
    case F_REGEXP_REPLACE:
      return &sig_of_regexp_replace;
    case F_REGEXP_SUBSTR:
      return &sig_of_regexp_substr;
    default:
      assert (false);
      return nullptr;
    }
}

void
func_signature::to_string_buffer (string_buffer &sb) const
{
  bool first = true;
  for (auto fix_arg : fix)
    {
      if (first)
	{
	  first = false;
	}
      else
	{
	  sb (", ");
	}
      pt_arg_type_to_string_buffer (fix_arg, sb);
    }
  if (!rep.empty ())
    {
      if (!first)
	{
	  sb (", ");
	}
      sb ("repeat[");
      first = true;
      for (auto rep_arg : rep)
	{
	  if (first)
	    {
	      first = false;
	    }
	  else
	    {
	      sb (", ");
	    }
	  pt_arg_type_to_string_buffer (rep_arg, sb);
	}
      sb ("]");
    }
  sb (" -> ");
  pt_arg_type_to_string_buffer (ret, sb);
}

namespace func_type
{
  argument_resolve::argument_resolve ()
    : m_type (PT_TYPE_NONE)
    , m_check_coll_infer (false)
    , m_coll_infer ()
  {
    //
  }

  signature_compatibility::signature_compatibility ()
    : m_compat (type_compatibility::INCOMPATIBLE)
    , m_args_resolve {}
    , m_common_collation {}
    , m_collation_action (TP_DOMAIN_COLL_LEAVE)
    , m_signature (NULL)
  {
    //
  }

  bool
  sig_has_json_args (const func_signature &sig)
  {
    auto find_pred = [] (const pt_arg_type & arg)
    {
      return arg.type == arg.GENERIC && (arg.val.generic_type == PT_GENERIC_TYPE_JSON_DOC
					 || arg.val.generic_type == PT_GENERIC_TYPE_JSON_VAL);
    };

    auto it_found = std::find_if (sig.fix.begin (), sig.fix.end (), find_pred);
    if (it_found != sig.fix.end ())
      {
	return true;
      }

    // also search in repeateable args
    it_found = std::find_if (sig.rep.begin (), sig.rep.end (), find_pred);
    return it_found != sig.rep.end ();
  }

  bool
  is_type_with_collation (PT_TYPE_ENUM type)
  {
    return PT_HAS_COLLATION (type) || type == PT_TYPE_MAYBE;
  }

  bool
  can_signature_have_collation (const pt_arg_type &arg_sig)
  {
    switch (arg_sig.type)
      {
      case pt_arg_type::NORMAL:
	// types that can have collations
	return is_type_with_collation (arg_sig.val.type);

      case pt_arg_type::GENERIC:
	// all generic that can accept string (and result is still string)
	return arg_sig.val.generic_type == PT_GENERIC_TYPE_STRING
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_STRING_VARYING
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_CHAR
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_NCHAR
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_PRIMITIVE
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_ANY
	       || arg_sig.val.generic_type == PT_GENERIC_TYPE_SCALAR;

      case pt_arg_type::INDEX:
      default:
	assert (false);
	return false;
      }
  }

  bool
  cmp_types_equivalent (const pt_arg_type &type, pt_type_enum type_enum)
  {
    assert (type.type != pt_arg_type::INDEX);
    return type_enum == PT_TYPE_NULL || pt_are_equivalent_types (type, type_enum);
  }

  bool
  cmp_types_castable (const pt_arg_type &type, pt_type_enum type_enum) //is possible to cast type_enum -> type?
  {
    assert (type.type != pt_arg_type::INDEX);
    if (type_enum == PT_TYPE_NULL)
      {
	return true; // PT_TYPE_NULL is castable to any type
      }
    if (type_enum == PT_TYPE_MAYBE)
      {
	return true; // consider this castable, and truth will be told after late binding
      }
    if (type.type == pt_arg_type::NORMAL)
      {
	switch (type.val.type)
	  {
	  case PT_TYPE_INTEGER:
	    return (PT_IS_NUMERIC_TYPE (type_enum) || PT_IS_STRING_TYPE (type_enum));
	  case PT_TYPE_BIGINT:
	    return (PT_IS_DISCRETE_NUMBER_TYPE (type_enum));
	  case PT_TYPE_VARCHAR:
	    return (PT_IS_SIMPLE_CHAR_STRING_TYPE (type_enum) || PT_IS_NUMERIC_TYPE (type_enum)
		    || PT_IS_DATE_TIME_TYPE (type_enum) || PT_IS_BIT_STRING_TYPE (type_enum)
		    || type_enum == PT_TYPE_ENUMERATION); //monetary should be here???
	  case PT_TYPE_VARNCHAR:
	    return (PT_IS_NATIONAL_CHAR_STRING_TYPE (type_enum) || PT_IS_NUMERIC_TYPE (type_enum)
		    || PT_IS_DATE_TIME_TYPE (type_enum) || PT_IS_BIT_STRING_TYPE (type_enum)
		    || type_enum == PT_TYPE_ENUMERATION); //monetary should be here???
	  default:
	    return type.val.type == type_enum;
	  }
      }

    //type.type == pt_arg_type::GENERIC
    switch (type.val.generic_type)
      {
      case PT_GENERIC_TYPE_NUMBER:
	return (PT_IS_NUMERIC_TYPE (type_enum) || PT_IS_STRING_TYPE (type_enum) || type_enum == PT_TYPE_JSON);

      case PT_GENERIC_TYPE_DISCRETE_NUMBER:
	return (PT_IS_NUMERIC_TYPE (type_enum) || PT_IS_STRING_TYPE (type_enum));

      case PT_GENERIC_TYPE_ANY:
      case PT_GENERIC_TYPE_PRIMITIVE:
      case PT_GENERIC_TYPE_STRING:
      case PT_GENERIC_TYPE_STRING_VARYING:
      case PT_GENERIC_TYPE_BIT:
	// any non-set?
	return !PT_IS_COLLECTION_TYPE (type_enum);

      case PT_GENERIC_TYPE_CHAR:
	return (PT_IS_NUMERIC_TYPE (type_enum) || PT_IS_SIMPLE_CHAR_STRING_TYPE (type_enum)
		|| PT_IS_DATE_TIME_TYPE (type_enum) || type_enum == PT_TYPE_JSON);

      case PT_GENERIC_TYPE_NCHAR:
	return (PT_IS_NUMERIC_TYPE (type_enum) || PT_IS_NATIONAL_CHAR_STRING_TYPE (type_enum)
		|| PT_IS_DATE_TIME_TYPE (type_enum) || type_enum == PT_TYPE_JSON);

      case PT_GENERIC_TYPE_DATE:
      case PT_GENERIC_TYPE_DATETIME:
	return PT_IS_STRING_TYPE (type_enum) || PT_IS_DATE_TIME_TYPE (type_enum);

      case PT_GENERIC_TYPE_SCALAR:
	return !PT_IS_COLLECTION_TYPE (type_enum);

      case PT_GENERIC_TYPE_JSON_VAL:
	// it will be resolved at runtime
	return PT_IS_NUMERIC_TYPE (type_enum);      // numerics can be converted to a json value

      case PT_GENERIC_TYPE_JSON_DOC:
	return false;     // only equivalent types

      case PT_GENERIC_TYPE_SEQUENCE:
	// todo -
	return false;

      case PT_GENERIC_TYPE_LOB:
	// todo -
	return false;

      case PT_GENERIC_TYPE_QUERY:
	// ??
	assert (false);
	return false;

      default:
	assert (false);
	return false;
      }
  }

  parser_node *
  Node::get_arg (size_t index)
  {
    for (auto arg = m_node->info.function.arg_list; arg; arg = arg->next, --index)
      {
	if (index == 0)
	  {
	    return arg;
	  }
      }
    return NULL;
  }

  parser_node *
  Node::apply_argument (parser_node *prev, parser_node *arg, const argument_resolve &arg_res)
  {
    parser_node *save_next = arg->next;

    if (arg_res.m_type != arg->type_enum)
      {
	arg = pt_wrap_with_cast_op (m_parser, arg, arg_res.m_type, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	if (arg == NULL)
	  {
	    assert (false);
	    return NULL;
	  }
      }
    if (m_best_signature.m_common_collation.coll_id != -1 && arg_res.m_check_coll_infer)
      {
	if (m_best_signature.m_common_collation.coll_id != arg_res.m_coll_infer.coll_id)
	  {
	    PT_NODE *new_node = pt_coerce_node_collation (m_parser, arg, m_best_signature.m_common_collation.coll_id,
				m_best_signature.m_common_collation.codeset,
				arg_res.m_coll_infer.can_force_cs, false, arg_res.m_type,
				PT_TYPE_NONE);
	    if (new_node != NULL)
	      {
		arg = new_node;
	      }
	    else
	      {
		// what if it is null?
	      }
	  }
      }

    // restore links
    if (prev != NULL)
      {
	prev->next = arg;
      }
    else
      {
	m_node->info.function.arg_list = arg;
      }
    arg->next = save_next;

    return arg;
  }

  PT_NODE *
  Node::type_checking ()
  {
    if (m_node->info.function.is_type_checked)
      {
	// already checked
	return m_node;
      }

    if (preprocess ())
      {
	auto func_sigs = get_signatures (m_node->info.function.function_type);
	assert ("ERR no function signature" && func_sigs != NULL);
	if (!func_sigs)
	  {
	    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NO_SIGNATURES,
			  fcode_get_uppercase_name (m_node->info.function.function_type));
	    return m_node;
	  }
	const func_signature *func_sig = get_signature (*func_sigs);
	if (func_sig == NULL || !apply_signature (*func_sig))
	  {
	    m_node->type_enum = PT_TYPE_NA;
	    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NO_VALID_FUNCTION_SIGNATURE,
			  fcode_get_uppercase_name (m_node->info.function.function_type));
	  }
	else
	  {
	    set_return_type (*func_sig);
	  }
      }
    else
      {
	// do nothing??
      }
    m_node->info.function.is_type_checked = true;
    return m_node;
  }

  bool
  Node::preprocess ()
  {
    auto arg_list = m_node->info.function.arg_list;
    switch (m_node->info.function.function_type)
      {
      case F_GENERIC:
      case F_CLASS_OF: //move it to the beginning of pt_eval_function_type() ... not without complicating the code
	m_node->type_enum = (arg_list) ? arg_list->type_enum : PT_TYPE_NONE;
	return false; //no need to continue with generic code
      case PT_GROUP_CONCAT: //ToDo: try without this!
      {
	auto arg1 = m_node->info.function.arg_list;
	if (arg1 != NULL)
	  {
	    auto arg2 = arg1->next;
	    if (arg2 != NULL)
	      {
		if ((PT_IS_SIMPLE_CHAR_STRING_TYPE (arg1->type_enum) && PT_IS_NATIONAL_CHAR_STRING_TYPE (arg2->type_enum))
		    || (PT_IS_SIMPLE_CHAR_STRING_TYPE (arg2->type_enum)
			&& PT_IS_NATIONAL_CHAR_STRING_TYPE (arg1->type_enum)))
		  {
		    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON,
				  fcode_get_lowercase_name (PT_GROUP_CONCAT), pt_show_type_enum (arg1->type_enum),
				  pt_show_type_enum (arg2->type_enum));
		    m_node->type_enum = PT_TYPE_VARCHAR;
		    return false;
		  }
	      }
	  }
	break;
      }
      default:
	;
      }
    return true;
  }

  const char *
  Node::get_types (const func_all_signatures &signatures, size_t index, string_buffer &sb)
  {
    for (auto &signature: signatures)
      {
	auto i = index;
	if (index < signature.fix.size ())
	  {
	    pt_arg_type_to_string_buffer (signature.fix[i], sb);
	    sb (", ");
	  }
	else
	  {
	    i -= signature.fix.size ();
	    if (signature.rep.size () > 0)
	      {
		i %= signature.rep.size ();
		pt_arg_type_to_string_buffer (signature.rep[i], sb);
		sb (", ");
	      }
	  }
      }
    return sb.get_buffer ();
  }

  const func_signature *
  Node::get_signature (const func_all_signatures &signatures)
  {
    if (pt_has_error (m_parser))
      {
	return nullptr;
      }
    pt_reset_error (m_parser);

    size_t arg_count = static_cast<size_t> (pt_length_of_list (m_node->info.function.arg_list));
    signature_compatibility sgn_compat;

    int sigIndex = 0;
    for (auto &sig: signatures)
      {
	++sigIndex;
	parser_node *arg = m_node->info.function.arg_list;
	size_t arg_idx = 0;

	sgn_compat.m_args_resolve.resize (arg_count);
	sgn_compat.m_compat = type_compatibility::EQUIVALENT;
	sgn_compat.m_signature = &sig;
	// collation action is initialized as leave. if string-signature arguments are all maybes, then it will remain
	// leave, and is decided at runtime. if any argument is string, it will be set to TP_DOMAIN_COLL_NORMAL.
	sgn_compat.m_collation_action = TP_DOMAIN_COLL_LEAVE;

	bool coerce_args_utf8 = sig_has_json_args (sig);

	//check fix part of the signature
	for (auto &fix: sig.fix)
	  {
	    if (arg == NULL)
	      {
		invalid_arg_count_error (arg_count, sig);
		sgn_compat.m_compat = type_compatibility::INCOMPATIBLE;
		break;
	      }

	    // todo - index type signature should copy argument type, not argument signature
	    auto t = ((fix.type == pt_arg_type::INDEX) ? sig.fix[fix.val.index] : fix);

	    if (!check_arg_compat (t, arg, sgn_compat, sgn_compat.m_args_resolve[arg_idx], coerce_args_utf8))
	      {
		break;
	      }

	    ++arg_idx;
	    arg = arg->next;
	  }
	if (sgn_compat.m_compat == type_compatibility::INCOMPATIBLE)
	  {
	    continue;
	  }
	if ((arg != NULL && sig.rep.size () == 0) || (arg == NULL && sig.rep.size () != 0))
	  {
	    // number of arguments don't match
	    invalid_arg_count_error (arg_count, sig);
	    continue;
	  }

	//check repetitive args
	int index = 0;
	for (; arg; arg = arg->next, index = (index + 1) % sig.rep.size ())
	  {
	    auto &rep = sig.rep[index];
	    // todo - index type signature should copy argument type, not argument signature
	    auto t = ((rep.type == pt_arg_type::INDEX) ? sig.rep[rep.val.index] : rep);

	    if (!check_arg_compat (t, arg, sgn_compat, sgn_compat.m_args_resolve[arg_idx], coerce_args_utf8))
	      {
		break;
	      }
	    ++arg_idx;
	  }
	if (index != 0 && sgn_compat.m_compat != type_compatibility::INCOMPATIBLE)
	  {
	    invalid_arg_count_error (arg_count, sig);
	    sgn_compat.m_compat = type_compatibility::INCOMPATIBLE;
	    continue;
	  }

	if (sgn_compat.m_compat == type_compatibility::EQUIVALENT)
	  {
	    m_best_signature = std::move (sgn_compat);
	    break; //stop at 1st equivalent signature
	  }
	if (sgn_compat.m_compat == type_compatibility::COERCIBLE && m_best_signature.m_signature == NULL)
	  {
	    //don't stop, continue because it is possible to find an equivalent signature later
	    m_best_signature = sgn_compat;
	  }
      }
    if (m_best_signature.m_signature != NULL)
      {
	// signature found => clear error messages accumulated during signature checking
	pt_reset_error (m_parser);
      }
    return m_best_signature.m_signature;
  }

  void
  Node::set_return_type (const func_signature &signature)
  {
    parser_node *arg_list = m_node->info.function.arg_list;
    if (m_node->type_enum == PT_TYPE_NONE || m_node->data_type == NULL) //return type
      {
	// todo - make this really generic

	//set node->type_enum
	switch (signature.ret.type)
	  {
	  case pt_arg_type::NORMAL:
	    m_node->type_enum = signature.ret.val.type;
	    break;
	  case pt_arg_type::GENERIC:
	    assert (false);
	    break;
	  case pt_arg_type::INDEX:
	  {
	    parser_node *arg_node = get_arg (signature.ret.val.index);
	    if (arg_node != NULL)
	      {
		m_node->type_enum = arg_node->type_enum;
		if (m_node->type_enum == PT_TYPE_MAYBE && arg_node->expected_domain != NULL)
		  {
		    m_node->type_enum = pt_db_to_type_enum (arg_node->expected_domain->type->id);
		  }
		if (arg_node->data_type != NULL)
		  {
		    m_node->data_type = parser_copy_tree_list (m_parser, arg_node->data_type);
		  }
	      }
	    else
	      {
		// ??
		assert (false);
	      }
	    break;
	  }
	  }
	// set node->data_type
	// todo - remove this switch
	switch (m_node->info.function.function_type)
	  {
	  case PT_MAX:
	  case PT_MIN:
	  case PT_LEAD:
	  case PT_LAG:
	  case PT_FIRST_VALUE:
	  case PT_LAST_VALUE:
	  case PT_NTH_VALUE:
	    m_node->data_type = (arg_list ? parser_copy_tree_list (m_parser, arg_list->data_type) : NULL);
	    break;
	  case PT_SUM:
	    m_node->data_type = (arg_list ? parser_copy_tree_list (m_parser, arg_list->data_type) : NULL);
	    if (arg_list && arg_list->type_enum == PT_TYPE_NUMERIC && m_node->data_type)
	      {
		m_node->data_type->info.data_type.precision = DB_MAX_NUMERIC_PRECISION;
	      }
	    break;
	  case F_ELT:
	    m_node->data_type = pt_make_prim_data_type (m_parser, m_node->type_enum);
	    if (m_node->data_type)
	      {
		m_node->data_type->info.data_type.precision =
			(m_node->type_enum == PT_TYPE_VARNCHAR ? DB_MAX_VARNCHAR_PRECISION : DB_MAX_VARCHAR_PRECISION);
		m_node->data_type->info.data_type.dec_precision = 0;
	      }
	    break;
	  case PT_GROUP_CONCAT:
	  case F_INSERT_SUBSTRING:
	    m_node->data_type = pt_make_prim_data_type (m_parser, m_node->type_enum);
	    if (m_node->data_type)
	      {
		m_node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	      }
	    break;
	  case F_SET:
	  case F_MULTISET:
	  case F_SEQUENCE:
	    pt_add_type_to_set (m_parser, arg_list, &m_node->data_type);
	    break;
	  case F_TABLE_SET:
	  case F_TABLE_MULTISET:
	  case F_TABLE_SEQUENCE:
	    pt_add_type_to_set (m_parser, pt_get_select_list (m_parser, arg_list), &m_node->data_type);
	    break;
	  default:
	    // m_node->data_type = NULL;
	    break;
	  }

	if (m_node->data_type == NULL)
	  {
	    m_node->data_type = pt_make_prim_data_type (m_parser, m_node->type_enum);
	  }
	if (m_node->data_type != NULL && PT_IS_STRING_TYPE (m_node->type_enum))
	  {
	    // always return string without precision
	    m_node->type_enum = pt_to_variable_size_type (m_node->type_enum);
	    m_node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    m_node->data_type->type_enum = pt_to_variable_size_type (m_node->type_enum);
	    assert (m_node->type_enum == m_node->data_type->type_enum);
	  }
      }
    else
      {
	// when is this already set??
      }

    // set collation on result node... I am not sure this is correct
    if (PT_HAS_COLLATION (m_node->type_enum) && m_best_signature.m_common_collation.coll_id != -1)
      {
	pt_coll_infer result_coll_infer;
	if (is_type_with_collation (m_node->type_enum) && m_best_signature.m_collation_action == TP_DOMAIN_COLL_LEAVE
	    && m_node->data_type != NULL)
	  {
	    // all maybes case. leave collation coming from arguments
	    m_node->data_type->info.data_type.collation_flag = TP_DOMAIN_COLL_LEAVE;
	  }
	else if (pt_get_collation_info (m_node, &result_coll_infer)
		 && m_best_signature.m_common_collation.coll_id != result_coll_infer.coll_id)
	  {
	    parser_node *new_node = pt_coerce_node_collation (m_parser, m_node,
				    m_best_signature.m_common_collation.coll_id,
				    m_best_signature.m_common_collation.codeset,
				    true, false, PT_TYPE_VARCHAR, PT_TYPE_NONE);
	    if (new_node != NULL)
	      {
		m_node = new_node;
	      }
	  }
      }
  }

  bool
  Node::apply_signature (const func_signature &signature)
  {
    FUNC_TYPE func_type = m_node->info.function.function_type;
    parser_node *arg = m_node->info.function.arg_list;
    parser_node *prev = NULL;
    size_t arg_pos = 0;

    // check fixed part of the function signature
    for (auto type: signature.fix)
      {
	if (arg == NULL)
	  {
	    assert (false);
	    return false;
	  }

	arg = apply_argument (prev, arg, m_best_signature.m_args_resolve[arg_pos]);
	if (arg == NULL)
	  {
	    assert (false);
	    return false;
	  }

	++arg_pos;
	prev = arg;
	arg = arg->next;
      }

    if (arg != NULL && signature.rep.size () == 0)
      {
	assert (false);
	return false;
      }

    //check repetitive part of the function signature
    int index = 0;
    for (; arg != NULL; prev = arg, arg = arg->next, index = (index + 1) % signature.rep.size (), ++arg_pos)
      {
	arg = apply_argument (prev, arg, m_best_signature.m_args_resolve[arg_pos]);
	if (arg == NULL)
	  {
	    assert (false);
	    return false;
	  }
      }
    if (index != 0)
      {
	assert (false);
	return false;
      }

    return true;
  }

  bool
  Node::check_arg_compat (const pt_arg_type &arg_signature, const PT_NODE *arg_node,
			  signature_compatibility &compat, argument_resolve &arg_res, bool string_args_to_utf8)
  {
    arg_res.m_type = PT_TYPE_NONE;

    // todo - equivalent type & coercible type checks should all be in a the same place to have a better view of how
    //        each type can convert to another

    if (cmp_types_equivalent (arg_signature, arg_node->type_enum))
      {
	arg_res.m_type = pt_get_equivalent_type (arg_signature, arg_node->type_enum);
      }
    else if (cmp_types_castable (arg_signature, arg_node->type_enum))
      {
	compat.m_compat = type_compatibility::COERCIBLE;
	arg_res.m_type = pt_get_equivalent_type (arg_signature, arg_node->type_enum);
      }
    else
      {
	compat.m_compat = type_compatibility::INCOMPATIBLE;
	invalid_arg_error (arg_signature, arg_node, *compat.m_signature);
	return false;
      }

    // if compatible, pt_get_equivalent_type should return a valid type. but we need to double-check
    if (arg_res.m_type == PT_TYPE_NONE)
      {
	assert (false);
	compat.m_compat = type_compatibility::INCOMPATIBLE;
	invalid_arg_error (arg_signature, arg_node, *compat.m_signature);
	return false;
      }

    // three conditions should be met to require collation inference:
    //
    //  1. argument signature should allow collation. e.g. all generic strings allow collations, but json docs and values
    //     don't allow
    //
    //  2. equivalent type should have collation.
    //
    //  3. original argument type should have collation. if it doesn't have, it doesn't affect common collation.
    //     NOTE - if first two conditions are true and this is false, we don't do collation inference, but argument will
    //            be coerced to common collation.
    //
    // todo - what happens when all arguments are maybe??
    //
    // NOTE:
    //  Most of the time, first and second conditions are similar. There are cases when first condition is false and
    //  second is true. I don't know at this moment if second argument can be false while first is true. To be on the
    //  safe side, we check them both.
    //
    if (!can_signature_have_collation (arg_signature) || !is_type_with_collation (arg_res.m_type))
      {
	// collation does not matter for this argument
	arg_res.m_coll_infer.coll_id = -1;
	arg_res.m_check_coll_infer = false;
      }
    else
      {
	// collation matters for this argument
	if (string_args_to_utf8)
	  {
	    compat.m_common_collation.coll_id = LANG_COLL_UTF8_BINARY;
	    compat.m_common_collation.codeset = INTL_CODESET_UTF8;
	    compat.m_common_collation.can_force_cs = true;
	    compat.m_common_collation.coerc_level = PT_COLLATION_FULLY_COERC;
	    return true;
	  }

	arg_res.m_coll_infer.coll_id = -1;
	arg_res.m_check_coll_infer = true;
	if (is_type_with_collation (arg_node->type_enum) && pt_get_collation_info (arg_node, &arg_res.m_coll_infer))
	  {
	    // do collation inference
	    int common_coll;
	    INTL_CODESET common_cs;
	    if (compat.m_common_collation.coll_id == -1)
	      {
		compat.m_common_collation.coll_id = arg_res.m_coll_infer.coll_id;
		compat.m_common_collation.codeset = arg_res.m_coll_infer.codeset;
		compat.m_common_collation.can_force_cs = arg_res.m_coll_infer.can_force_cs;
		compat.m_common_collation.coerc_level = arg_res.m_coll_infer.coerc_level;
	      }
	    else if (pt_common_collation (&compat.m_common_collation, &arg_res.m_coll_infer, NULL, 2, false,
					  &common_coll, &common_cs) == NO_ERROR)
	      {
		compat.m_common_collation.coll_id = common_coll;
		compat.m_common_collation.codeset = common_cs;
	      }
	    else
	      {
		// todo: we'll need a clear error here
		compat.m_compat = type_compatibility::INCOMPATIBLE;
		invalid_coll_error (*compat.m_signature);
		return false;
	      }

	    if (arg_node->type_enum != PT_TYPE_MAYBE)
	      {
		compat.m_collation_action = TP_DOMAIN_COLL_NORMAL;
	      }
	  }
	else
	  {
	    // third condition is not met; this argument does not contribute to common collation.
	  }
      }
    return true;
  }

  void
  Node::invalid_arg_error (const pt_arg_type &arg_sgn, const PT_NODE *arg_node, const func_signature &func_sgn)
  {
    string_buffer expected_sb;
    string_buffer sgn_sb;

    pt_arg_type_to_string_buffer (arg_sgn, expected_sb);
    func_sgn.to_string_buffer (sgn_sb);

    pt_cat_error (m_parser, arg_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_ARGUMENT_TYPE,
		  pt_show_type_enum (arg_node->type_enum), expected_sb.get_buffer ());
    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_SIGNATURE,
		  sgn_sb.get_buffer ());
  }

  void
  Node::invalid_coll_error (const func_signature &func_sgn)
  {
    string_buffer sgn_sb;
    func_sgn.to_string_buffer (sgn_sb);

    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATION_OP_ERROR,
		  fcode_get_lowercase_name (m_node->info.function.function_type));
    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_SIGNATURE,
		  sgn_sb.get_buffer ());
  }

  void
  Node::invalid_arg_count_error (std::size_t arg_count, const func_signature &func_sgn)
  {
    string_buffer sgn_sb;
    func_sgn.to_string_buffer (sgn_sb);

    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_WRONG_ARGS_COUNT,
		  (int) arg_count);
    pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_SIGNATURE,
		  sgn_sb.get_buffer ());
  }
} // namespace func_type

/*
 * pt_are_equivalent_types () - check if a node type is equivalent with a
 *				definition type
 * return	: true if the types are equivalent, false otherwise
 * def_type(in)	: the definition type
 * op_type(in)	: argument type
 */
bool
pt_are_equivalent_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type)
{
  if (def_type.type == pt_arg_type::NORMAL)
    {
      if (def_type.val.type == op_type && op_type == PT_TYPE_NONE)
	{
	  /* return false if both arguments are of type none */
	  return false;
	}
      if (def_type.val.type == op_type)
	{
	  /* return true if both have the same type */
	  return true;
	}
      /* if def_type is a PT_TYPE_ENUM and the conditions above did not hold then the two types are not equivalent. */
      return false;
    }

  switch (def_type.val.generic_type)
    {
    case PT_GENERIC_TYPE_ANY:
      /* PT_GENERIC_TYPE_ANY is equivalent to any type */
      return true;
    case PT_GENERIC_TYPE_PRIMITIVE:
      if (PT_IS_PRIMITIVE_TYPE (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_DISCRETE_NUMBER:
      if (PT_IS_DISCRETE_NUMBER_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* PT_GENERIC_TYPE_DISCRETE_NUMBER is equivalent with SHORT, INTEGER and BIGINT */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_NUMBER:
      if (PT_IS_NUMERIC_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* any NUMBER type is equivalent with PT_GENERIC_TYPE_NUMBER */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_STRING:
      if (PT_IS_CHAR_STRING_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* any STRING type is equivalent with PT_GENERIC_TYPE_STRING */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_CHAR:
      if (op_type == PT_TYPE_CHAR || op_type == PT_TYPE_VARCHAR || op_type == PT_TYPE_ENUMERATION)
	{
	  /* CHAR and VARCHAR are equivalent to PT_GENERIC_TYPE_CHAR */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_NCHAR:
      if (op_type == PT_TYPE_NCHAR || op_type == PT_TYPE_VARNCHAR)
	{
	  /* NCHAR and VARNCHAR are equivalent to PT_GENERIC_TYPE_NCHAR */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_BIT:
      if (PT_IS_BIT_STRING_TYPE (op_type))
	{
	  /* BIT and BIT VARYING are equivalent to PT_GENERIC_TYPE_BIT */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_DATETIME:
      if (PT_IS_DATE_TIME_TYPE (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_DATE:
      if (PT_HAS_DATE_PART (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_SEQUENCE:
      if (PT_IS_COLLECTION_TYPE (op_type))
	{
	  /* any COLLECTION is equivalent with PT_GENERIC_TYPE_SEQUENCE */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_JSON_VAL:
      return pt_is_json_value_type (op_type);

    case PT_GENERIC_TYPE_JSON_DOC:
      return pt_is_json_doc_type (op_type);

    case PT_GENERIC_TYPE_SCALAR:
      return ((op_type == PT_TYPE_ENUMERATION) || PT_IS_NUMERIC_TYPE (op_type) || PT_IS_STRING_TYPE (op_type)
	      || PT_IS_DATE_TIME_TYPE (op_type));

    default:
      return false;
    }

  return false;
}

/*
 * pt_get_equivalent_type () - get the type to which a node should be
 *			       converted to in order to match an expression
 *			       definition
 *   return	  : the new type
 *   def_type(in) : the type defined in the expression signature
 *   arg_type(in) : the type of the received expression argument
 */
PT_TYPE_ENUM
pt_get_equivalent_type (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM arg_type)
{
  if (arg_type == PT_TYPE_NULL || arg_type == PT_TYPE_NONE)
    {
      /* either the argument is null or not defined */
      return arg_type;
    }

  if (def_type.type != pt_arg_type::GENERIC)
    {
      /* if the definition does not have a generic type, return the definition type */
      return def_type.val.type;
    }

  /* In some cases that involve ENUM (e.g. bit_length function) we need to convert ENUM to the other type even if the
   * types are equivalent */
  if (pt_are_equivalent_types (def_type, arg_type) && arg_type != PT_TYPE_ENUMERATION)
    {
      /* def_type includes type */
      if (arg_type == PT_TYPE_LOGICAL)
	{
	  /* def_type is a generic type and even though logical type might be equivalent with the generic definition,
	   * we are sure that we don't want it to be logical here */
	  return PT_TYPE_INTEGER;
	}
      return arg_type;
    }

  /* At this point we do not have a clear match. We will return the "largest" type for the generic type defined in the
   * expression signature */
  switch (def_type.val.generic_type)
    {
    case PT_GENERIC_TYPE_ANY:
      if (arg_type == PT_TYPE_LOGICAL)
	{
	  /* if PT_TYPE_LOGICAL appears for a PT_GENERIC_TYPE_ANY, it should be converted to PT_TYPE_INTEGER. */
	  return PT_TYPE_INTEGER;
	}
      return arg_type;

    case PT_GENERIC_TYPE_PRIMITIVE:
      if (PT_IS_PRIMITIVE_TYPE (arg_type))
	{
	  return arg_type;
	}
      break;

    case PT_GENERIC_TYPE_LOB:
      if (PT_IS_LOB_TYPE (arg_type))
	{
	  return arg_type;
	}
      break;

    case PT_GENERIC_TYPE_DISCRETE_NUMBER:
      return PT_TYPE_BIGINT;

    case PT_GENERIC_TYPE_NUMBER:
      if (arg_type == PT_TYPE_ENUMERATION)
	{
	  return PT_TYPE_SMALLINT;
	}
      return PT_TYPE_DOUBLE;

    case PT_GENERIC_TYPE_CHAR:
    case PT_GENERIC_TYPE_STRING:
    case PT_GENERIC_TYPE_STRING_VARYING:
      return PT_TYPE_VARCHAR;

    case PT_GENERIC_TYPE_NCHAR:
      return PT_TYPE_VARNCHAR;

    case PT_GENERIC_TYPE_BIT:
      return PT_TYPE_VARBIT;

    case PT_GENERIC_TYPE_DATE:
      return PT_TYPE_DATETIME;

    case PT_GENERIC_TYPE_SCALAR:
      if (arg_type == PT_TYPE_ENUMERATION || arg_type == PT_TYPE_MAYBE || PT_IS_NUMERIC_TYPE (arg_type)
	  || PT_IS_STRING_TYPE (arg_type) || PT_IS_DATE_TIME_TYPE (arg_type))
	{
	  return arg_type;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    case PT_GENERIC_TYPE_JSON_VAL:
      if (pt_is_json_value_type (arg_type))
	{
	  return arg_type;
	}
      else if (PT_IS_NUMERIC_TYPE (arg_type))
	{
	  return PT_TYPE_JSON;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    case PT_GENERIC_TYPE_JSON_DOC:
      if (pt_is_json_doc_type (arg_type))
	{
	  return arg_type;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    default:
      return PT_TYPE_NONE;
    }

  return PT_TYPE_NONE;
}
