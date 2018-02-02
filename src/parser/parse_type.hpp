#ifndef _PARSER_TYPE_HPP_
#define _PARSER_TYPE_HPP_

#include "parse_tree.h"

/* generic types */
typedef enum pt_generic_type_enum
{
  PT_GENERIC_TYPE_NONE,
  PT_GENERIC_TYPE_STRING,	/* any type of string */
  PT_GENERIC_TYPE_STRING_VARYING,	/* VARCHAR or VARNCHAR */
  PT_GENERIC_TYPE_CHAR,		/* VARCHAR or CHAR */
  PT_GENERIC_TYPE_NCHAR,	/* VARNCHAR or NCHAR */
  PT_GENERIC_TYPE_BIT,		/* BIT OR VARBIT */
  PT_GENERIC_TYPE_DISCRETE_NUMBER,	/* SMALLINT, INTEGER, BIGINTEGER */
  PT_GENERIC_TYPE_NUMBER,	/* any number type */
  PT_GENERIC_TYPE_DATE,		/* date, datetime or timestamp */
  PT_GENERIC_TYPE_DATETIME,	/* any date or time type */
  PT_GENERIC_TYPE_SEQUENCE,	/* any type of sequence */
  PT_GENERIC_TYPE_LOB,		/* BLOB or CLOB */
  PT_GENERIC_TYPE_QUERY,	/* Sub query (for range operators) */
  PT_GENERIC_TYPE_PRIMITIVE,	/* primitive types */
  PT_GENERIC_TYPE_ANY		/* any type */
} PT_GENERIC_TYPE_ENUM;

struct parse_type
{
  enum {NORMAL, GENERIC, INDEX} type;
  union
  {
    PT_TYPE_ENUM normal;
    PT_GENERIC_TYPE_ENUM generic;
    size_t index;
  };

  parse_type(pt_type_enum type=PT_TYPE_NONE)
    : type(NORMAL)
    , normal(type)
  {}

  parse_type(pt_generic_type_enum generic_type)
    : type(GENERIC)
    , generic(generic_type)
  {}

  parse_type(size_t index)
    : type(INDEX)
    , index(index)
  {}

  void operator()(pt_type_enum normal_type)
  {
    type = NORMAL;
    normal = normal_type;
  }

  void operator()(pt_generic_type_enum generic_type)
  {
    type = GENERIC;
    generic = generic_type;
  }

  void operator()(size_t index_type)
  {
    type = INDEX;
    index = index_type;
  }

  void operator=(pt_type_enum normal_type)
  {
    type = NORMAL;
    normal = normal_type;
  }

};

#endif // _PARSER_TYPE_HPP_
