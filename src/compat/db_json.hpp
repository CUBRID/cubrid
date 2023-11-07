/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * db_json.hpp - functions related to json
 */

#ifndef _DB_JSON_HPP_
#define _DB_JSON_HPP_

#include "error_manager.h"
#include "memory_reference_store.hpp"
#include "db_function.hpp"
#include "storage_common.h"

#include <cstdint>
#include <string>

// forward definitions
struct or_buf;

#if defined (__cplusplus)
class JSON_DOC;
class JSON_PATH;
class JSON_VALIDATOR;
class JSON_ITERATOR;
#else
typedef void JSON_DOC;
typedef void JSON_PATH;
typedef void JSON_VALIDATOR;
typedef void JSON_ITERATOR;
#endif

#if defined (__cplusplus)
#include <vector>

/*
 * these also double as type precedence
 * INT and DOUBLE actually have the same precedence
 */
enum DB_JSON_TYPE
{
  DB_JSON_NULL = 0,
  DB_JSON_UNKNOWN,
  DB_JSON_INT,
  DB_JSON_BIGINT,
  DB_JSON_DOUBLE,
  DB_JSON_STRING,
  DB_JSON_OBJECT,
  DB_JSON_ARRAY,
  DB_JSON_BOOL,
};

using JSON_DOC_STORE = cubmem::reference_store<JSON_DOC>;

bool db_json_is_valid (const char *json_str);
const char *db_json_get_type_as_str (const JSON_DOC *document);
unsigned int db_json_get_length (const JSON_DOC *document);
unsigned int db_json_get_depth (const JSON_DOC *doc);
int db_json_extract_document_from_path (const JSON_DOC *document, const std::vector<std::string> &raw_path,
					JSON_DOC_STORE &result, bool allow_wildcards = true);
int db_json_extract_document_from_path (const JSON_DOC *document, const std::string &path,
					JSON_DOC_STORE &result, bool allow_wildcards = true);
int db_json_contains_path (const JSON_DOC *document, const std::vector<std::string> &paths, bool find_all,
			   bool &result);
char *db_json_get_raw_json_body_from_document (const JSON_DOC *doc);

char *db_json_get_json_body_from_document (const JSON_DOC &doc);

int db_json_add_member_to_object (JSON_DOC *doc, const char *name, const char *value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, int value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, std::int64_t value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, double value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, const JSON_DOC *value);

void db_json_add_element_to_array (JSON_DOC *doc, char *value);
void db_json_add_element_to_array (JSON_DOC *doc, int value);
void db_json_add_element_to_array (JSON_DOC *doc, std::int64_t value);
void db_json_add_element_to_array (JSON_DOC *doc, double value);
void db_json_add_element_to_array (JSON_DOC *doc, const JSON_DOC *value);

int db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc, size_t json_raw_length);
JSON_DOC *db_json_get_copy_of_doc (const JSON_DOC *doc);

int db_json_serialize (const JSON_DOC &doc, or_buf &buffer);
std::size_t db_json_serialize_length (const JSON_DOC &doc);
int db_json_deserialize (or_buf *buf, JSON_DOC *&doc);

int db_json_insert_func (const JSON_DOC *doc_to_be_inserted, JSON_DOC &doc_destination, const char *raw_path);
int db_json_replace_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_set_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_keys_func (const JSON_DOC &doc, JSON_DOC &result_json, const char *raw_path);
int db_json_array_append_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_array_insert_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_remove_func (JSON_DOC &doc, const char *raw_path);
int db_json_search_func (const JSON_DOC &doc, const DB_VALUE *pattern, const DB_VALUE *esc_char,
			 std::vector<JSON_PATH> &paths, const std::vector<std::string> &patterns, bool find_all);
int db_json_merge_patch_func (const JSON_DOC *source, JSON_DOC *&dest);
int db_json_merge_preserve_func (const JSON_DOC *source, JSON_DOC *&dest);
int db_json_get_all_paths_func (const JSON_DOC &doc, JSON_DOC *&result_json);
void db_json_pretty_func (const JSON_DOC &doc, char *&result_str);
std::string db_json_json_string_as_utf8 (std::string raw_json_string);
int db_json_path_unquote_object_keys_external (std::string &sql_path);
int db_json_unquote (const JSON_DOC &doc, char *&result_str);

