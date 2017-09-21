/************************************************************************/
/* TODO: license comment                                                */
/************************************************************************/

#ifndef _DB_JSON_H
#define _DB_JSON_H

/* *INDENT-OFF* */
#if defined (__cplusplus)

#include "rapidjson/schema.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/allocators.h"
#include "rapidjson/writer.h"
#include <vector>
#include <string>

#define DB_JSON_MAX_STRING_SIZE 32

enum DB_JSON_TYPE {
  DB_JSON_STRING = 1,
  DB_JSON_NUMBER,
  DB_JSON_OBJECT,
  DB_JSON_ARRAY,
  DB_JSON_UNKNOWN
};

class JSON_PRIVATE_ALLOCATOR;

typedef rapidjson::UTF8<> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator<JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;

typedef rapidjson::GenericDocument<JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_DOC;
typedef rapidjson::GenericValue<JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_VALUE;
typedef rapidjson::GenericPointer<JSON_VALUE> JSON_POINTER;

/* C functions */
int db_json_is_valid (const char *json_str);
const char *db_json_get_type_as_str (const JSON_DOC &document);
unsigned int db_json_get_length (const JSON_VALUE &document);
unsigned int db_json_get_depth (const JSON_VALUE &doc);
JSON_DOC *db_json_extract_document_from_path (JSON_DOC &document, const char *raw_path);
char *db_json_get_raw_json_body_from_document (const JSON_DOC &doc);
JSON_DOC *db_json_get_paths_for_search_func (const JSON_DOC &doc, const char *search_str, unsigned int one_or_all);

void db_json_add_member_to_object (JSON_DOC &doc, char *name, char *value);
void db_json_add_member_to_object (JSON_DOC &doc, char *name, int value);
void db_json_add_member_to_object (JSON_DOC &doc, char *name, float value);
void db_json_add_member_to_object (JSON_DOC &doc, char *name, double value);
void db_json_add_member_to_object (JSON_DOC &doc, char *name, JSON_DOC &value);

void db_json_add_element_to_array (JSON_DOC &doc, char *value);
void db_json_add_element_to_array (JSON_DOC &doc, int value);
void db_json_add_element_to_array (JSON_DOC &doc, float value);
void db_json_add_element_to_array (JSON_DOC &doc, double value);
void db_json_add_element_to_array (JSON_DOC &doc, const JSON_VALUE &value);

JSON_DOC *db_json_get_json_from_str (const char *json_raw, int &error_code);
JSON_DOC *db_json_get_copy_of_doc (const JSON_DOC &doc);
void db_json_copy_doc (JSON_DOC &dest, JSON_VALUE &src);

void db_json_insert_func (JSON_DOC &doc, char *raw_path, char *str_value, int &error_code);
void db_json_insert_func (JSON_DOC &doc, char *raw_path, JSON_VALUE &value, int &error_code);

void db_json_remove_func (JSON_DOC &doc, char *raw_path, int &error_code);

void db_json_merge_two_json_objects (JSON_DOC &obj1, JSON_DOC &obj2);
void db_json_merge_two_json_arrays (JSON_DOC &array1, JSON_DOC &array2);

DB_JSON_TYPE db_json_get_type (JSON_DOC &doc);
/* end of C functions */

class JSON_PRIVATE_ALLOCATOR
{
public:
  static const bool kNeedFree;
  void *Malloc (size_t size);
  void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
  static void Free (void *ptr);
};

class JSON_VALIDATOR
{
public:
  JSON_VALIDATOR (char *schema_raw);
  JSON_VALIDATOR (const JSON_VALIDATOR &copy);
  JSON_VALIDATOR &operator= (const JSON_VALIDATOR &copy);
  ~JSON_VALIDATOR ();

  int load ();
  int validate (const JSON_DOC &doc) const;
  char *get_schema_raw () const;

  static int validate_json (const char *json_raw_body);

private:
  void generate_schema_validator (void);

  rapidjson::Document *document;
  rapidjson::SchemaDocument *schema;
  rapidjson::SchemaValidator *validator;
  char *schema_raw;
  bool is_loaded;
};

#else /* !defined (__cplusplus) */

typedef void JSON_DOC;
typedef void JSON_VALUE;
typedef void JSON_POINTER;
typedef void JSON_VALIDATOR;

#endif /* defined (__cplusplus) */

/* *INDENT-ON* */
#endif /* db_json.h */
