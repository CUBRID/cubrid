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
 * db_json.cpp - functions related to json
 */

#include "db_json.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "rapidjson/schema.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/allocators.h"
#include "rapidjson/writer.h"

#include <vector>
#include "memory_alloc.h"

#if defined GetObject
/* stupid windows and their definitions; GetObject is defined as GetObjectW or GetObjectA */
#undef GetObject
#endif /* defined GetObject */

class JSON_PRIVATE_ALLOCATOR
{
  public:
    static const bool kNeedFree;
    void *Malloc (size_t size);
    void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
    static void Free (void *ptr);

    JSON_PRIVATE_ALLOCATOR();
  private:
    THREAD_ENTRY *thread_p;
};

typedef rapidjson::UTF8 <> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR > JSON_PRIVATE_MEMPOOL;
typedef rapidjson::GenericValue <JSON_ENCODING, JSON_PRIVATE_MEMPOOL > JSON_VALUE;
typedef rapidjson::GenericPointer <JSON_VALUE > JSON_POINTER;

typedef rapidjson::GenericStringBuffer<JSON_ENCODING, JSON_PRIVATE_ALLOCATOR> JSON_STRING_BUFFER;

class JSON_DOC: public rapidjson::GenericDocument <JSON_ENCODING, JSON_PRIVATE_MEMPOOL >
{
    bool IsLeaf ();
};

class JSON_VALIDATOR
{
  public:
    JSON_VALIDATOR (const char *schema_raw);
    JSON_VALIDATOR (const JSON_VALIDATOR &copy);
    JSON_VALIDATOR &operator= (const JSON_VALIDATOR &copy);
    ~JSON_VALIDATOR ();

    int load ();
    int validate (const JSON_DOC *doc) const;
    const char *get_schema_raw () const;

  private:
    void generate_schema_validator (void);

    rapidjson::Document *document;
    rapidjson::SchemaDocument *schema;
    rapidjson::SchemaValidator *validator;
    char *schema_raw;
    bool is_loaded;
};

static void db_json_search_helper (const JSON_VALUE *whole_doc,
                                   const JSON_VALUE *doc,
                                   const char *current_path,
                                   const char *search_str,
                                   bool all,
                                   std::vector < std::string > &result);
static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);

JSON_VALIDATOR::JSON_VALIDATOR (const char *schema_raw) : is_loaded (false),
  document (NULL),
  schema (NULL),
  validator (NULL)
{
  this->schema_raw = new char [strlen (schema_raw) + 1];
  strcpy (this->schema_raw, schema_raw);

  /*
   * schema_raw_hash_code = std::hash<std::string>{}(std::string(schema_raw));
   * TODO is it worth the hash code?
   */
}

JSON_VALIDATOR::~JSON_VALIDATOR (void)
{
  if (validator != NULL)
    {
      delete validator;
      validator = NULL;
    }
  if (schema != NULL)
    {
      delete schema;
      schema = NULL;
    }
  if (document != NULL)
    {
      delete document;
      document = NULL;
    }

  if (schema_raw != NULL)
    {
      delete[] schema_raw;
      schema_raw = NULL;
    }
}

/*
 * create a validator object based on the schema raw member
 */

int
JSON_VALIDATOR::load ()
{
  if (this->schema_raw == NULL || is_loaded)
    {
      /* no schema */
      return NO_ERROR;
    }

  /* todo: do we have to allocate document? */
  document = new rapidjson::Document ();
  if (document == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (rapidjson::Document));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  document->Parse (this->schema_raw);
  if (document->HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (document->GetParseError ()), document->GetErrorOffset ());
      return ER_INVALID_JSON;
    }

  generate_schema_validator ();
  is_loaded = true;

  return NO_ERROR;
}

JSON_VALIDATOR::JSON_VALIDATOR (const JSON_VALIDATOR &copy)
{
  if (copy.document == NULL)
    {
      /* no schema actually */
      assert (copy.schema == NULL && copy.validator == NULL && copy.schema_raw == NULL);

      this->schema_raw = NULL;
    }
  else
    {
      this->schema_raw = strdup (copy.get_schema_raw ());

      document = new rapidjson::Document ();

      /* TODO: is this safe? */
      document->CopyFrom (*copy.document, document->GetAllocator ());
      generate_schema_validator ();
    }

  is_loaded = true;
}

