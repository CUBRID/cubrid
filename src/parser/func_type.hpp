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
  extern func_type json_key_val;
  extern func_type json_val;
}

#endif
