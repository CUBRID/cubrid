/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * func_type.cpp
 */

#include "func_type.hpp"
#include "message_catalog.h"
#include "parse_tree.h"
#include "parser.h"
#include "parser_message.h"

//PT_TYPE_MAYBE
// - for the moment I don't see how to eliminate PT_TYPE_MAYBE from functions with multiple signature
// - with PT_TYPE_MAYBE in signature, the final type will not be decided during type checking but later
// - without PT_TYPE_MAYBE in signature you must either:
//   1. choose one signature and apply cast
//   ... but what about "prepare median(?)... execute with date'2018-06-13'... execute with 123"?
//   2. handle from code
//   ... but there are cases when the cast should be made
//   but neither one of them is OK

std::vector<func_signature> func_signature::integer =
{
  {PT_TYPE_INTEGER, {}, {}},
};

std::vector<func_signature> func_signature::bigint =
{
  {PT_TYPE_BIGINT, {}, {}},
};

std::vector<func_signature> func_signature::percentile_cont =
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

std::vector<func_signature> func_signature::percentile_disc =
{
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
};

std::vector<func_signature> func_signature::bigint_discrete =
{
  {PT_TYPE_BIGINT, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_NA}, {}}, //not needed???
};

std::vector<func_signature> func_signature::avg =
{
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
};

std::vector<func_signature> func_signature::double_number =
{
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
};

std::vector<func_signature> func_signature::count_star =
{
  {PT_TYPE_INTEGER, {}, {}},
};

std::vector<func_signature> func_signature::count =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_ANY}, {}},
};

std::vector<func_signature> func_signature::sum =
{
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_SET}, {}},
  {0, {PT_TYPE_MULTISET}, {}},
  {0, {PT_TYPE_SEQUENCE}, {}},
};

std::vector<func_signature> func_signature::double_r_any =
{
  {PT_TYPE_DOUBLE, {}, {}},
  {PT_TYPE_DOUBLE, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::ntile =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_NUMBER}, {}}, //argument value will be truncated at execution
};

/*cannot define a clear signature because casting depends on actual value
  MEDIAN('123456')     <=> MEDIAN(double) -> double
  MEDIAN('2018-03-14') <=> MEDIAN(date)   -> date  */
std::vector<func_signature> func_signature::median =
{
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER}, {}}, //if ret type is double => tests with median(int) will fail
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_TYPE_MAYBE}, {}}, //DISCUSSION: can we get rid of MAYBE here??? prepare median(?)...execute with date'2018-06-13'
};

std::vector<func_signature> func_signature::type0_nr_or_str =
{
  {0, {PT_GENERIC_TYPE_SCALAR}, {}},
};

std::vector<func_signature> func_signature::type0_nr_or_str_discrete =
{
  {0, {PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_ENUMERATION, PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
};

std::vector<func_signature> func_signature::group_concat =
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

std::vector<func_signature> func_signature::lead_lag =
{
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {0, {PT_GENERIC_TYPE_BIT}, {}},
  {0, {PT_GENERIC_TYPE_SEQUENCE}, {}},
};

std::vector<func_signature> func_signature::elt =
{
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARCHAR}}, //get_current_result() expects args to be VCHAR, not just equivalent
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARNCHAR}}, //get_current_result() expects args to be VNCHAR, not just equivalent
  {PT_TYPE_NULL, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
};

std::vector<func_signature> func_signature::insert =
{
  {PT_TYPE_VARCHAR, {PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR}, {}},
  {PT_TYPE_VARNCHAR, {PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER, PT_TYPE_INTEGER, 0}, {}},

  {0, {3, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR}, {}}, //for insert(?, i, i, n'nchar')
  {0, {3, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING}, {}}, //for insert(?, i, i, 'char or anything else')
};

std::vector<func_signature> func_signature::json_r_key_val =
{
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
};

std::vector<func_signature> func_signature::json_r_val =
{
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_JSON_VAL}},
};

std::vector<func_signature> func_signature::json_doc =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {}},
};

std::vector<func_signature> func_signature::json_doc_r_doc =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_DOC}},
};

