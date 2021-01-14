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
 * func_type.hpp
 */

#ifndef _FUNC_TYPE_HPP_
#define _FUNC_TYPE_HPP_

#include "parse_type.hpp"
#include "string_buffer.hpp"
#include <vector>

struct parser_context;
struct parser_node;

using pt_group_arg_type = std::vector<pt_arg_type>;
struct func_signature
{
  pt_arg_type ret; //return type
  pt_group_arg_type fix; //fixed arguments types
  pt_group_arg_type rep; //repetitive arguments types

  void to_string_buffer (string_buffer &sb) const;
};
using func_all_signatures = std::vector<func_signature>;

namespace func_type
{
  // helpers
  enum class type_compatibility
  {
    EQUIVALENT,
    COERCIBLE,
    INCOMPATIBLE
  };

  struct argument_resolve
  {
    PT_TYPE_ENUM m_type;
    bool m_check_coll_infer;
    pt_coll_infer m_coll_infer;

    argument_resolve ();
  };

  struct signature_compatibility
  {
    type_compatibility m_compat;
    std::vector<argument_resolve> m_args_resolve;
    pt_coll_infer m_common_collation;
    TP_DOMAIN_COLL_ACTION m_collation_action;

    const func_signature *m_signature;

    signature_compatibility ();
  };

  bool cmp_types_equivalent (const pt_arg_type &type, pt_type_enum type_enum);
  bool cmp_types_castable (const pt_arg_type &type, pt_type_enum type_enum);

  bool is_type_with_collation (PT_TYPE_ENUM type);
  bool can_signature_have_collation (const pt_arg_type &arg_sig);
  bool sig_has_json_args (const func_signature &sig);

  class Node
  {
    private:
      parser_context *m_parser;
      parser_node *m_node;
      signature_compatibility m_best_signature;

    public:
      Node (parser_context *parser, parser_node *node)
	: m_parser (parser)
	, m_node (node)
      {
      }

      parser_node *get_arg (size_t index);

      PT_NODE *type_checking ();

    protected:
      bool preprocess(); //preprocess current function node type for special cases
      const func_signature *get_signature (const func_all_signatures &signatures);
      void set_return_type (const func_signature &signature); //set return type for current node in current context
      bool apply_signature (const func_signature &signature); //apply function signature with casts if necessary

      parser_node *apply_argument (parser_node *prev, parser_node *arg, const argument_resolve &arg_res);

      const char *get_types (const func_all_signatures &signatures, size_t index, string_buffer &sb);
      bool check_arg_compat (const pt_arg_type &arg_signature, const PT_NODE *arg_node,
			     signature_compatibility &compat, argument_resolve &resolved_type, bool args_to_utf8);
      void invalid_arg_error (const pt_arg_type &arg_sgn, const PT_NODE *arg_node, const func_signature &func_sgn);
      void invalid_coll_error (const func_signature &func_sgn);
      void invalid_arg_count_error (std::size_t arg_count, const func_signature &func_sgn);
  }; //class Node
} //namespace func_type

bool pt_are_equivalent_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type);
PT_TYPE_ENUM pt_get_equivalent_type (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM arg_type);

#endif
