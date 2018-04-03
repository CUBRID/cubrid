#ifndef X
#pragma message(__FILE__ "("  "): warning: X-macro should be defined before including this header")
e.g.:
    //enum
    #define X(id, ...) FT_##id,
    enum FUNC_TYPE: int
    {
        _0 = 899, //start value
        #include "func_type.x"
    };
    #undef X

    //coresponding string for each enum value
    #define X(id, ...) #id,
    char* type_str[] = {
        #include "func_type.x"
    };
    #undef X

    //coresponding signature for each enum value
    #define X(id, signatures) signatures,
    std::vector<func_signature>* types[] = {
        #include "func_type.x"
    };
    #undef X
#endif

/* aggregate functions */
X(MIN                   , &func_signature::type0_nr_or_str)
X(MAX                   , &func_signature::type0_nr_or_str)
X(SUM                   , &func_signature::sum)
X(AVG                   , &func_signature::double_number)
X(STDDEV                , &func_signature::double_number)
X(VARIANCE              , &func_signature::double_number)
X(STDDEV_POP            , &func_signature::double_number)
X(VAR_POP               , &func_signature::double_number)
X(STDDEV_SAMP           , &func_signature::double_number)
X(VAR_SAMP              , &func_signature::double_number)
X(COUNT                 , &func_signature::count)
X(COUNT_STAR            , &func_signature::count_star)
X(GROUPBY_NUM           , &func_signature::bigint)
X(AGG_BIT_AND           , &func_signature::bigint_discrete)
X(AGG_BIT_OR            , &func_signature::bigint_discrete)
X(AGG_BIT_XOR           , &func_signature::bigint_discrete)
X(GROUP_CONCAT          , &func_signature::group_concat)
X(ROW_NUMBER            , &func_signature::integer)
X(RANK                  , &func_signature::integer)
X(DENSE_RANK            , &func_signature::integer)
X(NTILE                 , &func_signature::ntile)
X(TOP_AGG_FUNC          , NULL)
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* analytic only functions */
X(LEAD                  , &func_signature::lead_lag)
X(LAG                   , &func_signature::lead_lag)

/* foreign functions */
X(GENERIC_              , NULL) //PT_GENERIC

/* from here down are function code common to parser and xasl */
/* "table" functions argument(s) are tables */
X(TABLE_SET             , &func_signature::set_r_any)
X(TABLE_MULTISET        , &func_signature::multiset_r_any)
X(TABLE_SEQUENCE        , &func_signature::sequence_r_any)
X(TOP_TABLE_FUNC        , NULL)
X(MIDXKEY               , NULL)

/* "normal" functions, arguments are values */
X(SET                   , &func_signature::set_r_any)
X(MULTISET              , &func_signature::multiset_r_any)
X(SEQUENCE              , &func_signature::sequence_r_any)
X(VID                   , NULL)
X(GENERIC               , NULL)
X(CLASS_OF              , NULL)
X(INSERT_SUBSTRING      , &func_signature::insert)
X(ELT                   , &func_signature::elt)
X(JSON_OBJECT           , &func_signature::json_r_key_val)
X(JSON_ARRAY            , &func_signature::json_r_val)
X(JSON_MERGE            , &func_signature::json_doc_r_doc)
X(JSON_INSERT           , &func_signature::json_doc_r_path_doc)
X(JSON_REMOVE           , &func_signature::json_doc_r_path)
X(JSON_ARRAY_APPEND     , &func_signature::json_doc_r_path_doc)
X(JSON_GET_ALL_PATHS    , &func_signature::json_doc)
X(JSON_REPLACE          , &func_signature::json_doc_r_path_doc)
X(JSON_SET              , &func_signature::json_doc_r_path_doc)
X(JSON_KEYS             , &func_signature::json_doc_path)

/* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
X(FIRST_VALUE           , &func_signature::type0_nr_or_str)
X(LAST_VALUE            , &func_signature::type0_nr_or_str)
X(NTH_VALUE             , &func_signature::type0_nr_or_str_discrete)

/* aggregate and analytic functions */
X(MEDIAN                , &func_signature::median)
X(CUME_DIST             , &func_signature::double_r_any)
X(PERCENT_RANK          , &func_signature::double_r_any)
X(PERCENTILE_CONT       , &func_signature::percentile_cont)
X(PERCENTILE_DIS        , &func_signature::percentile_dis)
