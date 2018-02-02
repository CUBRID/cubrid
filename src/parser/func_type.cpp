#include "func_type.hpp"
#include "parse_tree.h"
#include "parser.h"

//namespace sig
//{
  func_type sig::json_key_val = {PT_TYPE_JSON, {{PT_TYPE_VARCHAR}, {PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}}, {{PT_TYPE_VARCHAR}, {PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}}};
  func_type sig::json_val = {parse_type(PT_TYPE_JSON), {{PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}}, {{PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}}};
//}
