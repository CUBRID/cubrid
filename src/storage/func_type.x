#ifndef X
#   pragma message(__FILE__ "("  "): warning: X-macro should be defined before including this header")
#endif

  /* aggregate functions */
  X(PT_MIN)
  X(PT_MAX)
  X(PT_SUM)
  X(PT_AVG)
  X(PT_STDDEV)
  X(PT_VARIANCE)
  X(PT_STDDEV_POP)
  X(PT_VAR_POP)
  X(PT_STDDEV_SAMP)
  X(PT_VAR_SAMP)
  X(PT_COUNT)
  X(PT_COUNT_STAR)
  X(PT_GROUPBY_NUM)
  X(PT_AGG_BIT_AND)
  X(PT_AGG_BIT_OR)
  X(PT_AGG_BIT_XOR)
  X(PT_GROUP_CONCAT)
  X(PT_ROW_NUMBER)
  X(PT_RANK)
  X(PT_DENSE_RANK)
  X(PT_NTILE)
  X(PT_TOP_AGG_FUNC)
  /* only aggregate functions should be below PT_TOP_AGG_FUNC */

  /* analytic only functions */
  X(PT_LEAD)
  X(PT_LAG)

  /* foreign functions */
  X(PT_GENERIC)

  /* from here down are function code common to parser and xasl */
  /* "table" functions argument(s) are tables */
  X(F_TABLE_SET)
  X(F_TABLE_MULTISET)
  X(F_TABLE_SEQUENCE)
  X(F_TOP_TABLE_FUNC)
  X(F_MIDXKEY)

  /* "normal" functions, arguments are values */
  X(F_SET)
  X(F_MULTISET)
  X(F_SEQUENCE)
  X(F_VID)
  X(F_GENERIC)
  X(F_CLASS_OF)
  X(F_INSERT_SUBSTRING)
  X(F_ELT)
  X(F_JSON_OBJECT, JsonObj::f0, JsonObj::f1)
  X(F_JSON_ARRAY, JsonArr::f0)
  X(F_JSON_MERGE)
  X(F_JSON_INSERT)
  X(F_JSON_REMOVE)

  /* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
  X(PT_FIRST_VALUE)
  X(PT_LAST_VALUE)
  X(PT_NTH_VALUE)

  /* aggregate and analytic functions */
  X(PT_MEDIAN)
  X(PT_CUME_DIST)
  X(PT_PERCENT_RANK)
  X(PT_PERCENTILE_CONT)
  X(PT_PERCENTILE_DIS)
