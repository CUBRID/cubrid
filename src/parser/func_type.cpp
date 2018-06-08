#include "func_type.hpp"
#include "parse_tree.h"
#include "parser.h"

std::vector<func_signature> func_signature::integer = {
  {PT_TYPE_INTEGER, {}, {}},
};

std::vector<func_signature> func_signature::bigint = {
  {PT_TYPE_BIGINT, {}, {}},
};

std::vector<func_signature> func_signature::percentile_cont = {
#if 1
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_NUMBER  }, {}},
  {0                , {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_STRING  }, {}},
  {0                , {PT_TYPE_MAYBE           }, {}},
  {0                , {PT_TYPE_NA              }, {}},
#else //use double as return type (as documentation says)... but tests are failing (adjust doc or tests)
  {PT_TYPE_DOUBLE   , {PT_GENERIC_TYPE_NUMBER  }, {}},
  {0                , {PT_GENERIC_TYPE_STRING  }, {}},
  {PT_TYPE_DOUBLE   , {PT_GENERIC_TYPE_DATETIME}, {}},
  {0                , {PT_TYPE_MAYBE           }, {}},
  {0                , {PT_TYPE_NA              }, {}},
#endif
};

std::vector<func_signature> func_signature::percentile_disc = {
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_NUMBER  }, {}},
  {0                , {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_STRING  }, {}},
  {0                , {PT_TYPE_MAYBE           }, {}},
  {0                , {PT_TYPE_NA              }, {}},
};

std::vector<func_signature> func_signature::bigint_discrete = {
  {PT_TYPE_BIGINT, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_MAYBE                  }, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_NA                     }, {}},
};

std::vector<func_signature> func_signature::double_number = {
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {PT_TYPE_DOUBLE, {PT_TYPE_MAYBE}, {}},
  {PT_TYPE_DOUBLE, {PT_TYPE_NA}, {}},
};

std::vector<func_signature> func_signature::count_star = {
  {PT_TYPE_INTEGER, {}, {}},
};

std::vector<func_signature> func_signature::count = {
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_ANY}, {}},
  //{PT_TYPE_INTEGER, {PT_GENERIC_TYPE_NUMBER}, {}},
  //{PT_TYPE_INTEGER, {PT_GENERIC_TYPE_STRING}, {}},
  //{PT_TYPE_INTEGER, {PT_GENERIC_TYPE_DATETIME}, {}},
};

std::vector<func_signature> func_signature::sum = {
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_NA}, {}},
  {0, {PT_TYPE_SET}, {}},
  {0, {PT_TYPE_MULTISET}, {}},
  {0, {PT_TYPE_SEQUENCE}, {}},
};

std::vector<func_signature> func_signature::double_r_any = {//original code doesn't check arguments!!!
  {PT_TYPE_DOUBLE, {}, {                    }},
  {PT_TYPE_DOUBLE, {}, {PT_GENERIC_TYPE_ANY }},
};

std::vector<func_signature> func_signature::ntile = {//why original code cast args to double instead int???
  {PT_TYPE_INTEGER, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {PT_TYPE_INTEGER, {PT_TYPE_MAYBE}, {}},
};

/*cannot define a clear signature because casting depends on actual value
  MEDIAN('123456')     <=> MEDIAN(double) -> double
  MEDIAN('2018-03-14') <=> MEDIAN(date)   -> date  */
std::vector<func_signature> func_signature::median = {
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_NUMBER}  , {}}, //if ret type is double => tests with median(int) will fail
  {0                , {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE    , {PT_GENERIC_TYPE_STRING}  , {}}, //DISCUSSION: can we get rid of MAYBE???
  {0                , {PT_TYPE_MAYBE}           , {}},
};

std::vector<func_signature> func_signature::type0_nr_or_str = {
  {0, {PT_TYPE_ENUMERATION}, {}},
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {0, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_GENERIC_TYPE_BIT}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_NA}, {}},
};

