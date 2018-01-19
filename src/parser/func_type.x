#ifndef X
#   pragma message(__FILE__ "("  "): warning: X-macro should be defined before including this header")
#endif

/* aggregate functions */
X(MIN)
X(MAX)
X(SUM)
X(AVG)
X(STDDEV)
X(VARIANCE)
X(STDDEV_POP)
X(VAR_POP)
X(STDDEV_SAMP)
X(VAR_SAMP)
X(COUNT)
X(COUNT_STAR)
X(GROUPBY_NUM)
X(AGG_BIT_AND)
X(AGG_BIT_OR)
X(AGG_BIT_XOR)
X(GROUP_CONCAT)
X(ROW_NUMBER)
X(RANK)
X(DENSE_RANK)
X(NTILE)
X(TOP_AGG_FUNC)
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* analytic only functions */
X(LEAD)
X(LAG)

/* foreign functions */
X(GENERIC_) //PT_GENERIC

/* from here down are function code common to parser and xasl */
/* "table" functions argument(s) are tables */
X(TABLE_SET)
X(TABLE_MULTISET)
X(TABLE_SEQUENCE)
X(TOP_TABLE_FUNC)
X(MIDXKEY)

/* "normal" functions, arguments are values */
X(SET)
X(MULTISET)
X(SEQUENCE)
X(VID)
X(GENERIC) //GENERIC
X(CLASS_OF)
X(INSERT_SUBSTRING)
X(ELT)
X(JSON_OBJECT, JsonObj::f0, JsonObj::f1)
X(JSON_ARRAY, JsonArr::f0)
X(JSON_MERGE)
X(JSON_INSERT)
X(JSON_REMOVE)

/* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
X(FIRST_VALUE)
X(LAST_VALUE)
X(NTH_VALUE)

/* aggregate and analytic functions */
X(MEDIAN)
X(CUME_DIST)
X(PERCENT_RANK)
X(PERCENTILE_CONT)
X(PERCENTILE_DIS)
