#include "func_type.hpp"
#include "parse_tree.h"
#include "parser.h"

#define NUMERIC PT_TYPE_DOUBLE, PT_TYPE_INTEGER, PT_TYPE_BIGINT, PT_TYPE_SMALLINT, PT_TYPE_FLOAT, PT_TYPE_MONETARY, PT_TYPE_LOGICAL, PT_TYPE_NUMERIC //PT_IS_NUMERIC_TYPE()
#define STRING  PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR, PT_TYPE_BIT, PT_TYPE_VARBIT //PT_IS_STRING_TYPE()
#define DATE    PT_TYPE_DATE, PT_TYPE_TIME, PT_TYPE_TIMESTAMP, PT_TYPE_DATETIME, PT_TYPE_TIMETZ, PT_TYPE_TIMELTZ, PT_TYPE_DATETIMETZ, PT_TYPE_DATETIMELTZ, PT_TYPE_TIMESTAMPTZ, PT_TYPE_TIMESTAMPLTZ //PT_IS_DATE_TIME_TYPE()

func_type sig::integer = {
  PT_TYPE_INTEGER, {}, {},
};

func_type sig::bigint = {
  PT_TYPE_BIGINT, {}, {},
};

func_type sig::discrete_cast_to_bigint = {
  PT_TYPE_BIGINT,
  {{PT_TYPE_BIGINT, PT_TYPE_INTEGER, PT_TYPE_SMALLINT, PT_TYPE_MAYBE, PT_TYPE_NULL, PT_TYPE_NA}},
  {},
};

func_type sig::numeric_cast_to_double = {
  PT_TYPE_DOUBLE, {{NUMERIC}}, {},
};

func_type sig::count_star = {
  PT_TYPE_INTEGER, {}, {},
};

func_type sig::count = {
  PT_TYPE_INTEGER, {{PT_TYPE_INTEGER, NUMERIC, STRING, DATE}}, {},
};

func_type sig::sum = {
  PT_TYPE_DOUBLE, {{NUMERIC, PT_TYPE_MAYBE, PT_TYPE_NULL}}, {},
};

func_type sig::cume_dist = {//original code doesn't check arguments!!!
  PT_TYPE_DOUBLE,
  {},
  {{PT_TYPE_INTEGER, PT_TYPE_BIGINT, PT_TYPE_SMALLINT}, {PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR}},
};

func_type sig::ntile = {//why original code cast args to double???
  PT_TYPE_INTEGER,
  {{PT_TYPE_INTEGER, PT_TYPE_BIGINT, PT_TYPE_SMALLINT, PT_TYPE_MAYBE, PT_TYPE_NULL}},
  {},
};

func_type sig::double_01 = {
  PT_TYPE_DOUBLE, {{PT_TYPE_DOUBLE}}, {},
};

func_type sig::median = {
  0,  {{NUMERIC, DATE}}, {},
};

func_type sig::type0_nr_or_str = {
  0,
  {{NUMERIC, DATE, STRING, PT_TYPE_ENUMERATION, PT_TYPE_MAYBE, PT_TYPE_NULL, PT_TYPE_NA}},
  {},
};

func_type sig::group_concat = {
  PT_TYPE_VARCHAR,
  {{STRING, NUMERIC, DATE, PT_TYPE_ENUMERATION, PT_TYPE_MAYBE, PT_TYPE_NULL, PT_TYPE_NA}, {STRING, PT_TYPE_NONE}},
  {},
};

func_type sig::lead_lag = {//original code doesn't do anything!!!
  0,
  {{NUMERIC, STRING}},
  {},
};

func_type sig::elt = {
  PT_TYPE_VARCHAR,
  {{PT_TYPE_BIGINT, PT_TYPE_SMALLINT, PT_TYPE_INTEGER}, {PT_TYPE_VARCHAR}},
  {{PT_TYPE_VARCHAR}},
};

func_type sig::insert = {
  PT_TYPE_VARCHAR,
  {{PT_TYPE_VARCHAR}, {PT_TYPE_INTEGER}, {PT_TYPE_INTEGER}, {STRING, NUMERIC, DATE, PT_TYPE_MAYBE, PT_TYPE_NULL, PT_TYPE_NA}},
  {},
};


#define JSON_KEY  {PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR}
#define JSON_VAL  {PT_TYPE_VARCHAR, PT_TYPE_VARCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR, PT_TYPE_NUMERIC, PT_TYPE_INTEGER, PT_TYPE_LOGICAL, PT_TYPE_DOUBLE, PT_TYPE_NUMERIC, PT_TYPE_JSON, PT_TYPE_MAYBE, PT_TYPE_NULL}
#define JSON_DOC  {PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR, PT_TYPE_JSON, PT_TYPE_MAYBE, PT_TYPE_NULL}
#define JSON_PATH {PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR, PT_TYPE_BIT, PT_TYPE_VARBIT, PT_TYPE_MAYBE, PT_TYPE_NULL}

func_type sig::json_key_val_r_key_val = {//(jsonKey, jsonVal[, jsonKey, jsonVal])
  PT_TYPE_JSON,
#if 1
  {JSON_KEY, JSON_VAL},
  {JSON_KEY, JSON_VAL},
#else
  {{PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_NUMBER, PT_TYPE_MAYBE, PT_TYPE_NULL}},
  {{PT_GENERIC_TYPE_STRING}, {PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_NUMBER, PT_TYPE_MAYBE, PT_TYPE_NULL}},
#endif
};

func_type sig::json_val_r_val = {//(pt_is_json_value()[, pt_is_json_value()...])
  PT_TYPE_JSON,
  {JSON_VAL},
  {JSON_VAL},
};

func_type sig::json_doc = {//(pt_is_json_doc_type())
  PT_TYPE_JSON,
  {JSON_DOC},
  {},
};

func_type sig::json_doc_r_doc = {//(pt_is_json_doc_type()[, pt_is_json_doc()...])
  PT_TYPE_JSON,
  {JSON_DOC},
  {JSON_DOC},
};

func_type sig::json_doc_path = {//(pt_is_json_doc_type(), pt_is_json_path())
  PT_TYPE_JSON,
  {JSON_DOC, JSON_PATH},
  {},
};

func_type sig::json_doc_path_r_path = {//(pt_is_json_doc_type(), pt_is_json_path()[, pt_is_json_path()...])
  PT_TYPE_JSON,
  {JSON_DOC, JSON_PATH},
  {JSON_PATH},
};

func_type sig::json_doc_path_doc_r_path_doc = {//(pt_is_json_doc_type(), pt_is_json_path(), pt_is_json_doc() [, pt_is_json_path(), pt_is_json_doc()...])
  PT_TYPE_JSON,
  {JSON_DOC, JSON_PATH, JSON_DOC},
  {JSON_PATH, JSON_DOC},
};