std::vector<func_signature> func_signature::type0_nr_or_str_discrete = {
  {0, {PT_GENERIC_TYPE_NUMBER   , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING   , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_BIT      , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_ENUMERATION      , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_MAYBE            , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_NA               , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
};

std::vector<func_signature> func_signature::group_concat = {
  {PT_TYPE_VARCHAR  , {PT_TYPE_ENUMERATION    , PT_GENERIC_TYPE_CHAR  }, {}},//needed because pt_are_equivalent_types(PT_GENERIC_TYPE_CHAR, PT_TYPE_ENUMERATION) and casting to VCHR will affect order
  {PT_TYPE_VARCHAR  , {PT_TYPE_ENUMERATION    , PT_GENERIC_TYPE_NCHAR }, {}},

  //normal cases
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_CHAR   , PT_GENERIC_TYPE_CHAR  }, {}},
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NCHAR  , PT_GENERIC_TYPE_NCHAR }, {}},
  {PT_TYPE_VARBIT   , {PT_GENERIC_TYPE_BIT    , PT_GENERIC_TYPE_BIT   }, {}},

#if 0 //anything else should be casted to separator's type (if possible! makes sense to detect incompatible types when detecting/applying signatures?); NOTE: casting affects the order!!!
  {PT_TYPE_VARCHAR  , {1                        , PT_GENERIC_TYPE_CHAR  }, {}},//test
  {PT_TYPE_VARNCHAR , {1                        , PT_GENERIC_TYPE_NCHAR }, {}},//test
#else //anything else should be left untouched (like in the original code), maybe it will be casted later?
#if 0 //it allows group_concat(SET) but it should not!
//{PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_ANY      , PT_GENERIC_TYPE_CHAR  }, {}},
//{PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_ANY      , PT_GENERIC_TYPE_NCHAR }, {}},
#else //OK to keep the order but it allows cast (n)char -> number and it should not because group_concat(n'123', ', ') should be rejected?!
      //like that it allows group_concat(n'123', ', ') or group_concat(<nchar field>, ', ') when <nchar field> can be casted to double (acceptable for me)
      //but solved in preprocess for compatibility to original behaviour
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_NUMBER   , PT_GENERIC_TYPE_CHAR  }, {}},
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NUMBER   , PT_GENERIC_TYPE_NCHAR }, {}},
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_DATETIME , PT_GENERIC_TYPE_CHAR  }, {}},
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_DATETIME , PT_GENERIC_TYPE_NCHAR }, {}},
#endif
#endif
};

std::vector<func_signature> func_signature::lead_lag = {//original code doesn't do anything!!!
  {0, {PT_GENERIC_TYPE_NUMBER   }, {}},
  {0, {PT_GENERIC_TYPE_STRING   }, {}},
  {0, {PT_GENERIC_TYPE_DATETIME }, {}},
  {0, {PT_GENERIC_TYPE_BIT      }, {}},
  {0, {PT_GENERIC_TYPE_SEQUENCE }, {}},
};

std::vector<func_signature> func_signature::elt = {
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARCHAR     }},//get_current_result() expects args to be VCHAR, not just equivalent
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARNCHAR    }},//get_current_result() expects args to be VNCHAR, not just equivalent
//{PT_TYPE_NULL     , {PT_TYPE_NULL                   }, {PT_GENERIC_TYPE_ANY }},
//{PT_TYPE_NULL     , {PT_TYPE_NULL                   }, {                    }},
  {PT_TYPE_NULL     , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {                    }},
};

std::vector<func_signature> func_signature::insert = {
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_CHAR   , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_CHAR} , {}},
//{PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NCHAR  , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_NCHAR}, {}},
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NCHAR  , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , 0                    }, {}},

  {0                , {3                      , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_NCHAR}, {}},
  {0                , {3                      , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_STRING}, {}},
};

std::vector<func_signature> func_signature::json_r_key_val = {//(jsonKey, jsonVal[, jsonKey, jsonVal])
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
};

std::vector<func_signature> func_signature::json_r_val = {//(pt_is_json_value()[, pt_is_json_value()...])
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_JSON_VAL}},
};

std::vector<func_signature> func_signature::json_doc = {//(pt_is_json_doc_type())
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {}},
};

std::vector<func_signature> func_signature::json_doc_r_doc = {//(pt_is_json_doc_type()[, pt_is_json_doc()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_DOC}},
};

std::vector<func_signature> func_signature::json_doc_path = {//(pt_is_json_doc_type(), pt_is_json_path())
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_JSON_PATH}, {}},
};

