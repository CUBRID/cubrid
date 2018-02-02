#ifndef X
#   pragma message(__FILE__ "("  "): warning: X-macro should be defined before including this header")
#endif

/* aggregate functions */
X(MIN                   , NULL)
X(MAX                   , NULL)
X(SUM                   , NULL)
X(AVG                   , NULL)
X(STDDEV                , NULL)
X(VARIANCE              , NULL)
X(STDDEV_POP            , NULL)
X(VAR_POP               , NULL)
X(STDDEV_SAMP           , NULL)
X(VAR_SAMP              , NULL)
X(COUNT                 , NULL)
X(COUNT_STAR            , NULL)
X(GROUPBY_NUM           , NULL)
X(AGG_BIT_AND           , NULL)
X(AGG_BIT_OR            , NULL)
X(AGG_BIT_XOR           , NULL)
X(GROUP_CONCAT          , NULL)
X(ROW_NUMBER            , NULL)
X(RANK                  , NULL)
X(DENSE_RANK            , NULL)
X(NTILE                 , NULL)
X(TOP_AGG_FUNC          , NULL)
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* analytic only functions */
X(LEAD                  , NULL)
X(LAG                   , NULL)

/* foreign functions */
X(GENERIC_              , NULL) //PT_GENERIC

/* from here down are function code common to parser and xasl */
/* "table" functions argument(s) are tables */
X(TABLE_SET             , NULL)
X(TABLE_MULTISET        , NULL)
X(TABLE_SEQUENCE        , NULL)
X(TOP_TABLE_FUNC        , NULL)
X(MIDXKEY               , NULL)

/* "normal" functions, arguments are values */
X(SET                   , NULL)
X(MULTISET              , NULL)
X(SEQUENCE              , NULL)
X(VID                   , NULL)
X(GENERIC               , NULL)
X(CLASS_OF              , NULL)
X(INSERT_SUSTRING       , NULL)
X(ELT                   , NULL)
X(JSON_OBJECT           , &sig::json_key_val)
X(JSON_ARRAY            , &sig::json_val)
X(JSON_MERGE            , NULL)
X(JSON_INSERT           , NULL)
X(JSON_REMOVE           , NULL)

/* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
X(FIRST_VALUE           , NULL)
X(LAST_VALUE            , NULL)
X(NTH_VALUE             , NULL)

/* aggregate and analytic functions */
X(MEDIAN                , NULL)
X(CUME_DIST             , NULL)
X(PERCENT_RANK          , NULL)
X(PERCENTILE_CONT       , NULL)
X(PERCENTILE_DIS        , NULL)
