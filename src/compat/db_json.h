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
 * db_json.h - functions related to json
 */

#ifndef _DB_JSON_H
#define _DB_JSON_H

#if defined (__cplusplus)
class JSON_DOC;
class JSON_VALIDATOR;
#else
typedef void JSON_DOC;
typedef void JSON_VALIDATOR;
#endif

/* *INDENT-OFF* */
#if defined (__cplusplus)

enum DB_JSON_TYPE {
  DB_JSON_STRING = 1,
  DB_JSON_NUMBER,
  DB_JSON_OBJECT,
  DB_JSON_ARRAY,
  DB_JSON_UNKNOWN,
};

/* C functions */
int db_json_is_valid (const char *json_str);
const char *db_json_get_type_as_str (const JSON_DOC *document);
unsigned int db_json_get_length (const JSON_DOC *document);
unsigned int db_json_get_depth (const JSON_DOC *doc);
JSON_DOC *db_json_extract_document_from_path (JSON_DOC *document, const char *raw_path);
char *db_json_get_raw_json_body_from_document (const JSON_DOC *doc);
JSON_DOC *db_json_get_paths_for_search_func (const JSON_DOC *doc, const char *search_str, unsigned int one_or_all);

void db_json_add_member_to_object (JSON_DOC *doc, char *name, char *value);
void db_json_add_member_to_object (JSON_DOC *doc, char *name, int value);
void db_json_add_member_to_object (JSON_DOC *doc, char *name, float value);
void db_json_add_member_to_object (JSON_DOC *doc, char *name, double value);
void db_json_add_member_to_object (JSON_DOC *doc, char *name, JSON_DOC *value);

void db_json_add_element_to_array (JSON_DOC *doc, char *value);
void db_json_add_element_to_array (JSON_DOC *doc, int value);
void db_json_add_element_to_array (JSON_DOC *doc, float value);
void db_json_add_element_to_array (JSON_DOC *doc, double value);
void db_json_add_element_to_array (JSON_DOC *doc, JSON_DOC *value);

JSON_DOC *db_json_get_json_from_str (const char *json_raw, int &error_code);
JSON_DOC *db_json_get_copy_of_doc (const JSON_DOC *doc);
void db_json_copy_doc (JSON_DOC *dest, JSON_DOC *src);

void db_json_insert_func (JSON_DOC *doc, char *raw_path, char *str_value, int &error_code);
void db_json_insert_func (JSON_DOC *doc, char *raw_path, JSON_DOC *value, int &error_code);

void db_json_remove_func (JSON_DOC *doc, char *raw_path, int &error_code);

void db_json_merge_two_json_objects (JSON_DOC *obj1, JSON_DOC *obj2);
void db_json_merge_two_json_arrays (JSON_DOC *array1, JSON_DOC *array2);
void db_json_merge_two_json_by_array_wrapping (JSON_DOC *j1, JSON_DOC *j2);

int db_json_object_contains_key (JSON_DOC *obj, const char *key, int &error_code);
char *db_json_get_schema_raw_from_validator (JSON_VALIDATOR *val);
int db_json_validate_json (char *json_body);

JSON_DOC *db_json_allocate_doc_and_set_type (DB_JSON_TYPE desired_type);
JSON_VALIDATOR *db_json_load_validator (char *json_schema_raw, int &error_code);
JSON_VALIDATOR *db_json_copy_validator (JSON_VALIDATOR *validator);
JSON_DOC *db_json_allocate_doc ();
void db_json_delete_doc (JSON_DOC *doc);
void db_json_delete_validator (JSON_VALIDATOR *validator);
int db_json_validate_doc (JSON_VALIDATOR *validator, JSON_DOC *doc);

DB_JSON_TYPE db_json_get_type (JSON_DOC *doc);
/* end of C functions */

#endif /* defined (__cplusplus) */

/* *INDENT-ON* */
#endif /* db_json.h */
