/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * db_json.hpp - functions related to json
 */

#ifndef _DB_JSON_H_
#define _DB_JSON_H_

#include "error_manager.h"
#include "object_representation.h"

#if defined (__cplusplus)
class JSON_DOC;
class JSON_VALIDATOR;
class JSON_ITERATOR;
#else
typedef void JSON_DOC;
typedef void JSON_VALIDATOR;
typedef void JSON_ITERATOR;
#endif

#if defined (__cplusplus)

#include <functional>
#include "thread_compat.hpp"

/*
 * these also double as type precedence
 * INT and DOUBLE actually have the same precedence
*/
enum DB_JSON_TYPE
{
  DB_JSON_NULL = 0,
  DB_JSON_UNKNOWN,
  DB_JSON_INT,
  DB_JSON_DOUBLE,
  DB_JSON_STRING,
  DB_JSON_OBJECT,
  DB_JSON_ARRAY,
  DB_JSON_BOOL,
};

enum class JSON_PATH_TYPE
{
  JSON_PATH_SQL_JSON,
  JSON_PATH_POINTER,
  JSON_PATH_EMPTY
};

/* C functions */
bool db_json_is_valid (const char *json_str);
const char *db_json_get_type_as_str (const JSON_DOC *document);
unsigned int db_json_get_length (const JSON_DOC *document);
unsigned int db_json_get_depth (const JSON_DOC *doc);
int db_json_extract_document_from_path (const JSON_DOC *document, const char *raw_path,
					JSON_DOC *&result);
int db_json_contains_path (const JSON_DOC *document, const char *raw_path, bool &result);
char *db_json_get_raw_json_body_from_document (const JSON_DOC *doc);

char *db_json_get_json_body_from_document (const JSON_DOC &doc);

int db_json_add_member_to_object (JSON_DOC *doc, const char *name, const char *value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, int value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, double value);
int db_json_add_member_to_object (JSON_DOC *doc, const char *name, const JSON_DOC *value);

void db_json_add_element_to_array (JSON_DOC *doc, char *value);
void db_json_add_element_to_array (JSON_DOC *doc, int value);
void db_json_add_element_to_array (JSON_DOC *doc, double value);
void db_json_add_element_to_array (JSON_DOC *doc, const JSON_DOC *value);

int db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc, size_t json_raw_length);
JSON_DOC *db_json_get_copy_of_doc (const JSON_DOC *doc);

int db_json_serialize (const JSON_DOC &doc, OR_BUF &buffer);
std::size_t db_json_serialize_length (const JSON_DOC &doc);
int db_json_deserialize (OR_BUF *buf, JSON_DOC *&doc);

int db_json_insert_func (const JSON_DOC *doc_to_be_inserted, JSON_DOC &doc_destination, const char *raw_path);
int db_json_replace_func (const JSON_DOC *new_value, JSON_DOC &doc, const char *raw_path);
int db_json_set_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_keys_func (const JSON_DOC &doc, JSON_DOC *&result_json, const char *raw_path);
int db_json_keys_func (const char *json_raw, JSON_DOC *&result_json, const char *raw_path, size_t json_raw_length);
int db_json_array_append_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_array_insert_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path);
int db_json_remove_func (JSON_DOC &doc, const char *raw_path);
int db_json_merge_func (const JSON_DOC *source, JSON_DOC *&dest);
int db_json_get_all_paths_func (const JSON_DOC &doc, JSON_DOC *&result_json);
void db_json_pretty_func (const JSON_DOC &doc, char *&result_str);
int db_json_unquote (const JSON_DOC &doc, char *&result_str);
int db_json_arrayagg_func (const JSON_DOC *value, JSON_DOC &result_json);

int db_json_object_contains_key (JSON_DOC *obj, const char *key, int &result);
const char *db_json_get_schema_raw_from_validator (JSON_VALIDATOR *val);
int db_json_validate_json (const char *json_body);

int db_json_load_validator (const char *json_schema_raw, JSON_VALIDATOR *&validator);
JSON_VALIDATOR *db_json_copy_validator (JSON_VALIDATOR *validator);
JSON_DOC *db_json_allocate_doc ();
void db_json_delete_doc (JSON_DOC *&doc);
void db_json_delete_validator (JSON_VALIDATOR *&validator);
int db_json_validate_doc (JSON_VALIDATOR *validator, JSON_DOC *doc);
bool db_json_are_validators_equal (JSON_VALIDATOR *val1, JSON_VALIDATOR *val2);

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
double db_json_get_double_from_document (const JSON_DOC *doc);
const char *db_json_get_string_from_document (const JSON_DOC *doc);
char *db_json_get_bool_as_str_from_document (const JSON_DOC *doc);
char *db_json_copy_string_from_document (const JSON_DOC *doc);

void db_json_set_string_to_doc (JSON_DOC *doc, const char *str);
void db_json_set_double_to_doc (JSON_DOC *doc, double d);
void db_json_set_int_to_doc (JSON_DOC *doc, int i);

int db_json_value_is_contained_in_doc (const JSON_DOC *doc, const JSON_DOC *value, bool &result);
bool db_json_are_docs_equal (const JSON_DOC *doc1, const JSON_DOC *doc2);
void db_json_make_document_null (JSON_DOC *doc);
bool db_json_doc_has_numeric_type (const JSON_DOC *doc);
bool db_json_doc_is_uncomparable (const JSON_DOC *doc);
/* end of C functions */

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

#endif /* _DB_JSON_H_ */
