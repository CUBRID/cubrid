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
get_signatures (FUNC_CODE ft)
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
    FUNC_CODE func_type = m_node->info.function.function_type;
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

// TODO: remove me
#define PT_COLL_WRAP_TYPE_FOR_MAYBE(type) \
  ((PT_IS_CHAR_STRING_TYPE (type)) ? (type) : PT_TYPE_VARCHAR)

#define PT_IS_CAST_MAYBE(node) \
  ((node)->node_type == PT_EXPR && (node)->info.expr.op == PT_CAST \
   && (node)->info.expr.arg1 != NULL && (node)->info.expr.arg1->type_enum == PT_TYPE_MAYBE)

static PT_NODE *pt_check_function_collation (PARSER_CONTEXT *parser, PT_NODE *node);
static int pt_character_length_for_node (PT_NODE *node, const PT_TYPE_ENUM coerce_type);

/*
 * pt_eval_function_type () -
 *   return: returns a node of the same type.
 *   parser(in): parser global context info for reentrancy
 *   node(in): a parse tree node of type PT_FUNCTION denoting an
 *             an expression with aggregate functions.
 *
 * TODO - remove me when all functions are migrated to new evaluation
 * GIVEUP - just moved from type_check.c to type_eval_func.cpp
 */
PT_NODE *
pt_eval_function_type_aggregate (PARSER_CONTEXT *parser, PT_NODE *node)
{
  PT_NODE *arg_list;
  PT_TYPE_ENUM arg_type;
  FUNC_CODE fcode;
  bool check_agg_single_arg = false;
  bool is_agg_function = false;
  PT_NODE *prev = NULL;
  PT_NODE *arg = NULL;

  is_agg_function = pt_is_aggregate_function (parser, node);
  arg_list = node->info.function.arg_list;
  fcode = node->info.function.function_type;

  /*
   * Should only get one arg to function; set to 0 if the function
   * accepts more than one.
   */
  check_agg_single_arg = true;
  arg_type = (arg_list) ? arg_list->type_enum : PT_TYPE_NONE;

  switch (fcode)
    {
    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
    case PT_AVG:
      if (arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL && arg_type != PT_TYPE_NA)
	{
	  if (!PT_IS_NUMERIC_TYPE (arg_type))
	    {
	      arg_list = pt_wrap_with_cast_op (parser, arg_list, PT_TYPE_DOUBLE, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	      if (arg_list == NULL)
		{
		  return node;
		}
	    }
	  node->info.function.arg_list = arg_list;
	}

      arg_type = PT_TYPE_DOUBLE;
      break;

    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
      if (!PT_IS_DISCRETE_NUMBER_TYPE (arg_type) && arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL
	  && arg_type != PT_TYPE_NA)
	{
	  /* cast arg_list to bigint */
	  arg_list = pt_wrap_with_cast_op (parser, arg_list, PT_TYPE_BIGINT, 0, 0, NULL);
	  if (arg_list == NULL)
	    {
	      return node;
	    }
	  arg_type = PT_TYPE_BIGINT;
	  node->info.function.arg_list = arg_list;
	}
      break;

    case PT_SUM:
      if (!PT_IS_NUMERIC_TYPE (arg_type) && arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL
	  && arg_type != PT_TYPE_NA && !pt_is_set_type (arg_list))
	{
	  /* To display the sum as integer and not scientific */
	  PT_TYPE_ENUM cast_type = (arg_type == PT_TYPE_ENUMERATION ? PT_TYPE_INTEGER : PT_TYPE_DOUBLE);

	  /* cast arg_list to double or integer */
	  arg_list = pt_wrap_with_cast_op (parser, arg_list, cast_type, 0, 0, NULL);
	  if (arg_list == NULL)
	    {
	      return node;
	    }
	  arg_type = cast_type;
	  node->info.function.arg_list = arg_list;
	}
      break;

    case PT_MAX:
    case PT_MIN:
    case PT_FIRST_VALUE:
    case PT_LAST_VALUE:
    case PT_NTH_VALUE:
      if (!PT_IS_NUMERIC_TYPE (arg_type) && !PT_IS_STRING_TYPE (arg_type) && !PT_IS_DATE_TIME_TYPE (arg_type)
	  && arg_type != PT_TYPE_ENUMERATION && arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL
	  && arg_type != PT_TYPE_NA)
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_OPDS,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type));
	}
      break;

    case PT_LEAD:
    case PT_LAG:
      break;

    case PT_COUNT:
      assert (false);
      break;

    case PT_GROUP_CONCAT:
    {
      PT_TYPE_ENUM sep_type;
      sep_type = (arg_list->next) ? arg_list->next->type_enum : PT_TYPE_NONE;
      check_agg_single_arg = false;

      if (!PT_IS_NUMERIC_TYPE (arg_type) && !PT_IS_STRING_TYPE (arg_type) && !PT_IS_DATE_TIME_TYPE (arg_type)
	  && arg_type != PT_TYPE_ENUMERATION && arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL
	  && arg_type != PT_TYPE_NA)
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_OPDS,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type));
	  break;
	}

      if (!PT_IS_STRING_TYPE (sep_type) && sep_type != PT_TYPE_NONE)
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_OPDS,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (sep_type));
	  break;
	}

      if ((sep_type == PT_TYPE_NCHAR || sep_type == PT_TYPE_VARNCHAR) && arg_type != PT_TYPE_NCHAR
	  && arg_type != PT_TYPE_VARNCHAR)
	{
	  PT_ERRORmf3 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type), pt_show_type_enum (sep_type));
	  break;
	}

      if ((arg_type == PT_TYPE_NCHAR || arg_type == PT_TYPE_VARNCHAR) && sep_type != PT_TYPE_NCHAR
	  && sep_type != PT_TYPE_VARNCHAR && sep_type != PT_TYPE_NONE)
	{
	  PT_ERRORmf3 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type), pt_show_type_enum (sep_type));
	  break;
	}

      if ((arg_type == PT_TYPE_BIT || arg_type == PT_TYPE_VARBIT) && sep_type != PT_TYPE_BIT
	  && sep_type != PT_TYPE_VARBIT && sep_type != PT_TYPE_NONE)
	{
	  PT_ERRORmf3 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type), pt_show_type_enum (sep_type));
	  break;
	}

      if ((arg_type != PT_TYPE_BIT && arg_type != PT_TYPE_VARBIT)
	  && (sep_type == PT_TYPE_BIT || sep_type == PT_TYPE_VARBIT))
	{
	  PT_ERRORmf3 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type), pt_show_type_enum (sep_type));
	  break;
	}
    }
    break;

    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
      check_agg_single_arg = false;
      break;

    case PT_NTILE:
      if (!PT_IS_DISCRETE_NUMBER_TYPE (arg_type) && arg_type != PT_TYPE_MAYBE && arg_type != PT_TYPE_NULL
	  && arg_type != PT_TYPE_NA)
	{
	  /* cast arg_list to double */
	  arg_list = pt_wrap_with_cast_op (parser, arg_list, PT_TYPE_DOUBLE, 0, 0, NULL);
	  if (arg_list == NULL)
	    {
	      return node;
	    }

	  arg_type = PT_TYPE_INTEGER;
	  node->info.function.arg_list = arg_list;
	}
      break;

    case F_ELT:
    {
      /* all types used in the arguments list */
      bool has_arg_type[PT_TYPE_MAX - PT_TYPE_MIN] = { false };

      /* a subset of argument types given to ELT that can not be cast to [N]CHAR VARYING */
      PT_TYPE_ENUM bad_types[4] =
      {
	PT_TYPE_NONE, PT_TYPE_NONE, PT_TYPE_NONE, PT_TYPE_NONE
      };

      PT_NODE *arg = arg_list;

      size_t i = 0;		/* used to index has_arg_type */
      size_t num_bad = 0;	/* used to index bad_types */

      memset (has_arg_type, 0, sizeof (has_arg_type));

      /* check the index argument (null, numeric or host var) */
      if (PT_IS_NUMERIC_TYPE (arg->type_enum) || PT_IS_CHAR_STRING_TYPE (arg->type_enum)
	  || arg->type_enum == PT_TYPE_NONE || arg->type_enum == PT_TYPE_NA || arg->type_enum == PT_TYPE_NULL
	  || arg->type_enum == PT_TYPE_MAYBE)
	{
	  arg = arg->next;
	}
      else
	{
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_INDEX,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg->type_enum));
	  break;
	}

      /* make a list of all other argument types (null, [N]CHAR [VARYING], or host var) */
      while (arg)
	{
	  if (arg->type_enum < PT_TYPE_MAX)
	    {
	      has_arg_type[arg->type_enum - PT_TYPE_MIN] = true;
	      arg = arg->next;
	    }
	  else
	    {
	      assert (false);	/* invalid data type */
	      arg_type = PT_TYPE_NONE;
	      PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON,
			   fcode_get_lowercase_name (fcode), pt_show_type_enum (arg->type_enum));
	      break;
	    }
	}

      /* look for unsupported argument types in the list */
      while (i < (sizeof (has_arg_type) / sizeof (has_arg_type[0])))
	{
	  if (has_arg_type[i])
	    {
	      if (!PT_IS_NUMERIC_TYPE (PT_TYPE_MIN + i) && !PT_IS_CHAR_STRING_TYPE (PT_TYPE_MIN + i)
		  && !PT_IS_DATE_TIME_TYPE (PT_TYPE_MIN + i) && (PT_TYPE_MIN + i != PT_TYPE_ENUMERATION)
		  && (PT_TYPE_MIN + i != PT_TYPE_LOGICAL) && (PT_TYPE_MIN + i != PT_TYPE_NONE)
		  && (PT_TYPE_MIN + i != PT_TYPE_NA) && (PT_TYPE_MIN + i != PT_TYPE_NULL)
		  && (PT_TYPE_MIN + i != PT_TYPE_MAYBE))
		{
		  /* type is not NULL, unknown and is not known coercible to [N]CHAR VARYING */
		  size_t k = 0;

		  while (k < num_bad && bad_types[k] != PT_TYPE_MIN + i)
		    {
		      k++;
		    }

		  if (k == num_bad)
		    {
		      bad_types[num_bad++] = (PT_TYPE_ENUM) (PT_TYPE_MIN + i);

		      if (num_bad == sizeof (bad_types) / sizeof (bad_types[0]))
			{
			  break;
			}
		    }
		}
	    }

	  i++;
	}

      /* check string category (CHAR or NCHAR) for any string arguments */
      if ((num_bad < sizeof (bad_types) / sizeof (bad_types[0]) - 1)
	  && (has_arg_type[PT_TYPE_CHAR - PT_TYPE_MIN] || has_arg_type[PT_TYPE_VARCHAR - PT_TYPE_MIN])
	  && (has_arg_type[PT_TYPE_NCHAR - PT_TYPE_MIN] || has_arg_type[PT_TYPE_VARNCHAR - PT_TYPE_MIN]))
	{
	  if (has_arg_type[PT_TYPE_CHAR - PT_TYPE_MIN])
	    {
	      bad_types[num_bad++] = PT_TYPE_CHAR;
	    }
	  else
	    {
	      bad_types[num_bad++] = PT_TYPE_VARCHAR;
	    }

	  if (has_arg_type[PT_TYPE_NCHAR - PT_TYPE_MIN])
	    {
	      bad_types[num_bad++] = PT_TYPE_NCHAR;
	    }
	  else
	    {
	      bad_types[num_bad++] = PT_TYPE_VARNCHAR;
	    }
	}

      /* report any unsupported arguments */
      switch (num_bad)
	{
	case 1:
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (bad_types[0]));
	  break;
	case 2:
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf3 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_2,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (bad_types[0]),
		       pt_show_type_enum (bad_types[1]));
	  break;
	case 3:
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf4 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_3,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (bad_types[0]),
		       pt_show_type_enum (bad_types[1]), pt_show_type_enum (bad_types[2]));
	  break;
	case 4:
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (bad_types[0]),
		       pt_show_type_enum (bad_types[1]), pt_show_type_enum (bad_types[2]),
		       pt_show_type_enum (bad_types[3]));
	  break;
	}
    }
    break;

    case F_INSERT_SUBSTRING:
    {
      PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE, arg3_type = PT_TYPE_NONE, arg4_type =
				       PT_TYPE_NONE;
      PT_NODE *arg_array[NUM_F_INSERT_SUBSTRING_ARGS];
      int num_args = 0;
      /* arg_list to array */
      if (pt_node_list_to_array (parser, arg_list, arg_array, NUM_F_INSERT_SUBSTRING_ARGS, &num_args) != NO_ERROR)
	{
	  break;
	}
      if (num_args != NUM_F_INSERT_SUBSTRING_ARGS)
	{
	  assert (false);
	  break;
	}

      arg1_type = arg_array[0]->type_enum;
      arg2_type = arg_array[1]->type_enum;
      arg3_type = arg_array[2]->type_enum;
      arg4_type = arg_array[3]->type_enum;
      /* check arg2 and arg3 */
      if (!PT_IS_NUMERIC_TYPE (arg2_type) && !PT_IS_CHAR_STRING_TYPE (arg2_type) && arg2_type != PT_TYPE_MAYBE
	  && arg2_type != PT_TYPE_NULL && arg2_type != PT_TYPE_NA)
	{
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_type), pt_show_type_enum (arg2_type),
		       pt_show_type_enum (arg3_type), pt_show_type_enum (arg4_type));
	  break;
	}

      if (!PT_IS_NUMERIC_TYPE (arg3_type) && !PT_IS_CHAR_STRING_TYPE (arg3_type) && arg3_type != PT_TYPE_MAYBE
	  && arg3_type != PT_TYPE_NULL && arg3_type != PT_TYPE_NA)
	{
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_type), pt_show_type_enum (arg2_type),
		       pt_show_type_enum (arg3_type), pt_show_type_enum (arg4_type));
	  break;
	}

      /* check arg1 */
      if (!PT_IS_NUMERIC_TYPE (arg1_type) && !PT_IS_STRING_TYPE (arg1_type) && !PT_IS_DATE_TIME_TYPE (arg1_type)
	  && arg1_type != PT_TYPE_ENUMERATION && arg1_type != PT_TYPE_MAYBE && arg1_type != PT_TYPE_NULL
	  && arg1_type != PT_TYPE_NA)
	{
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_type), pt_show_type_enum (arg2_type),
		       pt_show_type_enum (arg3_type), pt_show_type_enum (arg4_type));
	  break;
	}
      /* check arg4 */
      if (!PT_IS_NUMERIC_TYPE (arg4_type) && !PT_IS_STRING_TYPE (arg4_type) && !PT_IS_DATE_TIME_TYPE (arg4_type)
	  && arg4_type != PT_TYPE_MAYBE && arg4_type != PT_TYPE_NULL && arg4_type != PT_TYPE_NA)
	{
	  arg_type = PT_TYPE_NONE;
	  PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_type), pt_show_type_enum (arg2_type),
		       pt_show_type_enum (arg3_type), pt_show_type_enum (arg4_type));
	  break;
	}
    }
    break;

    case PT_MEDIAN:
    case PT_PERCENTILE_CONT:
    case PT_PERCENTILE_DISC:
      if (arg_type != PT_TYPE_NULL && arg_type != PT_TYPE_NA && !PT_IS_NUMERIC_TYPE (arg_type)
	  && !PT_IS_STRING_TYPE (arg_type) && !PT_IS_DATE_TIME_TYPE (arg_type) && arg_type != PT_TYPE_MAYBE)
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_OPDS,
		       fcode_get_lowercase_name (fcode), pt_show_type_enum (arg_type));
	}

      break;

    default:
      check_agg_single_arg = false;
      break;
    }

  if (is_agg_function && check_agg_single_arg)
    {
      if (arg_list->next)
	{
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_AGG_FUN_WANT_1_ARG,
		      pt_short_print (parser, node));
	}
    }

  if (node->type_enum == PT_TYPE_NONE || node->data_type == NULL || ! (node->info.function.is_type_checked))
    {
      /* determine function result type */
      switch (fcode)
	{
	case PT_COUNT:
	case PT_COUNT_STAR:
	  assert (false);
	  break;
	case PT_ROW_NUMBER:
	case PT_RANK:
	case PT_DENSE_RANK:
	case PT_NTILE:
	  node->type_enum = PT_TYPE_INTEGER;
	  break;

	case PT_CUME_DIST:
	case PT_PERCENT_RANK:
	  node->type_enum = PT_TYPE_DOUBLE;
	  break;

	case PT_GROUPBY_NUM:
	  node->type_enum = PT_TYPE_BIGINT;
	  break;

	case PT_AGG_BIT_AND:
	case PT_AGG_BIT_OR:
	case PT_AGG_BIT_XOR:
	  node->type_enum = PT_TYPE_BIGINT;
	  break;

	case F_TABLE_SET:
	  node->type_enum = PT_TYPE_SET;
	  pt_add_type_to_set (parser, pt_get_select_list (parser, arg_list), &node->data_type);
	  break;

	case F_TABLE_MULTISET:
	  node->type_enum = PT_TYPE_MULTISET;
	  pt_add_type_to_set (parser, pt_get_select_list (parser, arg_list), &node->data_type);
	  break;

	case F_TABLE_SEQUENCE:
	  node->type_enum = PT_TYPE_SEQUENCE;
	  pt_add_type_to_set (parser, pt_get_select_list (parser, arg_list), &node->data_type);
	  break;

	case F_SET:
	  node->type_enum = PT_TYPE_SET;
	  pt_add_type_to_set (parser, arg_list, &node->data_type);
	  break;

	case F_MULTISET:
	  node->type_enum = PT_TYPE_MULTISET;
	  pt_add_type_to_set (parser, arg_list, &node->data_type);
	  break;

	case F_SEQUENCE:
	  node->type_enum = PT_TYPE_SEQUENCE;
	  pt_add_type_to_set (parser, arg_list, &node->data_type);
	  break;

	case PT_SUM:
	  node->type_enum = arg_type;
	  node->data_type = parser_copy_tree_list (parser, arg_list->data_type);
	  if (arg_type == PT_TYPE_NUMERIC && node->data_type)
	    {
	      node->data_type->info.data_type.precision = DB_MAX_NUMERIC_PRECISION;
	    }
	  break;

	case PT_AVG:
	case PT_STDDEV:
	case PT_STDDEV_POP:
	case PT_STDDEV_SAMP:
	case PT_VARIANCE:
	case PT_VAR_POP:
	case PT_VAR_SAMP:
	  node->type_enum = arg_type;
	  node->data_type = NULL;
	  break;

	case PT_MEDIAN:
	case PT_PERCENTILE_CONT:
	case PT_PERCENTILE_DISC:
	  /* let calculation decide the type */
	  node->type_enum = PT_TYPE_MAYBE;
	  node->data_type = NULL;
	  break;

	case PT_GROUP_CONCAT:
	{
	  if (arg_type == PT_TYPE_NCHAR || arg_type == PT_TYPE_VARNCHAR)
	    {
	      node->type_enum = PT_TYPE_VARNCHAR;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARNCHAR);
	      if (node->data_type == NULL)
		{
		  assert (false);
		}
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	  else if (arg_type == PT_TYPE_BIT || arg_type == PT_TYPE_VARBIT)
	    {
	      node->type_enum = PT_TYPE_VARBIT;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARBIT);
	      if (node->data_type == NULL)
		{
		  node->type_enum = PT_TYPE_NONE;
		  assert (false);
		}
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	  else
	    {
	      node->type_enum = PT_TYPE_VARCHAR;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
	      if (node->data_type == NULL)
		{
		  node->type_enum = PT_TYPE_NONE;
		  assert (false);
		}
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	}
	break;

	case F_INSERT_SUBSTRING:
	{
	  PT_NODE *new_att = NULL;
	  PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE, arg3_type = PT_TYPE_NONE, arg4_type =
					   PT_TYPE_NONE;
	  PT_TYPE_ENUM arg1_orig_type, arg2_orig_type, arg3_orig_type, arg4_orig_type;
	  PT_NODE *arg_array[NUM_F_INSERT_SUBSTRING_ARGS];
	  int num_args;

	  /* arg_list to array */
	  if (pt_node_list_to_array (parser, arg_list, arg_array, NUM_F_INSERT_SUBSTRING_ARGS, &num_args) != NO_ERROR)
	    {
	      break;
	    }
	  if (num_args != NUM_F_INSERT_SUBSTRING_ARGS)
	    {
	      assert (false);
	      break;
	    }

	  arg1_orig_type = arg1_type = arg_array[0]->type_enum;
	  arg2_orig_type = arg2_type = arg_array[1]->type_enum;
	  arg3_orig_type = arg3_type = arg_array[2]->type_enum;
	  arg4_orig_type = arg4_type = arg_array[3]->type_enum;
	  arg_type = PT_TYPE_NONE;

	  /* validate and/or convert arguments */
	  /* arg1 should be VAR-str, but compatible with arg4 (except when arg4 is BIT - no casting to bit on arg1) */
	  if (! (PT_IS_STRING_TYPE (arg1_type)))
	    {
	      PT_TYPE_ENUM upgraded_type = PT_TYPE_NONE;
	      if (arg4_type == PT_TYPE_NCHAR)
		{
		  upgraded_type = PT_TYPE_VARNCHAR;
		}
	      else
		{
		  upgraded_type = PT_TYPE_VARCHAR;
		}

	      new_att =
		      pt_wrap_with_cast_op (parser, arg_array[0], upgraded_type, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	      if (new_att == NULL)
		{
		  break;
		}
	      node->info.function.arg_list = arg_array[0] = new_att;
	      arg_type = arg1_type = upgraded_type;
	    }
	  else
	    {
	      arg_type = arg1_type;
	    }


	  if (arg2_type != PT_TYPE_INTEGER)
	    {
	      new_att =
		      pt_wrap_with_cast_op (parser, arg_array[1], PT_TYPE_INTEGER, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	      if (new_att == NULL)
		{
		  break;
		}
	      arg_array[0]->next = arg_array[1] = new_att;
	      arg2_type = PT_TYPE_INTEGER;

	    }

	  if (arg3_type != PT_TYPE_INTEGER)
	    {
	      new_att =
		      pt_wrap_with_cast_op (parser, arg_array[2], PT_TYPE_INTEGER, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	      if (new_att == NULL)
		{
		  break;
		}
	      arg_array[1]->next = arg_array[2] = new_att;
	      arg3_type = PT_TYPE_INTEGER;
	    }

	  /* set result type and precision */
	  if (arg_type == PT_TYPE_NCHAR || arg_type == PT_TYPE_VARNCHAR)
	    {
	      node->type_enum = PT_TYPE_VARNCHAR;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARNCHAR);
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	  else if (arg_type == PT_TYPE_BIT || arg_type == PT_TYPE_VARBIT)
	    {
	      node->type_enum = PT_TYPE_VARBIT;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARBIT);
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	  else
	    {
	      arg_type = node->type_enum = PT_TYPE_VARCHAR;
	      node->data_type = pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
	      node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	    }
	  /* validate and/or set arg4 */
	  if (! (PT_IS_STRING_TYPE (arg4_type)))
	    {
	      new_att = pt_wrap_with_cast_op (parser, arg_array[3], arg_type, TP_FLOATING_PRECISION_VALUE, 0, NULL);
	      if (new_att == NULL)
		{
		  break;
		}
	      arg_array[2]->next = arg_array[3] = new_att;
	    }
	  /* final check of arg and arg4 type matching */
	  if ((arg4_type == PT_TYPE_VARNCHAR || arg4_type == PT_TYPE_NCHAR)
	      && (arg_type == PT_TYPE_VARCHAR || arg_type == PT_TYPE_CHAR))
	    {
	      arg_type = PT_TYPE_NONE;
	      PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
			   fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_orig_type),
			   pt_show_type_enum (arg2_orig_type), pt_show_type_enum (arg3_orig_type),
			   pt_show_type_enum (arg4_orig_type));
	    }
	  else if ((arg_type == PT_TYPE_VARNCHAR || arg_type == PT_TYPE_NCHAR)
		   && (arg4_type == PT_TYPE_VARCHAR || arg4_type == PT_TYPE_CHAR))
	    {
	      arg_type = PT_TYPE_NONE;
	      PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
			   fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_orig_type),
			   pt_show_type_enum (arg2_orig_type), pt_show_type_enum (arg3_orig_type),
			   pt_show_type_enum (arg4_orig_type));
	    }
	  else if ((arg_type == PT_TYPE_VARBIT || arg_type == PT_TYPE_BIT)
		   && (arg4_type != PT_TYPE_VARBIT && arg4_type != PT_TYPE_BIT))
	    {
	      arg_type = PT_TYPE_NONE;
	      PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
			   fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_orig_type),
			   pt_show_type_enum (arg2_orig_type), pt_show_type_enum (arg3_orig_type),
			   pt_show_type_enum (arg4_orig_type));
	    }
	  else if ((arg4_type == PT_TYPE_VARBIT || arg4_type == PT_TYPE_BIT)
		   && (arg_type != PT_TYPE_VARBIT && arg_type != PT_TYPE_BIT))
	    {
	      arg_type = PT_TYPE_NONE;
	      PT_ERRORmf5 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNC_NOT_DEFINED_ON_4,
			   fcode_get_lowercase_name (fcode), pt_show_type_enum (arg1_orig_type),
			   pt_show_type_enum (arg2_orig_type), pt_show_type_enum (arg3_orig_type),
			   pt_show_type_enum (arg4_orig_type));
	    }
	}
	break;

	case F_ELT:
	{
	  PT_NODE *new_att = NULL;
	  PT_NODE *arg = arg_list, *prev_arg = arg_list;
	  int max_precision = 0;

	  /* check and cast the type for the index argument. */
	  if (!PT_IS_DISCRETE_NUMBER_TYPE (arg->type_enum))
	    {
	      new_att = pt_wrap_with_cast_op (parser, arg, PT_TYPE_BIGINT, TP_FLOATING_PRECISION_VALUE, 0, NULL);

	      if (new_att)
		{
		  prev_arg = arg_list = arg = new_att;
		  node->info.function.arg_list = arg_list;
		}
	      else
		{
		  break;
		}
	    }

	  /*
	   * Look for the first argument of character string type and obtain its category (CHAR/NCHAR). All other
	   * arguments should be converted to this type, which is also the return type. */

	  arg_type = PT_TYPE_NONE;
	  arg = arg->next;

	  while (arg && arg_type == PT_TYPE_NONE)
	    {
	      if (PT_IS_CHAR_STRING_TYPE (arg->type_enum))
		{
		  if (arg->type_enum == PT_TYPE_CHAR || arg->type_enum == PT_TYPE_VARCHAR)
		    {
		      arg_type = PT_TYPE_VARCHAR;
		    }
		  else
		    {
		      arg_type = PT_TYPE_VARNCHAR;
		    }
		}
	      else
		{
		  arg = arg->next;
		}
	    }

	  if (arg_type == PT_TYPE_NONE)
	    {
	      /* no [N]CHAR [VARYING] argument passed; convert them all to VARCHAR */
	      arg_type = PT_TYPE_VARCHAR;
	    }

	  /* take the maximum precision among all value arguments */
	  arg = arg_list->next;

	  while (arg)
	    {
	      int precision = TP_FLOATING_PRECISION_VALUE;

	      precision = pt_character_length_for_node (arg, arg_type);
	      if (max_precision != TP_FLOATING_PRECISION_VALUE)
		{
		  if (precision == TP_FLOATING_PRECISION_VALUE || max_precision < precision)
		    {
		      max_precision = precision;
		    }
		}

	      arg = arg->next;
	    }

	  /* cast all arguments to [N]CHAR VARYING(max_precision) */
	  arg = arg_list->next;
	  while (arg)
	    {
	      if ((arg->type_enum != arg_type) ||
		  (arg->data_type && arg->data_type->info.data_type.precision != max_precision))
		{
		  PT_NODE *new_attr = pt_wrap_with_cast_op (parser, arg, arg_type,
				      max_precision, 0, NULL);

		  if (new_attr)
		    {
		      prev_arg->next = arg = new_attr;
		    }
		  else
		    {
		      break;
		    }
		}

	      arg = arg->next;
	      prev_arg = prev_arg->next;
	    }

	  /* Return the selected data type and precision */

	  node->data_type = pt_make_prim_data_type (parser, arg_type);

	  if (node->data_type)
	    {
	      node->type_enum = arg_type;
	      node->data_type->info.data_type.precision = max_precision;
	      node->data_type->info.data_type.dec_precision = 0;
	    }
	}
	break;

	default:
	  /* otherwise, f(x) has same type as x */
	  node->type_enum = arg_type;
	  node->data_type = parser_copy_tree_list (parser, arg_list->data_type);
	  break;
	}
      /* to prevent recheck of function return type at pt_eval_function_type_old() */
      node->info.function.is_type_checked = true;
    }

  /* collation checking */
  arg_list = node->info.function.arg_list;
  switch (fcode)
    {
    case PT_GROUP_CONCAT:
    {
      PT_COLL_INFER coll_infer1;
      PT_NODE *new_node;
      PT_TYPE_ENUM sep_type;

      (void) pt_get_collation_info (arg_list, &coll_infer1);

      sep_type = (arg_list->next) ? arg_list->next->type_enum : PT_TYPE_NONE;
      if (PT_HAS_COLLATION (sep_type))
	{
	  assert (arg_list->next != NULL);

	  new_node =
		  pt_coerce_node_collation (parser, arg_list->next, coll_infer1.coll_id, coll_infer1.codeset, false, false,
					    PT_COLL_WRAP_TYPE_FOR_MAYBE (sep_type), PT_TYPE_NONE);

	  if (new_node == NULL)
	    {
	      goto error_collation;
	    }

	  arg_list->next = new_node;
	}

      if (arg_list->type_enum != PT_TYPE_MAYBE)
	{
	  new_node =
		  pt_coerce_node_collation (parser, node, coll_infer1.coll_id, coll_infer1.codeset, true, false,
					    PT_COLL_WRAP_TYPE_FOR_MAYBE (arg_list->type_enum), PT_TYPE_NONE);

	  if (new_node == NULL)
	    {
	      goto error_collation;
	    }

	  node = new_node;
	}
      else if (node->data_type != NULL)
	{
	  /* argument is not determined, collation of result will be resolved at execution */
	  node->data_type->info.data_type.collation_flag = TP_DOMAIN_COLL_LEAVE;
	}
    }
    break;

    case F_INSERT_SUBSTRING:
    {
      PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg4_type = PT_TYPE_NONE;
      PT_NODE *arg_array[NUM_F_INSERT_SUBSTRING_ARGS];
      int num_args = 0;
      PT_COLL_INFER coll_infer1, coll_infer4;
      INTL_CODESET common_cs = LANG_SYS_CODESET;
      int common_coll = LANG_SYS_COLLATION;
      PT_NODE *new_node;
      int args_w_coll = 0;

      coll_infer1.codeset = LANG_SYS_CODESET;
      coll_infer4.codeset = LANG_SYS_CODESET;
      coll_infer1.coll_id = LANG_SYS_COLLATION;
      coll_infer4.coll_id = LANG_SYS_COLLATION;
      coll_infer1.coerc_level = PT_COLLATION_NOT_APPLICABLE;
      coll_infer4.coerc_level = PT_COLLATION_NOT_APPLICABLE;
      coll_infer1.can_force_cs = true;
      coll_infer4.can_force_cs = true;

      if (pt_node_list_to_array (parser, arg_list, arg_array, NUM_F_INSERT_SUBSTRING_ARGS, &num_args) != NO_ERROR)
	{
	  break;
	}

      if (num_args != NUM_F_INSERT_SUBSTRING_ARGS)
	{
	  assert (false);
	  break;
	}

      arg1_type = arg_array[0]->type_enum;
      arg4_type = arg_array[3]->type_enum;

      if (PT_HAS_COLLATION (arg1_type) || arg1_type == PT_TYPE_MAYBE)
	{
	  if (pt_get_collation_info (arg_array[0], &coll_infer1))
	    {
	      args_w_coll++;
	    }

	  if (arg1_type != PT_TYPE_MAYBE)
	    {
	      common_coll = coll_infer1.coll_id;
	      common_cs = coll_infer1.codeset;
	    }
	}

      if (PT_HAS_COLLATION (arg4_type) || arg4_type == PT_TYPE_MAYBE)
	{
	  if (pt_get_collation_info (arg_array[3], &coll_infer4))
	    {
	      args_w_coll++;
	    }

	  if (arg1_type != PT_TYPE_MAYBE)
	    {
	      common_coll = coll_infer4.coll_id;
	      common_cs = coll_infer4.codeset;
	    }
	}

      if (coll_infer1.coll_id == coll_infer4.coll_id)
	{
	  assert (coll_infer1.codeset == coll_infer4.codeset);
	  common_coll = coll_infer1.coll_id;
	  common_cs = coll_infer1.codeset;
	}
      else
	{
	  if (pt_common_collation (&coll_infer1, &coll_infer4, NULL, args_w_coll, false, &common_coll, &common_cs) !=
	      0)
	    {
	      goto error_collation;
	    }
	}

      /* coerce collation of arguments */
      if ((common_coll != coll_infer1.coll_id || common_cs != coll_infer1.codeset)
	  && (PT_HAS_COLLATION (arg1_type) || arg1_type == PT_TYPE_MAYBE))
	{
	  new_node =
		  pt_coerce_node_collation (parser, arg_array[0], common_coll, common_cs, coll_infer1.can_force_cs, false,
					    PT_COLL_WRAP_TYPE_FOR_MAYBE (arg1_type), PT_TYPE_NONE);
	  if (new_node == NULL)
	    {
	      goto error_collation;
	    }

	  node->info.function.arg_list = arg_array[0] = new_node;
	}

      /* coerce collation of arguments */
      if ((common_coll != coll_infer4.coll_id || common_cs != coll_infer4.codeset)
	  && (PT_HAS_COLLATION (arg4_type) || arg4_type == PT_TYPE_MAYBE))
	{
	  new_node =
		  pt_coerce_node_collation (parser, arg_array[3], common_coll, common_cs, coll_infer4.can_force_cs, false,
					    PT_COLL_WRAP_TYPE_FOR_MAYBE (arg4_type), PT_TYPE_NONE);
	  if (new_node == NULL)
	    {
	      goto error_collation;
	    }

	  arg_array[2]->next = arg_array[3] = new_node;
	}

      if ((arg_array[3]->type_enum == PT_TYPE_MAYBE || PT_IS_CAST_MAYBE (arg_array[3]))
	  && (arg_array[0]->type_enum == PT_TYPE_MAYBE || PT_IS_CAST_MAYBE (arg_array[0])) && node->data_type != NULL)
	{
	  node->data_type->info.data_type.collation_flag = TP_DOMAIN_COLL_LEAVE;
	}
      else
	{
	  new_node =
		  pt_coerce_node_collation (parser, node, common_coll, common_cs, true, false, PT_TYPE_NONE, PT_TYPE_NONE);
	  if (new_node == NULL)
	    {
	      goto error_collation;
	    }

	  node = new_node;
	}
    }
    break;

    default:
      node = pt_check_function_collation (parser, node);
      break;
    }

  return node;