JSON_VALIDATOR &JSON_VALIDATOR::operator= (const JSON_VALIDATOR &copy)
{
  if (this != &copy)
    {
      this->~JSON_VALIDATOR();
      new (this) JSON_VALIDATOR (copy);
    }

  return *this;
}

void
JSON_VALIDATOR::generate_schema_validator (void)
{
  schema = new rapidjson::SchemaDocument (*document);
  validator = new rapidjson::SchemaValidator (*schema);
}

/*
 * validate the doc argument with this validator
 */

int
JSON_VALIDATOR::validate (const JSON_DOC *doc) const
{
  int error_code = NO_ERROR;
  if (validator == NULL)
    {
      assert (schema_raw == NULL);
      return NO_ERROR;
    }

  if (!doc->Accept (*validator))
    {
      JSON_STRING_BUFFER sb1, sb2;

      validator->GetInvalidSchemaPointer ().StringifyUriFragment (sb1);
      validator->GetInvalidDocumentPointer ().StringifyUriFragment (sb2);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALIDATED_BY_SCHEMA, 3, sb1.GetString (),
              validator->GetInvalidSchemaKeyword (), sb2.GetString ());
      error_code = ER_JSON_INVALIDATED_BY_SCHEMA;
    }
  validator->Reset ();

  return error_code;
}

const char *
JSON_VALIDATOR::get_schema_raw () const
{
  return schema_raw;
}

void *
JSON_PRIVATE_ALLOCATOR::Malloc (size_t size)
{
  if (size)			//  behavior of malloc(0) is implementation defined.
    {
      return db_private_alloc (thread_p, size);
    }
  else
    {
      return NULL;		// standardize to returning NULL.
    }
}

void *
JSON_PRIVATE_ALLOCATOR::Realloc (void *originalPtr, size_t originalSize, size_t newSize)
{
  (void) originalSize;
  if (newSize == 0)
    {
      db_private_free (thread_p, originalPtr);
      return NULL;
    }
  return db_private_realloc (thread_p, originalPtr, newSize);
}

void
JSON_PRIVATE_ALLOCATOR::Free (void *ptr)
{
  db_private_free (NULL, ptr);
}

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;

/*C functions*/

bool
db_json_is_valid (const char *json_str)
{

  int error = db_json_validate_json (json_str);

  return error == NO_ERROR;
}

const char *
db_json_get_type_as_str (const JSON_DOC *document)
{
  assert (document != NULL);
  assert (!document->HasParseError ());

  if (document->IsArray ())
    {
      return "JSON_ARRAY";
    }
  else if (document->IsObject ())
    {
      return "JSON_OBJECT";
    }
  else if (document->IsInt ())
    {
      return "INTEGER";
    }
  else if (document->IsDouble ())
    {
      return "DOUBLE";
    }
  else if (document->IsString ())
    {
      return "STRING";
    }
  else
    {
      /* we shouldn't get here */
      assert (false);
      return "UNKNOWN";
    }
}

unsigned int
db_json_get_length (const JSON_DOC *document)
{
  if (!document->IsArray () && !document->IsObject ())
    {
      return 1;
    }
  if (document->IsArray ())
    {
      return document->Size ();
    }
  if (document->IsObject ())
    {
      int length = 0;
      for (JSON_VALUE::ConstMemberIterator itr = document->MemberBegin ();
           itr != document->MemberEnd (); ++itr)
        {
          length++;
        }

      return length;
    }

  return 0;
}

unsigned int
db_json_get_depth (const JSON_DOC *doc)
{
  return db_json_value_get_depth (doc);
}

