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

struct func_type
{
  parse_type ret;                            //return type
  std::vector<std::vector<parse_type>> fix;  //fixed types for arguments
  std::vector<std::vector<parse_type>> rep;  //repetitive types for arguments
};

namespace sig
{
  extern func_type bigint;//return bigint, no args
  extern func_type integer;//return integer, no args
  extern func_type discrete_cast_to_bigint;
  extern func_type numeric_cast_to_double;
  extern func_type count_star;
  extern func_type count;
  extern func_type sum;
  extern func_type cume_dist;
  extern func_type ntile;
  extern func_type double_01;//return double, arg in [0,1]
  extern func_type median;
  extern func_type type0_nr_or_str;//return same type as argument, arg is numeric or string
  extern func_type group_concat;
  extern func_type lead_lag;
  extern func_type elt;
  extern func_type insert;

  extern func_type json_key_val_r_key_val;
  extern func_type json_val_r_val;
  extern func_type json_doc;
  extern func_type json_doc_r_doc;
  extern func_type json_doc_path;
  extern func_type json_doc_path_r_path;
  extern func_type json_doc_path_doc_r_path_doc;
}

#endif