error_collation:
  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATION_OP_ERROR,
	      fcode_get_lowercase_name (fcode));

  return node;
}


/*
 * pt_check_function_collation () - checks the collation of function
 *				    (PT_FUNCTION)
 *
 *   return: error code
 *   parser(in): parser context
 *   node(in/out): a parse tree function node
 *
 */
static PT_NODE *
pt_check_function_collation (PARSER_CONTEXT *parser, PT_NODE *node)
{
  PT_NODE *arg_list, *arg, *prev_arg, *new_node;
  PT_COLL_INFER common_coll_infer, res_coll_infer;
  bool need_arg_coerc = false;
  TP_DOMAIN_COLL_ACTION res_collation_flag = TP_DOMAIN_COLL_LEAVE;
  FUNC_CODE fcode;

  assert (node != NULL);
  assert (node->node_type == PT_FUNCTION);

  if (node->info.function.arg_list == NULL)
    {
      return node;
    }

  fcode = node->info.function.function_type;
  arg_list = node->info.function.arg_list;
  prev_arg = NULL;

  if (fcode == F_ELT)
    {
      if (arg_list->next == NULL)
	{
	  return node;
	}
      arg_list = arg_list->next;
      prev_arg = arg_list;
    }

  arg = arg_list;

  common_coll_infer.coll_id = -1;
  common_coll_infer.codeset = INTL_CODESET_NONE;
  common_coll_infer.can_force_cs = true;
  common_coll_infer.coerc_level = PT_COLLATION_NOT_APPLICABLE;
  while (arg != NULL)
    {
      PT_COLL_INFER arg_coll_infer;

      arg_coll_infer.coll_id = -1;

      if (arg->type_enum != PT_TYPE_MAYBE && ! (PT_IS_CAST_MAYBE (arg)))
	{
	  res_collation_flag = TP_DOMAIN_COLL_NORMAL;
	}

      if ((PT_HAS_COLLATION (arg->type_enum) || arg->type_enum == PT_TYPE_MAYBE)
	  && pt_get_collation_info (arg, &arg_coll_infer))
	{
	  bool apply_common_coll = false;
	  int common_coll;
	  INTL_CODESET common_cs;

	  if (common_coll_infer.coll_id != -1 || common_coll_infer.coll_id != arg_coll_infer.coll_id)
	    {
	      need_arg_coerc = true;
	    }

	  if (common_coll_infer.coll_id == -1)
	    {
	      apply_common_coll = true;
	    }
	  else
	    {
	      if (pt_common_collation (&common_coll_infer, &arg_coll_infer, NULL, 2, false, &common_coll, &common_cs) ==
		  0)
		{
		  if (common_coll != common_coll_infer.coll_id)
		    {
		      apply_common_coll = true;
		    }
		}
	      else
		{
		  goto error_collation;
		}
	    }

	  if (apply_common_coll)
	    {
	      common_coll_infer = arg_coll_infer;
	    }
	}

      arg = arg->next;
    }

  if (common_coll_infer.coll_id == -1)
    {
      return node;
    }

  arg = arg_list;
  while (need_arg_coerc && arg != NULL)
    {
      PT_COLL_INFER arg_coll_infer;

      arg_coll_infer.coll_id = -1;

      if (! (PT_HAS_COLLATION (arg->type_enum) || arg->type_enum == PT_TYPE_MAYBE))
	{
	  prev_arg = arg;
	  arg = arg->next;
	  continue;
	}

      if (!pt_get_collation_info (arg, &arg_coll_infer))
	{
	  prev_arg = arg;
	  arg = arg->next;
	  continue;
	}

      if (common_coll_infer.coll_id != arg_coll_infer.coll_id)
	{
	  PT_NODE *save_next;

	  save_next = arg->next;

	  new_node =
		  pt_coerce_node_collation (parser, arg, common_coll_infer.coll_id, common_coll_infer.codeset,
					    arg_coll_infer.can_force_cs, false, PT_COLL_WRAP_TYPE_FOR_MAYBE (arg->type_enum),
					    PT_TYPE_NONE);

	  if (new_node != NULL)
	    {
	      arg = new_node;
	      if (prev_arg == NULL)
		{
		  node->info.function.arg_list = arg;
		}
	      else
		{
		  prev_arg->next = arg;
		}
	      arg->next = save_next;
	    }
	}

      prev_arg = arg;
      arg = arg->next;
    }

  if (need_arg_coerc)
    {
      switch (fcode)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	  /* add the new data_type to the set of data_types */
	  pt_add_type_to_set (parser, arg_list, &node->data_type);
	  break;
	default:
	  break;
	}
    }

  if (PT_HAS_COLLATION (node->type_enum) && res_collation_flag == TP_DOMAIN_COLL_LEAVE && node->data_type != NULL)
    {
      node->data_type->info.data_type.collation_flag = res_collation_flag;
    }
  else if ((PT_HAS_COLLATION (node->type_enum) || node->type_enum == PT_TYPE_MAYBE)
	   && pt_get_collation_info (node, &res_coll_infer) && res_coll_infer.coll_id != common_coll_infer.coll_id)
    {
      new_node =
	      pt_coerce_node_collation (parser, node, common_coll_infer.coll_id, common_coll_infer.codeset, true, false,
					PT_COLL_WRAP_TYPE_FOR_MAYBE (node->type_enum), PT_TYPE_NONE);
      if (new_node != NULL)
	{
	  node = new_node;
	}
    }

  return node;