static unsigned int
db_json_value_get_depth (const JSON_VALUE *doc)
{
  if (doc->IsArray ())
    {
      unsigned int max = 0;
      for (JSON_VALUE::ConstValueIterator itr = doc->Begin (); itr != doc->End (); ++itr)
        {
          unsigned int depth = db_json_value_get_depth (itr);
          if (depth > max)
            {
              max = depth;
            }
        }
      return max + 1;
    }
  else if (doc->IsObject ())
    {
      unsigned int max = 0;
      for (JSON_VALUE::ConstMemberIterator itr = doc->MemberBegin (); itr != doc->MemberEnd (); ++itr)
        {
          unsigned int depth = db_json_value_get_depth (&itr->value);
          if (depth > max)
            {
              max = depth;
            }
        }
      return max + 1;
    }
  else
    {
      /* no depth */
      return 0;
    }
}

int
db_json_extract_document_from_path (JSON_DOC *document, const char *raw_path, JSON_DOC *&result)
{
  JSON_POINTER p (raw_path);
  JSON_VALUE *resulting_json;

  if (!p.IsValid ())
    {
      result = NULL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (*document);

  if (resulting_json != NULL)
    {
      result = new JSON_DOC ();
      result->CopyFrom (*resulting_json, result->GetAllocator ());
    }
  else
    {
      result = NULL;
    }

  return NO_ERROR;
}

char *
db_json_get_raw_json_body_from_document (const JSON_DOC *doc)
{
  JSON_STRING_BUFFER buffer;
  rapidjson::Writer < JSON_STRING_BUFFER > writer (buffer);
  char *json_body;
  const char *buffer_str;

  buffer.Clear();

  doc->Accept (writer);
  json_body = db_private_strdup (NULL, buffer.GetString ());

  return json_body;
}

void
db_json_add_member_to_object (JSON_DOC *doc, const char *name, const char *value)
{
  if (!doc->IsObject())
    {
      doc->SetObject();
    }

  JSON_VALUE key, val;
  key.SetString (name, strlen (name), doc->GetAllocator ());
  val.SetString (value, strlen (value), doc->GetAllocator ());
  doc->AddMember (key, val, doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, int value)
{
  if (!doc->IsObject())
    {
      doc->SetObject();
    }

  JSON_VALUE key;
  key.SetString (name, strlen (name), doc->GetAllocator ());
  doc->AddMember (key, JSON_VALUE ().SetInt (value), doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, const JSON_DOC *value)
{
  if (!doc->IsObject())
    {
      doc->SetObject();
    }

  JSON_VALUE key, val;
  key.SetString (name, strlen (name), doc->GetAllocator ());
  val.CopyFrom (*value, doc->GetAllocator());
  doc->AddMember (key, val, doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, double value)
{
  if (!doc->IsObject())
    {
      doc->SetObject();
    }

  /*
  * JSON_VALUE uses a MemoryPoolAllocator which doesn't free memory,
  * so when key gets out of scope, the string wouldn't be freed
  * the memory will be freed only when doc is deleted
  */
  JSON_VALUE key;
  key.SetString (name, strlen (name), doc->GetAllocator ());
  doc->AddMember (key, JSON_VALUE ().SetDouble (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, char *value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  /*
   * JSON_VALUE uses a MemoryPoolAllocator which doesn't free memory,
   * so when v gets out of scope, the string wouldn't be freed
   * the memory will be freed only when doc is deleted
   */
  JSON_VALUE v;
  v.SetString (value, strlen (value), doc->GetAllocator ());
  doc->PushBack (v, doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, int value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  doc->PushBack (JSON_VALUE ().SetInt (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, double value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  doc->PushBack (JSON_VALUE ().SetDouble (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, const JSON_DOC *value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  JSON_VALUE new_doc;
  new_doc.CopyFrom (*value, doc->GetAllocator());
  doc->PushBack (new_doc, doc->GetAllocator ());
}

int
db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc)
{
  int error_code = NO_ERROR;
  doc = new JSON_DOC;

  if (doc->Parse (json_raw).HasParseError())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (doc->GetParseError ()), doc->GetErrorOffset ());
      delete doc;
      doc = NULL;
      error_code = ER_INVALID_JSON;
    }

  return error_code;
}

JSON_DOC *
db_json_get_copy_of_doc (const JSON_DOC *doc)
{
  JSON_DOC *new_doc = new JSON_DOC;
  new_doc->CopyFrom (*doc, new_doc->GetAllocator ());

  return new_doc;
}

void
db_json_copy_doc (JSON_DOC *dest, const JSON_DOC *src)
{
  dest->CopyFrom (*src, dest->GetAllocator());
}

int
db_json_insert_func (const JSON_DOC *value, JSON_DOC *doc, char *raw_path)
{
  JSON_POINTER p (raw_path);

  JSON_VALUE val;
  val.CopyFrom (*value, doc->GetAllocator());

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  p.Set (*doc, val, doc->GetAllocator ());
  return NO_ERROR;
}

int
db_json_remove_func (JSON_DOC *doc, char *raw_path)
{
  JSON_POINTER p (raw_path);

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (p.Get (*doc) == NULL)
    {
      return NO_ERROR;
    }

  p.Erase (*doc);
  return NO_ERROR;
}

DB_JSON_TYPE
db_json_get_type (const JSON_DOC *doc)
{
  if (doc->IsString())
    {
      return DB_JSON_STRING;
    }
  else if (doc->IsInt())
    {
      return DB_JSON_INT;
    }
  else if (doc->IsFloat() || doc->IsDouble())
    {
      return DB_JSON_DOUBLE;
    }
  else if (doc->IsObject())
    {
      return DB_JSON_OBJECT;
    }
  else if (doc->IsArray())
    {
      return DB_JSON_ARRAY;
    }
  else if (doc->IsNull())
    {
      return DB_JSON_NULL;
    }

  return DB_JSON_UNKNOWN;
}

void
db_json_merge_two_json_objects (JSON_DOC *obj1, const JSON_DOC *obj2)
{
  assert (db_json_get_type (obj1) == DB_JSON_OBJECT);
  assert (db_json_get_type (obj2) == DB_JSON_OBJECT);
  JSON_VALUE obj2_copy;

  obj2_copy.CopyFrom (*obj2, obj1->GetAllocator());

  for (JSON_VALUE::MemberIterator itr = obj2_copy.MemberBegin ();
       itr != obj2_copy.MemberEnd (); ++itr)
    {
      const char *name = itr->name.GetString ();
      if (obj1->HasMember (name))
        {
          if ((*obj1) [name].IsArray ())
            {
              (*obj1) [name].GetArray ().PushBack (itr->value, obj1->GetAllocator ());
            }
          else
            {
              JSON_VALUE value;
              value.SetArray();
              value.PushBack ((*obj1) [name], obj1->GetAllocator ());
              (*obj1) [name].Swap (value);
              (*obj1) [name].PushBack (itr->value, obj1->GetAllocator ());
            }
        }
      else
        {
          obj1->AddMember (itr->name, itr->value, obj1->GetAllocator ());
        }
    }
}

void
db_json_merge_two_json_arrays (JSON_DOC *array1, const JSON_DOC *array2)
{
  assert (db_json_get_type (array1) == DB_JSON_ARRAY);
  assert (db_json_get_type (array2) == DB_JSON_ARRAY);
  JSON_VALUE obj2_copy;

  obj2_copy.CopyFrom (*array2, array1->GetAllocator());

  for (JSON_VALUE::ValueIterator itr = obj2_copy.Begin ();
       itr != obj2_copy.End (); ++itr)
    {
      array1->PushBack (*itr, array1->GetAllocator ());
    }
}

void
db_json_merge_two_json_by_array_wrapping (JSON_DOC *j1, const JSON_DOC *j2)
{
  JSON_DOC *j2_copy = db_json_allocate_doc();

  j2_copy->CopyFrom (*j2, j2_copy->GetAllocator());

  if (db_json_get_type (j1) != DB_JSON_ARRAY)
    {
      JSON_VALUE value;
      value.SetArray();
      value.PushBack (*j1, j1->GetAllocator ());
      ((JSON_VALUE *)j1)->Swap (value);
    }

  if (db_json_get_type (j2) != DB_JSON_ARRAY)
    {
      JSON_VALUE value;
      value.SetArray();
      value.PushBack (*j2_copy, j1->GetAllocator ());
      ((JSON_VALUE *)j2_copy)->Swap (value);
    }

  db_json_merge_two_json_arrays (j1, j2_copy);
  db_json_delete_doc (j2_copy);
}

int
db_json_object_contains_key (JSON_DOC *obj, const char *key, int &result)
{

  if (!obj->IsObject ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NO_JSON_OBJECT_PROVIDED, 0);
      return ER_NO_JSON_OBJECT_PROVIDED;
    }

  result = (int) obj->HasMember (key);
  return NO_ERROR;
}

const char *
db_json_get_schema_raw_from_validator (JSON_VALIDATOR *val)
{
  return val->get_schema_raw();
}

int
db_json_validate_json (const char *json_body)
{
  rapidjson::Document document;

  document.Parse (json_body);
  if (document.HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (document.GetParseError ()), document.GetErrorOffset ());
      return ER_INVALID_JSON;
    }

  return NO_ERROR;
}

JSON_DOC *db_json_allocate_doc ()
{
  return new JSON_DOC;
}

void db_json_delete_doc (JSON_DOC *&doc)
{
  delete doc;
  doc = NULL;
}

int
db_json_load_validator (const char *json_schema_raw, JSON_VALIDATOR *&validator)
{
  assert (validator == NULL);

  validator = new JSON_VALIDATOR (json_schema_raw);
  int error_code = validator->load ();

  if (error_code != NO_ERROR)
    {
      delete validator;
      validator = NULL;
      return error_code;
    }

  return NO_ERROR;
}

JSON_VALIDATOR *
db_json_copy_validator (JSON_VALIDATOR *validator)
{
  return new JSON_VALIDATOR (*validator);
}

int
db_json_validate_doc (JSON_VALIDATOR *validator, JSON_DOC *doc)
{
  return validator->validate (doc);
}

void
db_json_delete_validator (JSON_VALIDATOR *&validator)
{
  delete validator;
  validator = NULL;
}

bool
db_json_are_validators_equal (JSON_VALIDATOR *val1, JSON_VALIDATOR *val2)
{
  if (val1 != NULL && val2 != NULL)
    {
      return (strcmp (val1->get_schema_raw(), val2->get_schema_raw()) == 0);
    }
  else if (val1 == NULL && val2 == NULL)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * db_json_merge_func ()
 * j1 (in)
 * j2 (in)
 * doc (out): the result
 * Json objects are merged like this:
 * {"a":"b", "x":"y"} M {"a":"c"} -> {"a":["b","c"], "x":"y"}
 * Json arrays as such:
 * ["a", "b"] M ["x", "y"] -> ["a", "b", "x", "y"]
 * Json scalars are transformed into arrays and merged normally
 */

int
db_json_merge_func (const JSON_DOC *source, JSON_DOC *dest)
{
  if (db_json_get_type (dest) == DB_JSON_NULL)
    {
      db_json_copy_doc (dest, source);
      return NO_ERROR;
    }

  if (db_json_get_type (dest) ==
      db_json_get_type (source))
    {
      if (db_json_get_type (dest) == DB_JSON_OBJECT)
        {
          db_json_merge_two_json_objects (dest, source);
        }
      else if (db_json_get_type (dest) == DB_JSON_ARRAY)
        {
          db_json_merge_two_json_arrays (dest, source);
        }
      else
        {
          db_json_merge_two_json_by_array_wrapping (dest, source);
        }
    }
  else
    {
      db_json_merge_two_json_by_array_wrapping (dest, source);
    }

  return NO_ERROR;
}

int
db_json_get_int_from_document (const JSON_DOC *doc)
{
  assert (db_json_get_type (doc) == DB_JSON_INT);

  return doc->GetInt();
}

double
db_json_get_double_from_document (const JSON_DOC *doc)
{
  assert (db_json_get_type (doc) == DB_JSON_DOUBLE);

  return doc->GetDouble();
}

const char *
db_json_get_string_from_document (const JSON_DOC *doc)
{
  assert (db_json_get_type (doc) == DB_JSON_STRING);

  return doc->GetString();
}

/*end of C functions*/

bool JSON_DOC::IsLeaf()
{
  return !IsArray() && !IsObject();
}

JSON_PRIVATE_ALLOCATOR::JSON_PRIVATE_ALLOCATOR()
{
  thread_p = thread_get_thread_entry_info ();
}
