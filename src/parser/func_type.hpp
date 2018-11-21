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
 * func_type.hpp
 */

#ifndef _FUNC_TYPE_HPP_
#define _FUNC_TYPE_HPP_

#include "parse_type.hpp"
#include "string_buffer.hpp"
#include <vector>

struct parser_context;
struct parser_node;

struct func_signature
{
  pt_arg_type ret; //return type
  std::vector<pt_arg_type> fix; //fixed arguments types
  std::vector<pt_arg_type> rep; //repetitive arguments types

  //signatures; naming convention: returnType_fixedType(s)_r_repetitiveType(s)
  static std::vector<func_signature> bigint; //return bigint, no args
  static std::vector<func_signature> integer; //return integer, no args
  static std::vector<func_signature> bigint_discrete; //return bigint, arg: discrete
  static std::vector<func_signature> avg;
  static std::vector<func_signature> double_number; //return double, arg: any number type number
  static std::vector<func_signature> count_star;
  static std::vector<func_signature> count;
  static std::vector<func_signature> sum;
  static std::vector<func_signature> double_r_any;
  static std::vector<func_signature> ntile;
  static std::vector<func_signature> median;
  static std::vector<func_signature> type0_nr_or_str; //return same type as argument 0, arg is numeric or string
  static std::vector<func_signature> type0_nr_or_str_discrete;
  static std::vector<func_signature> group_concat;
  static std::vector<func_signature> lead_lag;
  static std::vector<func_signature> elt;
  static std::vector<func_signature> insert;
  static std::vector<func_signature> percentile_cont;
  static std::vector<func_signature> percentile_disc;
  static std::vector<func_signature> json_r_key_val;
  static std::vector<func_signature> json_r_val;
  static std::vector<func_signature> json_doc;
  static std::vector<func_signature> json_doc_r_doc;
  static std::vector<func_signature> json_doc_path;
  static std::vector<func_signature> json_doc_r_path;
  static std::vector<func_signature> json_doc_str_r_path;
  static std::vector<func_signature> json_doc_r_path_val;
  static std::vector<func_signature> json_contains_path;
  static std::vector<func_signature> json_search;
  static std::vector<func_signature> json_arrayagg;
  static std::vector<func_signature> json_objectagg;
  static std::vector<func_signature> set_r_any; //set, table_set
  static std::vector<func_signature> multiset_r_any; //multiset, table_multiset
  static std::vector<func_signature> sequence_r_any; //sequence, table_sequence

  static std::vector<func_signature> generic;

  static std::vector<func_signature> *get_signatures (FUNC_TYPE ft); //get all valid signatures for a given type

  void to_string_buffer (string_buffer &sb) const;
};

const char *pt_func_type_to_string (FUNC_TYPE ft);


bool pt_are_equivalent_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type);
PT_TYPE_ENUM pt_get_equivalent_type (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM arg_type);


namespace Func
{
  // helpers
  enum class type_compatibility
  {
    EQUIVALENT,
    COERCIBLE,
    INCOMPATIBLE
  };

  struct argument_compatibility
  {
    type_compatibility m_compat;
    PT_TYPE_ENUM m_type;
  };

  struct signature_compatibility
  {
    type_compatibility m_signature_compat;
    std::vector<argument_compatibility> m_args_compat;
  };

  bool cmp_types_equivalent (const pt_arg_type &type, pt_type_enum type_enum);
  bool cmp_types_castable (const pt_arg_type &type, pt_type_enum type_enum);

  class Node
  {
    private:
      parser_context *m_parser;
      parser_node *m_node;
      signature_compatibility m_compat;

    public:
      Node (parser_context *parser, parser_node *node)
	: m_parser (parser)
	, m_node (node)
      {
      }

      parser_node *get_arg (size_t index);

      //cast given argument to specified type and re-link
      parser_node *cast (parser_node *prev, parser_node *arg, pt_type_enum type, int p, int s, parser_node *dt);

      bool preprocess(); //preprocess current function node type for special cases
      const func_signature *get_signature (const std::vector<func_signature> &signatures);
      void set_return_type (const func_signature &signature); //set return type for current node in current context
      bool apply_signature (const func_signature &signature); //apply function signature with casts if necessary
    protected:
      const char *get_types (const std::vector<func_signature> &signatures, size_t index, string_buffer &sb);
      void check_arg_compat (const pt_arg_type &arg_signature, const PT_NODE *arg_node,
			     argument_compatibility &compat);
      void invalid_arg_error (const pt_arg_type &arg_sgn, const PT_NODE *arg_node, const func_signature &func_sgn);
      void invalid_arg_count_error (std::size_t arg_count, const func_signature &func_sgn);
  }; //class Node
} //namespace Func

#endif
