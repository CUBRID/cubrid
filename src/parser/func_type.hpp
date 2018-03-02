#ifndef _FUNC_TYPE_HPP_
#define _FUNC_TYPE_HPP_

#include "parse_type.hpp"
#include <vector>

struct parser_context;
struct parser_node;

#define X(id, ...) _F_##id,
enum FUNC_TYPE2: int
{
  _0 = 899, //start value
  #include "func_type.x"
};
#undef X

struct func_signature
{
  pt_arg_type ret;              //return type
  std::vector<pt_arg_type> fix; //fixed arguments types
  std::vector<pt_arg_type> rep; //repetitive arguments types

  //signatures
  static std::vector<func_signature> bigint;                 //return bigint, no args
  static std::vector<func_signature> integer;                //return integer, no args
  static std::vector<func_signature> double_double01;        //return double, arg in [0,1]
  static std::vector<func_signature> bigint_discrete;        //return bigint, arg: discrete
  static std::vector<func_signature> double_number;          //return double, arg: any number type number
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
  static std::vector<func_signature> json_key_val_r_key_val;
  static std::vector<func_signature> json_val_r_val;
  static std::vector<func_signature> json_doc;
  static std::vector<func_signature> json_doc_r_doc;
  static std::vector<func_signature> json_doc_path;
  static std::vector<func_signature> json_doc_path_r_path;
  static std::vector<func_signature> json_doc_path_doc_r_path_doc;
};

#endif
