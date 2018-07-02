#ifndef _FUNC_TYPE_HPP_
#define _FUNC_TYPE_HPP_

#include "parse_type.hpp"
#include "string_buffer.hpp"
#include <vector>

struct parser_context;
struct parser_node;

struct func_signature
{
  pt_arg_type ret;              //return type
  std::vector<pt_arg_type> fix; //fixed arguments types
  std::vector<pt_arg_type> rep; //repetitive arguments types

  //signatures; naming convention: returnType_fixedType(s)_r_repetitiveType(s)
  static std::vector<func_signature> bigint;                //return bigint, no args
  static std::vector<func_signature> integer;               //return integer, no args
  static std::vector<func_signature> bigint_discrete;       //return bigint, arg: discrete
  static std::vector<func_signature> avg;
  static std::vector<func_signature> double_number;         //return double, arg: any number type number
  static std::vector<func_signature> count_star;
  static std::vector<func_signature> count;
  static std::vector<func_signature> sum;
  static std::vector<func_signature> double_r_any;
  static std::vector<func_signature> ntile;
  static std::vector<func_signature> median;
  static std::vector<func_signature> type0_nr_or_str;//return same type as argument 0, arg is numeric or string
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
  static std::vector<func_signature> json_doc_r_path_doc;

  static std::vector<func_signature> set_r_any;     //set, table_set
  static std::vector<func_signature> multiset_r_any;//multiset, table_multiset
  static std::vector<func_signature> sequence_r_any;//sequence, table_sequence

  static std::vector<func_signature> generic;

  static std::vector<func_signature>* get_signatures(FUNC_TYPE ft);//get all valid signatures for a given type
};

const char* str(const func_signature& signature, string_buffer& sb);
const char* str(FUNC_TYPE ft);

#endif
