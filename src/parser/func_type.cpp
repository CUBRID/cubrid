#include "func_type.hpp"
#include "parse_tree.h"
#include "parser.h"

namespace JsonObj // json_object(key, val[, key, val...]) specific functions
{
  int f0(parser_context* parser_ctx, parser_node*& arg) //expect key name
  {
    if(!arg)
      {
        return -1; //OK - final state
      }
    return (pt_is_json_object_name(arg->type_enum) ? 1 : -2);
  }

  int f1(parser_context* parser_ctx, parser_node*& arg) //expect val
  {
    if(!arg)
      {
        return -2; //KO - not final state
      }
    if (!pt_is_json_value_type(arg->type_enum))
      {
        DB_TYPE db_type = pt_type_enum_to_db (arg->type_enum);
        arg = pt_wrap_with_cast_op(parser_ctx, arg, PT_TYPE_VARCHAR, 256, 0, 0);
        if (/*can't cast(t,v) to any json value type*/0)
        {
          //pt_coerce_expression_argument()
          //pt_wrap_with_cast_op()
          return -3; // KO - invalid type for value
        }
      }
    return 0; //OK, change state
  }
}

namespace JsonArr //json_array() specific functions
{
  int f0(parser_context* parser_ctx, parser_node*& arg) //expect val
  {
    if(!arg)
      {
        return MAXINT; //OK - final state
      }
    if (!pt_is_json_value_type(arg->type_enum))
      {
        if (/*can't cast(t,v) to any json value type*/0)
        {
          return -1; // KO - invalid type for value
        }
      }
    return 0; //OK, change state
  }
}

