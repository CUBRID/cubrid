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
#if 0
//{0, {PT_GENERIC_TYPE_ANY}, {}},//tests fail!
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_ANY}, {}},
#else
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER  }, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING  }, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_MAYBE           }, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_NULL            }, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_NA              }, {}},
#endif
};

std::vector<func_signature> func_signature::percentile_dis = {
#if 0
//{0, {PT_GENERIC_TYPE_ANY}, {}},//tests fail!
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_ANY}, {}},
#else
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_NUMBER  }, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_STRING  }, {}},
  {PT_TYPE_MAYBE, {PT_GENERIC_TYPE_DATETIME}, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_MAYBE           }, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_NULL            }, {}},
  {PT_TYPE_MAYBE, {PT_TYPE_NA              }, {}},
#endif
};

std::vector<func_signature> func_signature::bigint_discrete = {
  {PT_TYPE_BIGINT, {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_MAYBE}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_NULL}, {}},
  {PT_TYPE_BIGINT, {PT_TYPE_NA}, {}},
};

std::vector<func_signature> func_signature::double_number = {
  {PT_TYPE_DOUBLE, {PT_GENERIC_TYPE_NUMBER}, {}},
  {PT_TYPE_DOUBLE, {PT_TYPE_MAYBE}, {}},
  {PT_TYPE_DOUBLE, {PT_TYPE_NULL}, {}},
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
  {0, {PT_TYPE_NULL}, {}},
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
  {PT_TYPE_INTEGER, {PT_TYPE_NULL}, {}},
};

/*cannot define a clear signature because casting depends on actual value
  MEDIAN('123456')     => MEDIAN(double)
  MEDIAN('2018-03-14') => MEDIAN(date)*/
std::vector<func_signature> func_signature::median = {
//{PT_TYPE_DOUBLE , {PT_GENERIC_TYPE_NUMBER}  , {}},
  {PT_TYPE_MAYBE  , {PT_GENERIC_TYPE_NUMBER}  , {}},
  {0              , {PT_GENERIC_TYPE_DATETIME}, {}},

//{PT_TYPE_MAYBE  , {PT_GENERIC_TYPE_ANY}     , {}}, //let evaluation select the return type
  {PT_TYPE_MAYBE  , {PT_TYPE_NULL}            , {}},
  {PT_TYPE_MAYBE  , {PT_TYPE_MAYBE}           , {}},
  {PT_TYPE_MAYBE  , {PT_GENERIC_TYPE_STRING}  , {}},
};

std::vector<func_signature> func_signature::type0_nr_or_str = {
  {0, {PT_GENERIC_TYPE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME}, {}},
  {0, {PT_GENERIC_TYPE_STRING}, {}},
  {0, {PT_GENERIC_TYPE_BIT}, {}},
  {0, {PT_TYPE_ENUMERATION}, {}},
  {0, {PT_TYPE_MAYBE}, {}},
  {0, {PT_TYPE_NULL}, {}},
  {0, {PT_TYPE_NA}, {}},
};

std::vector<func_signature> func_signature::type0_nr_or_str_discrete = {
  {0, {PT_GENERIC_TYPE_NUMBER   , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_DATETIME , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_STRING   , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_GENERIC_TYPE_BIT      , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_ENUMERATION      , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_MAYBE            , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
  {0, {PT_TYPE_NULL             , PT_GENERIC_TYPE_DISCRETE_NUMBER}, {}},
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
  {PT_TYPE_VARCHAR  , {PT_TYPE_NULL                                     }, {}},
};

std::vector<func_signature> func_signature::lead_lag = {//original code doesn't do anything!!!
  {0, {PT_GENERIC_TYPE_NUMBER   }, {}},
  {0, {PT_GENERIC_TYPE_STRING   }, {}},
  {0, {PT_GENERIC_TYPE_DATETIME }, {}},
  {0, {PT_GENERIC_TYPE_BIT      }, {}},
  {0, {PT_GENERIC_TYPE_SEQUENCE }, {}},
  {0, {PT_TYPE_NULL             }, {}},
};

std::vector<func_signature> func_signature::elt = {
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARCHAR     }},//get_current_result() expects args to be VCHAR, not just equivalent
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {PT_TYPE_VARNCHAR    }},//get_current_result() expects args to be VNCHAR, not just equivalent
  {PT_TYPE_NULL     , {PT_TYPE_NULL                   }, {PT_GENERIC_TYPE_ANY }},
  {PT_TYPE_NULL     , {PT_TYPE_NULL                   }, {                    }},
  {PT_TYPE_NULL     , {PT_GENERIC_TYPE_DISCRETE_NUMBER}, {                    }},//test with PT_GENERIC_TYPE_DISCRETE_NUMBER instead PT_TYPE_INTEGER
};