std::vector<func_signature> func_signature::json_doc_r_path = {//(pt_is_json_doc_type(), pt_is_json_path()[, pt_is_json_path()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_PATH}},
};

std::vector<func_signature> func_signature::json_doc_r_path_doc = {//(pt_is_json_doc_type(), pt_is_json_path(), pt_is_json_doc() [, pt_is_json_path(), pt_is_json_doc()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_PATH, PT_GENERIC_TYPE_JSON_DOC}},
};

std::vector<func_signature> func_signature::set_r_any = {
  {PT_TYPE_SET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::multiset_r_any = {
  {PT_TYPE_MULTISET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::sequence_r_any = {
  {PT_TYPE_SEQUENCE, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature>* func_signature::get_signatures(FUNC_TYPE ft){
    switch(ft){
        case PT_MIN:
        case PT_MAX:
            return &type0_nr_or_str;
        case PT_SUM:
            return &sum;
        case PT_AVG:
        case PT_STDDEV:
        case PT_VARIANCE:
        case PT_STDDEV_POP:
        case PT_VAR_POP:
        case PT_STDDEV_SAMP:
        case PT_VAR_SAMP:
            return &double_number;
        case PT_COUNT:
            return &count;
        case PT_COUNT_STAR:
            return &count_star;
        case PT_GROUPBY_NUM:
            return &bigint;
        case PT_AGG_BIT_AND:
        case PT_AGG_BIT_OR:
        case PT_AGG_BIT_XOR:
            return &bigint_discrete;
        case PT_GROUP_CONCAT:
            return &group_concat;
        case PT_ROW_NUMBER:
        case PT_RANK:
        case PT_DENSE_RANK:
            return &integer;
        case PT_NTILE:
            return &ntile;
        case PT_TOP_AGG_FUNC:
            return nullptr;
        case PT_LEAD:
        case PT_LAG:
            return &lead_lag;
        case PT_GENERIC:
            return nullptr;
        case F_SET:
        case F_TABLE_SET:
            return &set_r_any;
        case F_MULTISET:
        case F_TABLE_MULTISET:
            return &multiset_r_any;
        case F_SEQUENCE:
        case F_TABLE_SEQUENCE:
            return &sequence_r_any;
        case F_TOP_TABLE_FUNC:
            return nullptr;
        case F_MIDXKEY:
            return nullptr;
        case F_VID:
        case F_GENERIC:
        case F_CLASS_OF:
            return nullptr;
        case F_INSERT_SUBSTRING:
            return &insert;
        case F_ELT:
            return &elt;
        case F_JSON_OBJECT:
            return &json_r_key_val;
        case F_JSON_ARRAY:
            return &json_r_val;
        case F_JSON_MERGE:
            return &json_doc_r_doc;
        case F_JSON_INSERT:
            return &json_doc_r_path_doc;
        case F_JSON_REMOVE:
            return &json_doc_r_path;
        case F_JSON_ARRAY_APPEND:
            return &json_doc_r_path_doc;
        case F_JSON_GET_ALL_PATHS:
            return &json_doc;
        case F_JSON_REPLACE:
            return &json_doc_r_path_doc;
        case F_JSON_SET:
            return &json_doc_r_path_doc;
        case F_JSON_KEYS:
            return &json_doc_path;
        case PT_FIRST_VALUE:
        case PT_LAST_VALUE:
            return &type0_nr_or_str;
        case PT_NTH_VALUE:
            return &type0_nr_or_str_discrete;
        case PT_MEDIAN:
            return &median;
        case PT_CUME_DIST:
        case PT_PERCENT_RANK:
            return &double_r_any;
        case PT_PERCENTILE_CONT:
            return &percentile_cont;
        case PT_PERCENTILE_DISC:
            return &percentile_disc;
        default:
            assert(false);
            return nullptr;
    }
}

const char* str(const func_signature& signature, string_buffer& sb)
{
    ::str(signature.ret, sb);
    sb(" (");
    for(auto i: signature.fix)
    {
        ::str(i, sb);
    }
    sb(")");
    return sb.get_buffer();
}

const char* str(FUNC_TYPE ft)
{
    switch(ft){
        case PT_MIN:
            return "PT_MIN";
        case PT_MAX:
            return "PT_MAX";
        case PT_SUM:
            return "PT_SUM";
        case PT_AVG:
            return "PT_AVG";
        case PT_STDDEV:
            return "PT_STDDEV";
        case PT_VARIANCE:
            return "PT_VARIANCE";
        case PT_STDDEV_POP:
            return "PT_STDDEV_POP";
        case PT_VAR_POP:
            return "PT_VAR_POP";
        case PT_STDDEV_SAMP:
            return "PT_STDDEV_SAMP";
        case PT_VAR_SAMP:
            return "PT_VAR_SAMP";
        case PT_COUNT:
            return "PT_COUNT";
        case PT_COUNT_STAR:
            return "PT_COUNT_STAR";
        case PT_GROUPBY_NUM:
            return "PT_GROUPBY_NUM";
        case PT_AGG_BIT_AND:
            return "PT_AGG_BIT_AND";
        case PT_AGG_BIT_OR:
            return "PT_AGG_BIT_OR";
        case PT_AGG_BIT_XOR:
            return "PT_AGG_BIT_XOR";
        case PT_GROUP_CONCAT:
            return "PT_GROUP_CONCAT";
        case PT_ROW_NUMBER:
            return "PT_ROW_NUMBER";
        case PT_RANK:
            return "PT_RANK";
        case PT_DENSE_RANK:
            return "PT_DENSE_RANK";
        case PT_NTILE:
            return "PT_NTILE";
        case PT_TOP_AGG_FUNC:
            return "PT_TOP_AGG_FUNC";
        case PT_LEAD:
            return "PT_LEAD";
        case PT_LAG:
            return "PT_LAG";
        case PT_GENERIC:
            return "PT_GENERIC";
        case F_SET:
            return "F_SET";
        case F_TABLE_SET:
            return "F_TABLE_SET";
        case F_MULTISET:
            return "F_MULTISET";
        case F_TABLE_MULTISET:
            return "F_TABLE_MULTISET";
        case F_SEQUENCE:
            return "F_SEQUENCE";
        case F_TABLE_SEQUENCE:
            return "F_TABLE_SEQUENCE";
        case F_TOP_TABLE_FUNC:
            return "F_TOP_TABLE_FUNC";
        case F_MIDXKEY:
            return "F_MIDXKEY";
        case F_VID:
            return "F_VID";
        case F_GENERIC:
            return "F_GENERIC";
        case F_CLASS_OF:
            return "F_CLASS_OF";
        case F_INSERT_SUBSTRING:
            return "F_INSERT_SUBSTRING";
        case F_ELT:
            return "F_ELT";
        case F_JSON_OBJECT:
            return "F_JSON_OBJECT";
        case F_JSON_ARRAY:
            return "F_JSON_ARRAY";
        case F_JSON_MERGE:
            return "F_JSON_MERGE";
        case F_JSON_INSERT:
            return "F_JSON_INSERT";
        case F_JSON_REMOVE:
            return "F_JSON_REMOVE";
        case F_JSON_ARRAY_APPEND:
            return "F_JSON_ARRAY_APPEND";
        case F_JSON_GET_ALL_PATHS:
            return "F_JSON_GET_ALL_PATHS";
        case F_JSON_REPLACE:
            return "F_JSON_REPLACE";
        case F_JSON_SET:
            return "F_JSON_SET";
        case F_JSON_KEYS:
            return "F_JSON_KEYS";
        case PT_FIRST_VALUE:
            return "PT_FIRST_VALUE";
        case PT_LAST_VALUE:
            return "PT_LAST_VALUE";
        case PT_NTH_VALUE:
            return "PT_NTH_VALUE";
        case PT_MEDIAN:
            return "PT_MEDIAN";
        case PT_CUME_DIST:
            return "PT_CUME_DIST";
        case PT_PERCENT_RANK:
            return "PT_PERCENT_RANK";
        case PT_PERCENTILE_CONT:
            return "PT_PERCENTILE_CONT";
        case PT_PERCENTILE_DISC:
            return "PT_PERCENTILE_DISC";
        default:
            assert(false);
            return nullptr;
    }
}