std::vector<func_signature> func_signature::json_doc_path =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

std::vector<func_signature> func_signature::json_doc_r_path =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_STRING}},
};

std::vector<func_signature> func_signature::json_doc_str_r_path =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING}},
};

std::vector<func_signature> func_signature::json_doc_r_path_val =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
};

std::vector<func_signature> func_signature::json_contains_path =
{
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING}},
};

std::vector<func_signature> func_signature::json_keys =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {}},
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_STRING}, {}},
};

std::vector<func_signature> func_signature::json_search =
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

std::vector<func_signature> func_signature::json_arrayagg =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_VAL}, {}}
};

std::vector<func_signature> func_signature::json_objectagg =
{
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}, {}}
};

std::vector<func_signature> func_signature::set_r_any =
{
  {PT_TYPE_SET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::multiset_r_any =
{
  {PT_TYPE_MULTISET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::sequence_r_any =
{
  {PT_TYPE_SEQUENCE, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::generic =
{
  {0, {PT_GENERIC_TYPE_ANY}, {}},
};

std::vector<func_signature> *
func_signature::get_signatures (FUNC_TYPE ft)
{
  switch (ft)
    {
    case PT_MIN:
    case PT_MAX:
      return &type0_nr_or_str;
    case PT_SUM:
      return &sum;
    case PT_AVG:
      return &avg;
    case PT_STDDEV:
    case PT_VARIANCE:
    case PT_STDDEV_POP:
    case PT_VAR_POP:
    case PT_STDDEV_SAMP:
    case PT_VAR_SAMP:
      return &double_number;
    case PT_COUNT:
      return &count;
    case PT_COUNT_STAR:
      return &count_star;
    case PT_GROUPBY_NUM:
      return &bigint;
    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
      return &bigint_discrete;
    case PT_GROUP_CONCAT:
      return &group_concat;
    case PT_ROW_NUMBER:
    case PT_RANK:
    case PT_DENSE_RANK:
      return &integer;
    case PT_NTILE:
      return &ntile;
    case PT_TOP_AGG_FUNC:
      return nullptr;
    case PT_LEAD:
    case PT_LAG:
      return &lead_lag;
    case PT_GENERIC:
      return &generic;
    case F_SET:
    case F_TABLE_SET:
      return &set_r_any;
    case F_MULTISET:
    case F_TABLE_MULTISET:
      return &multiset_r_any;
    case F_SEQUENCE:
    case F_TABLE_SEQUENCE:
      return &sequence_r_any;
    case F_TOP_TABLE_FUNC:
      return nullptr;
    case F_MIDXKEY:
      return nullptr;
    case F_VID:
    case F_GENERIC:
    case F_CLASS_OF:
      return nullptr;
    case F_INSERT_SUBSTRING:
      return &insert;
    case F_ELT:
      return &elt;
    case F_JSON_ARRAY:
      return &json_r_val;
    case F_JSON_ARRAY_APPEND:
    case F_JSON_ARRAY_INSERT:
      return &json_doc_r_path_val;
    case F_JSON_CONTAINS_PATH:
      return &json_contains_path;
    case F_JSON_EXTRACT:
      return &json_doc_r_path;
    case F_JSON_GET_ALL_PATHS:
      return &json_doc;
    case F_JSON_INSERT:
      return &json_doc_r_path_val;
    case F_JSON_KEYS:
      return &json_keys;
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
      return &json_doc_r_doc;
    case F_JSON_OBJECT:
      return &json_r_key_val;
    case F_JSON_REMOVE:
      return &json_doc_r_path;
    case F_JSON_REPLACE:
      return &json_doc_r_path_val;
    case F_JSON_SEARCH:
      return &json_search;
    case F_JSON_SET:
      return &json_doc_r_path_val;
    case PT_FIRST_VALUE:
    case PT_LAST_VALUE:
      return &type0_nr_or_str;
    case PT_NTH_VALUE:
      return &type0_nr_or_str_discrete;
    case PT_MEDIAN:
      return &median;
    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
      return &double_r_any;
    case PT_PERCENTILE_CONT:
      return &percentile_cont;
    case PT_PERCENTILE_DISC:
      return &percentile_disc;
    case PT_JSON_ARRAYAGG:
      return &json_arrayagg;
    case PT_JSON_OBJECTAGG:
      return &json_objectagg;
    default:
      assert (false);
      return nullptr;
    }
}

const char *pt_func_type_to_string (FUNC_TYPE ft)
{
  switch (ft)
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
    case PT_VARIANCE:
      return "VARIANCE";
    case PT_STDDEV_POP:
      return "STDDEV_POP";
    case PT_VAR_POP:
      return "VAR_POP";
    case PT_STDDEV_SAMP:
      return "STDDEV_SAMP";
    case PT_VAR_SAMP:
      return "VAR_SAMP";
    case PT_COUNT:
      return "COUNT";
    case PT_COUNT_STAR:
      return "COUNT_STAR";
    case PT_GROUPBY_NUM:
      return "GROUPBY_NUM";
    case PT_AGG_BIT_AND:
      return "AGG_BIT_AND";
    case PT_AGG_BIT_OR:
      return "AGG_BIT_OR";
    case PT_AGG_BIT_XOR:
      return "AGG_BIT_XOR";
    case PT_GROUP_CONCAT:
      return "GROUP_CONCAT";
    case PT_ROW_NUMBER:
      return "ROW_NUMBER";
    case PT_RANK:
      return "RANK";
    case PT_DENSE_RANK:
      return "DENSE_RANK";
    case PT_NTILE:
      return "NTILE";
    case PT_TOP_AGG_FUNC:
      return "TOP_AGG_FUNC";
    case PT_LEAD:
      return "LEAD";
    case PT_LAG:
      return "LAG";
    case PT_GENERIC:
      return "GENERIC";
    case F_SET:
      return "F_SET";
    case F_TABLE_SET:
      return "F_TABLE_SET";
    case F_MULTISET:
      return "F_MULTISET";
    case F_TABLE_MULTISET:
      return "F_TABLE_MULTISET";
    case F_SEQUENCE:
      return "F_SEQUENCE";
    case F_TABLE_SEQUENCE:
      return "F_TABLE_SEQUENCE";
    case F_TOP_TABLE_FUNC:
      return "F_TOP_TABLE_FUNC";
    case F_MIDXKEY:
      return "F_MIDXKEY";
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
    case F_JSON_ARRAY:
      return "JSON_ARRAY";
    case F_JSON_ARRAY_APPEND:
      return "JSON_ARRAY_APPEND";
    case F_JSON_ARRAY_INSERT:
      return "JSON_ARRAY_INSERT";
    case F_JSON_CONTAINS_PATH:
      return "JSON_CONTAINS_PATH";
    case F_JSON_EXTRACT:
      return "JSON_EXTRACT";
    case F_JSON_GET_ALL_PATHS:
      return "JSON_GET_ALL_PATHS";
    case F_JSON_INSERT:
      return "JSON_INSERT";
    case F_JSON_KEYS:
      return "JSON_KEYS";
    case F_JSON_MERGE:
      return "JSON_MERGE";
    case F_JSON_MERGE_PATCH:
      return "JSON_MERGE_PATH";
    case F_JSON_OBJECT:
      return "JSON_OBJECT";
    case F_JSON_REMOVE:
      return "JSON_REMOVE";
    case F_JSON_REPLACE:
      return "JSON_REPLACE";
    case F_JSON_SEARCH:
      return "JSON_SEARCH";
    case F_JSON_SET:
      return "JSON_SET";
    case PT_FIRST_VALUE:
      return "FIRST_VALUE";
    case PT_LAST_VALUE:
      return "LAST_VALUE";
    case PT_NTH_VALUE:
      return "NTH_VALUE";
    case PT_MEDIAN:
      return "MEDIAN";
    case PT_CUME_DIST:
      return "CUME_DIST";
    case PT_PERCENT_RANK:
      return "PERCENT_RANK";
    case PT_PERCENTILE_CONT:
      return "PERCENTILE_CONT";
    case PT_PERCENTILE_DISC:
      return "PERCENTILE_DISC";
    case PT_JSON_ARRAYAGG:
      return "JSON_ARRAYAGG";
    case PT_JSON_OBJECTAGG:
      return "JSON_OBJECTAGG";
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

Func::signature_compatibility::signature_compatibility ()
  : m_compat (type_compatibility::INCOMPATIBLE)
  , m_args_resolve {}
  , m_collation_inference {}
  , m_signature (NULL)
{
  //
}

bool
Func::cmp_types_equivalent (const pt_arg_type &type, pt_type_enum type_enum)
{
  assert (type.type != pt_arg_type::INDEX);
  return type_enum == PT_TYPE_NULL || pt_are_equivalent_types (type, type_enum);
}

bool
Func::cmp_types_castable (const pt_arg_type &type, pt_type_enum type_enum) //is possible to cast type_enum -> type?
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

    case PT_GENERIC_TYPE_JSON_DOC:
    case PT_GENERIC_TYPE_JSON_VAL:
      // it will be resolved at runtime
      return PT_IS_NUMERIC_TYPE (type_enum);      // numerics can be converted to json

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
Func::Node::get_arg (size_t index)
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
Func::Node::apply_argument (parser_node *prev, parser_node *arg, const argument_resolve &arg_res)
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
  if (m_best_signature.m_collation_inference.coll_id != -1)
    {
      if (PT_HAS_COLLATION (arg_res.m_type)
	  && m_best_signature.m_collation_inference.coll_id != arg_res.m_coll_infer.coll_id)
	{
	  PT_NODE *new_node =
		  pt_coerce_node_collation (m_parser, arg, m_best_signature.m_collation_inference.coll_id,
					    m_best_signature.m_collation_inference.codeset,
					    arg_res.m_coll_infer.can_force_cs, false, arg_res.m_type, PT_TYPE_NONE);
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

bool
Func::Node::preprocess ()
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
				pt_show_function (PT_GROUP_CONCAT), pt_show_type_enum (arg1->type_enum),
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
Func::Node::get_types (const std::vector<func_signature> &signatures, size_t index, string_buffer &sb)
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
Func::Node::get_signature (const std::vector<func_signature> &signatures)
{
  if (pt_has_error (m_parser))
    {
      return nullptr;
    }
  pt_reset_error (m_parser);

  const func_signature *signature = nullptr;
  size_t arg_count = static_cast<size_t> (pt_length_of_list (m_node->info.function.arg_list));
  signature_compatibility sgn_compat;

  int sigIndex = 0;
  for (auto &sig: signatures)
    {
      ++sigIndex;
      parser_node *arg = m_node->info.function.arg_list;
      bool matchEquivalent = true;
      bool matchCastable = true;
      size_t arg_idx = 0;

      sgn_compat.m_args_resolve.resize (arg_count);
      sgn_compat.m_compat = type_compatibility::EQUIVALENT;
      sgn_compat.m_signature = &sig;

      //check fix part of the signature
      for (auto &fix: sig.fix)
	{
	  if (arg == NULL)
	    {
	      invalid_arg_count_error (arg_count, sig);
	      sgn_compat.m_compat = type_compatibility::INCOMPATIBLE;
	      break;
	    }
	  auto t = ((fix.type == pt_arg_type::INDEX) ? sig.fix[fix.val.index] : fix);

	  if (!check_arg_compat (t, arg, sgn_compat, sgn_compat.m_args_resolve[arg_idx]))
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
	  auto t = ((rep.type == pt_arg_type::INDEX) ? sig.rep[rep.val.index] : rep);

	  if (!check_arg_compat (t, arg, sgn_compat, sgn_compat.m_args_resolve[arg_idx]))
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
Func::Node::set_return_type (const func_signature &signature)
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
	  m_node->data_type->info.data_type.precision = TP_FLOATING_PRECISION_VALUE;
	}
    }
  else
    {
      // when is this already set??
    }

  // set collation on result node... I am not sure this is correct
  if (PT_HAS_COLLATION (m_node->type_enum) && m_best_signature.m_collation_inference.coll_id != -1)
    {
      pt_coll_infer result_coll_infer;
      if (pt_get_collation_info (m_node, &result_coll_infer)
	  && m_best_signature.m_collation_inference.coll_id != result_coll_infer.coll_id)
	{
	  parser_node *new_node = pt_coerce_node_collation (m_parser, m_node,
				  m_best_signature.m_collation_inference.coll_id,
				  m_best_signature.m_collation_inference.codeset,
				  true, false, PT_TYPE_VARCHAR, PT_TYPE_NONE);
	}
    }
}

bool
Func::Node::apply_signature (const func_signature &signature)
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
Func::Node::check_arg_compat (const pt_arg_type &arg_signature, const PT_NODE *arg_node,
			      signature_compatibility &compat, argument_resolve &arg_res)
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
  if (arg_res.m_type == PT_TYPE_NONE || arg_res.m_type == PT_TYPE_MAYBE)
    {
      assert (false);
      compat.m_compat = type_compatibility::INCOMPATIBLE;
      invalid_arg_error (arg_signature, arg_node, *compat.m_signature);
      return false;
    }

  // if argument type has collation and resolved type also has collation, we need to do collation inference
  //
  // if argument type does not have collation, it doesn't influence result collation (e.g. when casting int to string)
  //
  // if resolved type does not have collation, again it won't influence result collation (e.g. when casting string to
  // datetime)
  if (PT_HAS_COLLATION (arg_node->type_enum) && PT_HAS_COLLATION (arg_res.m_type))
    {
      int common_coll;
      INTL_CODESET common_cs;

      // get collation from node
      if (!pt_get_collation_info (arg_node, &arg_res.m_coll_infer))
	{
	  // ignore
	  return true;
	}

      if (compat.m_collation_inference.coll_id == -1)
	{
	  compat.m_collation_inference.coll_id = arg_res.m_coll_infer.coll_id;
	  compat.m_collation_inference.codeset = arg_res.m_coll_infer.codeset;
	  compat.m_collation_inference.can_force_cs = arg_res.m_coll_infer.can_force_cs;
	  compat.m_collation_inference.coerc_level = arg_res.m_coll_infer.coerc_level;
	}
      else if (pt_common_collation (&compat.m_collation_inference, &arg_res.m_coll_infer, NULL, 2, false, &common_coll,
				    &common_cs) == NO_ERROR)
	{
	  compat.m_collation_inference.coll_id = common_coll;
	  compat.m_collation_inference.codeset = common_cs;
	}
      else
	{
	  // todo: we'll need a clear error here
	  compat.m_compat = type_compatibility::INCOMPATIBLE;
	  invalid_coll_error (*compat.m_signature);
	  return false;
	}
    }
  return true;
}

void
Func::Node::invalid_arg_error (const pt_arg_type &arg_sgn, const PT_NODE *arg_node, const func_signature &func_sgn)
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
Func::Node::invalid_coll_error (const func_signature &func_sgn)
{
  string_buffer sgn_sb;
  func_sgn.to_string_buffer (sgn_sb);

  pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COLLATION_OP_ERROR,
		pt_show_function (m_node->info.function.function_type));
  pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_SIGNATURE,
		sgn_sb.get_buffer ());
}

void
Func::Node::invalid_arg_count_error (std::size_t arg_count, const func_signature &func_sgn)
{
  string_buffer sgn_sb;
  func_sgn.to_string_buffer (sgn_sb);

  pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_WRONG_ARGS_COUNT,
		(int) arg_count);
  pt_cat_error (m_parser, m_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INCOMPATIBLE_SIGNATURE,
		sgn_sb.get_buffer ());
}

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
      if (arg_type == PT_TYPE_MAYBE || arg_type == PT_TYPE_NULL)
	{
	  return PT_TYPE_JSON;
	}
      else if (pt_is_json_value_type (arg_type))
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
      if (arg_type == PT_TYPE_MAYBE || arg_type == PT_TYPE_NULL)
	{
	  return PT_TYPE_JSON;
	}
      else if (pt_is_json_doc_type (arg_type))
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

    default:
      return PT_TYPE_NONE;
    }

  return PT_TYPE_NONE;
}