int db_json_object_contains_key (JSON_DOC *obj, const char *key, int &result);
const char *db_json_get_schema_raw_from_validator (JSON_VALIDATOR *val);
int db_json_validate_json (const char *json_body);

int db_json_load_validator (const char *json_schema_raw, JSON_VALIDATOR *&validator);
JSON_VALIDATOR *db_json_copy_validator (JSON_VALIDATOR *validator);
JSON_DOC *db_json_allocate_doc ();
JSON_DOC *db_json_make_json_object ();
JSON_DOC *db_json_make_json_array ();
void db_json_delete_doc (JSON_DOC *&doc);
void db_json_delete_validator (JSON_VALIDATOR *&validator);
int db_json_validate_doc (JSON_VALIDATOR *validator, JSON_DOC *doc);
bool db_json_are_validators_equal (JSON_VALIDATOR *val1, JSON_VALIDATOR *val2);
bool db_json_path_contains_wildcard (const char *sql_path);

void db_json_iterator_next (JSON_ITERATOR &json_itr);
const JSON_DOC *db_json_iterator_get_document (JSON_ITERATOR &json_itr);
bool db_json_iterator_has_next (JSON_ITERATOR &json_itr);
void db_json_set_iterator (JSON_ITERATOR *&json_itr, const JSON_DOC &new_doc);
void db_json_reset_iterator (JSON_ITERATOR *&json_itr);
bool db_json_iterator_is_empty (const JSON_ITERATOR &json_itr);
JSON_ITERATOR *db_json_create_iterator (const DB_JSON_TYPE &type);
void db_json_delete_json_iterator (JSON_ITERATOR *&json_itr);
void db_json_clear_json_iterator (JSON_ITERATOR *&json_itr);

DB_JSON_TYPE db_json_get_type (const JSON_DOC *doc);
int db_json_get_int_from_document (const JSON_DOC *doc);
std::int64_t db_json_get_bigint_from_document (const JSON_DOC *doc);
double db_json_get_double_from_document (const JSON_DOC *doc);
const char *db_json_get_string_from_document (const JSON_DOC *doc);
char *db_json_get_bool_as_str_from_document (const JSON_DOC *doc);
bool db_json_get_bool_from_document (const JSON_DOC *doc);
char *db_json_copy_string_from_document (const JSON_DOC *doc);

void db_json_set_string_to_doc (JSON_DOC *doc, const char *str, unsigned len);
void db_json_set_double_to_doc (JSON_DOC *doc, double d);
void db_json_set_int_to_doc (JSON_DOC *doc, int i);
void db_json_set_bigint_to_doc (JSON_DOC *doc, std::int64_t i);

int db_json_value_is_contained_in_doc (const JSON_DOC *doc, const JSON_DOC *value, bool &result);
bool db_json_are_docs_equal (const JSON_DOC *doc1, const JSON_DOC *doc2);
void db_json_make_document_null (JSON_DOC *doc);
bool db_json_doc_has_numeric_type (const JSON_DOC *doc);
bool db_json_doc_is_uncomparable (const JSON_DOC *doc);

// DB_VALUE manipulation functions
int db_value_to_json_doc (const DB_VALUE &db_val, bool copy_json, JSON_DOC_STORE &json_doc);
int db_value_to_json_value (const DB_VALUE &db_val, JSON_DOC_STORE &json_doc);
void db_make_json_from_doc_store_and_release (DB_VALUE &value, JSON_DOC_STORE &doc_store);
int db_value_to_json_path (const DB_VALUE &path_value, FUNC_CODE fcode, std::string &path_str);
int db_value_to_json_key (const DB_VALUE &db_val, std::string &key_str);

int db_json_normalize_path_string (const char *pointer_path, std::string &normalized_path);
template <typename Fn, typename... Args>
inline int
db_json_convert_string_and_call (const char *json_raw, size_t json_raw_length, Fn &&func, Args &&... args)
{
  JSON_DOC *doc = NULL;
  int error_code;

  error_code = db_json_get_json_from_str (json_raw, doc, json_raw_length);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = func (doc, std::forward<Args> (args)...);
  db_json_delete_doc (doc);
  return error_code;
}

#endif /* defined (__cplusplus) */

#endif /* _DB_JSON_HPP_ */