error_collation:
  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATION_OP_ERROR,
	      fcode_get_lowercase_name (node->info.function.function_type));
  return node;
}


/* pt_character_length_for_node() -
    return: number of characters that a value of the given type can possibly
	    occuppy when cast to a CHAR type.
    node(in): node with type whose character length is to be returned.
    coerce_type(in): string type that node will be cast to
*/
static int
pt_character_length_for_node (PT_NODE *node, const PT_TYPE_ENUM coerce_type)
{
  int precision = DB_DEFAULT_PRECISION;

  switch (node->type_enum)
    {
    case PT_TYPE_DOUBLE:
      precision = TP_DOUBLE_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_FLOAT:
      precision = TP_FLOAT_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_MONETARY:
      precision = TP_MONETARY_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_BIGINT:
      precision = TP_BIGINT_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_INTEGER:
      precision = TP_INTEGER_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_SMALLINT:
      precision = TP_SMALLINT_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_TIME:
      precision = TP_TIME_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_DATE:
      precision = TP_DATE_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_TIMESTAMP:
      precision = TP_TIMESTAMP_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_TIMESTAMPLTZ:
    case PT_TYPE_TIMESTAMPTZ:
      precision = TP_TIMESTAMPTZ_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_DATETIME:
      precision = TP_DATETIME_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_DATETIMETZ:
    case PT_TYPE_DATETIMELTZ:
      precision = TP_DATETIMETZ_AS_CHAR_LENGTH;
      break;
    case PT_TYPE_NUMERIC:
      if (node->data_type == NULL)
	{
	  precision = DB_DEFAULT_NUMERIC_PRECISION + 1;	/* sign */
	  break;
	}

      precision = node->data_type->info.data_type.precision;
      if (precision == 0 || precision == DB_DEFAULT_PRECISION)
	{
	  precision = DB_DEFAULT_NUMERIC_PRECISION;
	}
      precision++;		/* for sign */

      if (node->data_type->info.data_type.dec_precision
	  && (node->data_type->info.data_type.dec_precision != DB_DEFAULT_SCALE
	      || node->data_type->info.data_type.dec_precision != DB_DEFAULT_NUMERIC_SCALE))
	{
	  precision++;		/* for decimal point */
	}
      break;
    case PT_TYPE_CHAR:
      if (node->data_type != NULL)
	{
	  precision = node->data_type->info.data_type.precision;
	}

      if (precision == DB_DEFAULT_PRECISION)
	{
	  precision = DB_MAX_CHAR_PRECISION;
	}
      break;
    case PT_TYPE_VARCHAR:
      if (node->data_type != NULL)
	{
	  precision = node->data_type->info.data_type.precision;
	}

      if (precision == DB_DEFAULT_PRECISION)
	{
	  precision = DB_MAX_VARCHAR_PRECISION;
	}
      break;
    case PT_TYPE_NCHAR:
      if (node->data_type != NULL)
	{
	  precision = node->data_type->info.data_type.precision;
	}

      if (precision == DB_DEFAULT_PRECISION)
	{
	  precision = DB_MAX_NCHAR_PRECISION;
	}
      break;
    case PT_TYPE_VARNCHAR:
      if (node->data_type != NULL)
	{
	  precision = node->data_type->info.data_type.precision;
	}

      if (precision == DB_DEFAULT_PRECISION)
	{
	  precision = DB_MAX_VARNCHAR_PRECISION;
	}
      break;
    case PT_TYPE_NULL:
    case PT_TYPE_NA:
      precision = 0;
      break;

    default:
      /* for host vars */
      switch (coerce_type)
	{
	case PT_TYPE_VARCHAR:
	  precision = DB_MAX_VARCHAR_PRECISION;
	  break;
	case PT_TYPE_VARNCHAR:
	  precision = DB_MAX_VARNCHAR_PRECISION;
	  break;
	default:
	  precision = TP_FLOATING_PRECISION_VALUE;
	  break;
	}
      break;
    }

  return precision;
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

bool
pt_is_function_unsupported (FUNC_CODE fcode)
{
  switch (fcode)
    {
    case PT_TOP_AGG_FUNC:
    case F_MIDXKEY:
    case F_TOP_TABLE_FUNC:
    case F_VID:
      return true;
    default:
      return false;
    }
}

bool
pt_is_function_no_arg (FUNC_CODE fcode)
{
  switch (fcode)
    {
    case PT_COUNT_STAR:
    case PT_GROUPBY_NUM:
    case PT_ROW_NUMBER:
    case PT_RANK:
    case PT_DENSE_RANK:
    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
    case F_JSON_ARRAY:
    case F_JSON_OBJECT:
      return true;

    default:
      return false;
    }
}

bool
pt_is_function_new_type_checking (FUNC_CODE fcode)
{
  switch (fcode)
    {
    // old functions
    // case PT_MIN:
    // case PT_MAX:
    // case PT_SUM:

    case PT_AVG:
    case PT_STDDEV:
    case PT_VARIANCE:
    case PT_STDDEV_POP:
    case PT_VAR_POP:
    case PT_STDDEV_SAMP:
    case PT_VAR_SAMP:

    case PT_GROUPBY_NUM:
    // case PT_AGG_BIT_AND:
    // case PT_AGG_BIT_OR:
    // case PT_AGG_BIT_XOR:
    // case PT_GROUP_CONCAT:
    case PT_ROW_NUMBER:
    case PT_RANK:
    case PT_DENSE_RANK:
    // case PT_NTILE:
    // case PT_LEAD:
    // case PT_LAG:
    case F_SET:
    case F_TABLE_SET:
    case F_MULTISET:
    case F_TABLE_MULTISET:
    case F_SEQUENCE:
    case F_TABLE_SEQUENCE:

    // case F_INSERT_SUBSTRING:
    // case F_ELT:

    // case PT_FIRST_VALUE:
    // case PT_LAST_VALUE:
    // case PT_NTH_VALUE:
    case PT_MEDIAN:

    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
    // case PT_PERCENTILE_CONT:
    // case PT_PERCENTILE_DISC:

    case F_BENCHMARK:
    // JSON functions are migrated to new checking function
    case F_JSON_ARRAY:
    case F_JSON_ARRAY_APPEND:
    case F_JSON_ARRAY_INSERT:
    case PT_JSON_ARRAYAGG:
    case F_JSON_CONTAINS:
    case F_JSON_CONTAINS_PATH:
    case F_JSON_DEPTH:
    case F_JSON_EXTRACT:
    case F_JSON_GET_ALL_PATHS:
    case F_JSON_KEYS:
    case F_JSON_INSERT:
    case F_JSON_LENGTH:
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
    case F_JSON_OBJECT:
    case PT_JSON_OBJECTAGG:
    case F_JSON_PRETTY:
    case F_JSON_QUOTE:
    case F_JSON_REMOVE:
    case F_JSON_REPLACE:
    case F_JSON_SEARCH:
    case F_JSON_SET:
    case F_JSON_TYPE:
    case F_JSON_UNQUOTE:
    case F_JSON_VALID:
    // REGEXP functions are migrated to new checking function
    case F_REGEXP_COUNT:
    case F_REGEXP_INSTR:
    case F_REGEXP_LIKE:
    case F_REGEXP_REPLACE:
    case F_REGEXP_SUBSTR:
    // COUNT functions
    case PT_COUNT:
    case PT_COUNT_STAR:
      return true;

    default:
      return false;
    }
}
