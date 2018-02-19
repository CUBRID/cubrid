#ifndef X
#   pragma message(__FILE__ "("  "): warning: X-macro should be defined before including this header")
#endif

/* aggregate functions */
X(MIN                   , &sig::type0_nr_or_str)
X(MAX                   , &sig::type0_nr_or_str)
X(SUM                   , &sig::sum)
X(AVG                   , &sig::numeric_cast_to_double)
X(STDDEV                , &sig::numeric_cast_to_double)
X(VARIANCE              , &sig::numeric_cast_to_double)
X(STDDEV_POP            , &sig::numeric_cast_to_double)
X(VAR_POP               , &sig::numeric_cast_to_double)
X(STDDEV_SAMP           , &sig::numeric_cast_to_double)
X(VAR_SAMP              , &sig::numeric_cast_to_double)
X(COUNT                 , &sig::count)
X(COUNT_STAR            , &sig::count_star)
X(GROUPBY_NUM           , &sig::bigint)
X(AGG_BIT_AND           , &sig::discrete_cast_to_bigint)
X(AGG_BIT_OR            , &sig::discrete_cast_to_bigint)
X(AGG_BIT_XOR           , &sig::discrete_cast_to_bigint)
X(GROUP_CONCAT          , &sig::group_concat)
X(ROW_NUMBER            , &sig::integer)
X(RANK                  , &sig::integer)
X(DENSE_RANK            , &sig::integer)
X(NTILE                 , &sig::ntile)
X(TOP_AGG_FUNC          , NULL)
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* analytic only functions */
X(LEAD                  , &sig::lead_lag)
X(LAG                   , &sig::lead_lag)

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
X(INSERT_SUSTRING       , &sig::insert)
X(ELT                   , &sig::elt)
X(JSON_OBJECT           , &sig::json_key_val_r_key_val)
X(JSON_ARRAY            , &sig::json_val_r_val)
X(JSON_MERGE            , &sig::json_doc_r_doc)
X(JSON_INSERT           , &sig::json_doc_path_doc_r_path_doc)
X(JSON_REMOVE           , &sig::json_doc_path_r_path)
X(JSON_ARRAY_APPEND     , &sig::json_doc_path_doc_r_path_doc)
X(JSON_GET_ALL_PATHS    , &sig::json_doc)
X(JSON_REPLACE          , &sig::json_doc_path_doc_r_path_doc)
X(JSON_SET              , &sig::json_doc_path_doc_r_path_doc)
X(JSON_KEYS             , &sig::json_doc_path)

/* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
X(FIRST_VALUE           , &sig::type0_nr_or_str)
X(LAST_VALUE            , &sig::type0_nr_or_str)
X(NTH_VALUE             , &sig::type0_nr_or_str)

/* aggregate and analytic functions */
X(MEDIAN                , &sig::median)
X(CUME_DIST             , &sig::cume_dist)
X(PERCENT_RANK          , &sig::cume_dist)
X(PERCENTILE_CONT       , &sig::double_01)
X(PERCENTILE_DIS        , &sig::double_01)
