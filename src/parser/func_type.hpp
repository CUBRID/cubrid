#ifndef _FUNC_TYPE_HPP_
#define _FUNC_TYPE_HPP_

struct parser_context;
struct parser_node;

#define X(id, ...) _F_##id,
enum FUNC_TYPE2: int
{
  _0 = 899, //start value
  #include "func_type.x"
};
#undef X

namespace JsonObj // json_object(key, val[, key, val...]) specific functions
{
  int f0(parser_context* parser_ctx, parser_node*& arg); //expect key
  int f1(parser_context* parser_ctx, parser_node*& arg); //expect val
}

namespace JsonArr //json_array(val[, ...]) specific functions
{
  int f0(parser_context* parser_ctx, parser_node*& arg); //expect val
}


#endif