std::vector<func_signature> func_signature::insert = {
  {PT_TYPE_VARCHAR  , {PT_TYPE_NULL           , PT_GENERIC_TYPE_ANY , PT_GENERIC_TYPE_ANY , PT_GENERIC_TYPE_ANY}  , {}},
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_ANY    , PT_TYPE_NULL        , PT_GENERIC_TYPE_ANY , PT_GENERIC_TYPE_ANY}  , {}},
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_ANY    , PT_GENERIC_TYPE_ANY , PT_TYPE_NULL        , PT_GENERIC_TYPE_ANY}  , {}},
  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_ANY    , PT_GENERIC_TYPE_ANY , PT_GENERIC_TYPE_ANY , PT_TYPE_NULL       }  , {}},

  {PT_TYPE_VARCHAR  , {PT_GENERIC_TYPE_CHAR   , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_CHAR} , {}},
//{PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NCHAR  , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_NCHAR}, {}},
  {PT_TYPE_VARNCHAR , {PT_GENERIC_TYPE_NCHAR  , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , 0                    }, {}},

  {0                , {3                      , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_NCHAR}, {}},
  {0                , {3                      , PT_TYPE_INTEGER     , PT_TYPE_INTEGER     , PT_GENERIC_TYPE_STRING}, {}},
};

std::vector<func_signature> func_signature::json_key_val_r_key_val = {//(jsonKey, jsonVal[, jsonKey, jsonVal])
#if 0
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
#else
  {PT_TYPE_JSON, {}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_JSON_VAL}},
#endif
};

std::vector<func_signature> func_signature::json_val_r_val = {//(pt_is_json_value()[, pt_is_json_value()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_VAL}, {PT_GENERIC_TYPE_JSON_VAL}},
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

std::vector<func_signature> func_signature::json_doc_path_r_path = {//(pt_is_json_doc_type(), pt_is_json_path()[, pt_is_json_path()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_JSON_PATH}, {PT_GENERIC_TYPE_JSON_PATH}},
};

std::vector<func_signature> func_signature::json_doc_path_doc_r_path_doc = {//(pt_is_json_doc_type(), pt_is_json_path(), pt_is_json_doc() [, pt_is_json_path(), pt_is_json_doc()...])
  {PT_TYPE_JSON, {PT_GENERIC_TYPE_JSON_DOC, PT_GENERIC_TYPE_JSON_PATH, PT_GENERIC_TYPE_JSON_DOC}, {PT_GENERIC_TYPE_JSON_PATH, PT_GENERIC_TYPE_JSON_DOC}},
};

std::vector<func_signature> func_signature::set = {//???
  {PT_TYPE_SET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::multiset = {//???
  {PT_TYPE_MULTISET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::sequence = {//???
  {PT_TYPE_SEQUENCE, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::table_set = {//??? similar with set?
  {PT_TYPE_SET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::table_multiset = {//???
  {PT_TYPE_MULTISET, {}, {PT_GENERIC_TYPE_ANY}},
};

std::vector<func_signature> func_signature::table_sequence = {//???
  {PT_TYPE_SEQUENCE, {}, {PT_GENERIC_TYPE_ANY}},
};

