#include "parse_type.hpp"
#include "parser.h"
#include "string_buffer.hpp"

const char* str(pt_generic_type_enum type)
{
  static const char* arr[] = {
    "GT_NONE",
    "GT_STRING",
    "GT_STRING_VARYING",
    "GT_CHAR",
    "GT_NCHAR",
    "GT_BIT",
    "GT_DISCRETE_NUMBER",
    "GT_NUMBER",
    "GT_DATE",
    "GT_DATETIME",
    "GT_SEQUENCE",
    "GT_LOB",
    "GT_QUERY",
    "GT_PRIMITIVE",
    "GT_ANY",
    "GT_JSON_VAL",
    "GT_JSON_DOC",
    "GT_JSON_PATH",
    "GT_SCALAR",
  };
  return arr[type];
}

//--------------------------------------------------------------------------------
const char* str(const pt_arg_type& type, string_buffer& sb)
{
  switch (type.type)
    {
    case pt_arg_type::NORMAL:
      //sb("%s", str(type.val.type));
      sb("%s", pt_show_type_enum(type.val.type));
      break;
    case pt_arg_type::GENERIC:
      sb("%s", str(type.val.generic_type));
      break;
    case pt_arg_type::INDEX:
      sb("IDX%d", type.val.index);
      break;
    }
  return sb.get_buffer();
}
