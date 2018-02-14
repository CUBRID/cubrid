#include "func_type.hpp"
#include "parse_tree.h"
#include "parser.h"

func_type sig::json_key_val = {
  PT_TYPE_JSON, //return type(s)
#if 1
  //1st arg type , 2nd arg type ...
  {{PT_TYPE_CHAR}, {PT_TYPE_CHAR, PT_TYPE_NUMERIC, PT_TYPE_INTEGER, PT_TYPE_DOUBLE}},
  {{PT_TYPE_CHAR}, {PT_TYPE_CHAR, PT_TYPE_NUMERIC, PT_TYPE_INTEGER, PT_TYPE_DOUBLE}},
#else
  {{PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_NUMBER}},
  {{PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_NUMBER}},
#endif
};

func_type sig::json_val = {
  PT_TYPE_JSON,                         //return type(s)
  {{PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}},  //fixed arguments types
  {{PT_TYPE_VARCHAR, PT_TYPE_DOUBLE}},  //repetitive arguments types
};
