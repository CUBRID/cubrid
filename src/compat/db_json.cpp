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
 * The json feature is made as a black box to not make the whole project
 * depend on the rapidjson library. We might change this library in the future,
 * and this is easier when it's made this way.
 *
 * Rapidjson allocator usage:
 * We made the library use our own allocator, db_private_alloc. To achieve this,
 * we created new types, like JSON_DOC and JSON_VALUE. JSON_DOC uses inheritance
 * and not typedef as its creator in order for it to support forward declaration.
 * Also, we made JSON_PRIVATE_ALLOCATOR class which gets passed as template arguments to these
 * new types. This class implements malloc, realloc and free.
 *
 * JSON_VALUE does not use its own allocator, but rather it uses a JSON_DOC allocator.
 * With this we can control the JSON_VALUE's scope to match the scope of the said JSON_DOC.
 * For example, consider this piece of code:
 *
 * JSON_DOC *func(const char *str)
 * {
 *   JSON_VALUE value;
 *   JSON_DOC *doc;
 *   doc = db_json_allocate_doc ();
 *
 *   value.SetString (str, strlen (str), doc->GetAllocator ());
 *   doc.PushBack (value, doc->GetAllocator ());
 *
 *   return doc;
 * }
 *
 * Because PushBack doesn't perform a copy, one might think that this function
 * contains a major bug, because when value gets out of scope, the JSON_VALUE's
 * destructor gets called and the str's internal copy in value gets destroyed,
 * leading to memory corruptions.
 * This isn't the case, str's internal copy gets destroyed only when doc is deleted
 * because we used doc's allocator when calling SetString.
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
#include "system_parameter.h"

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
};

typedef rapidjson::UTF8 <> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;
typedef rapidjson::GenericValue <JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_VALUE;
typedef rapidjson::GenericPointer <JSON_VALUE> JSON_POINTER;
typedef rapidjson::GenericStringBuffer<JSON_ENCODING, JSON_PRIVATE_ALLOCATOR> JSON_STRING_BUFFER;

class JSON_DOC: public rapidjson::GenericDocument <JSON_ENCODING, JSON_PRIVATE_MEMPOOL>
{
  public:
    bool IsLeaf ();

  private:
    static const int MAX_CHUNK_SIZE;
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

    rapidjson::Document m_document;
    rapidjson::SchemaDocument *m_schema;
    rapidjson::SchemaValidator *m_validator;
    char *m_schema_raw;
    bool m_is_loaded;
};

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;
const int JSON_DOC::MAX_CHUNK_SIZE = 64 * 1024; /* TODO does 64K serve our needs? */

static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);
static int db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result);
static DB_JSON_TYPE db_json_get_type_of_value (const JSON_VALUE *val);
static bool db_json_value_has_numeric_type (const JSON_VALUE *doc);
static int db_json_get_int_from_value (const JSON_VALUE *val);
static double db_json_get_double_from_value (const JSON_VALUE *doc);
static char *db_json_get_string_from_value (const JSON_VALUE *doc);

JSON_VALIDATOR::JSON_VALIDATOR (const char *schema_raw) : m_schema (NULL),
  m_validator (NULL),
  m_is_loaded (false)
{
  m_schema_raw = strdup (schema_raw);
  /*
   * schema_raw_hash_code = std::hash<std::string>{}(std::string(schema_raw));
   * TODO is it worth the hash code?
   */
}

JSON_VALIDATOR::~JSON_VALIDATOR (void)
{
  if (m_schema != NULL)
    {
      delete m_schema;
    }

  if (m_validator != NULL)
    {
      delete m_validator;
    }

  if (m_schema_raw != NULL)
    {
      delete[] m_schema_raw;
      m_schema_raw = NULL;
    }
}

/*
 * create a validator object based on the schema raw member
 */

int
JSON_VALIDATOR::load ()
{
  if (m_schema_raw == NULL || m_is_loaded)
    {
      /* no schema */
      return NO_ERROR;
    }

  m_document.Parse (m_schema_raw);
  if (m_document.HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (m_document.GetParseError ()), m_document.GetErrorOffset ());
      return ER_INVALID_JSON;
    }

  generate_schema_validator ();
  m_is_loaded = true;

  return NO_ERROR;
}

JSON_VALIDATOR::JSON_VALIDATOR (const JSON_VALIDATOR &copy)
{
  if (copy.m_document == NULL)
    {
      /* no schema actually */
      assert (copy.m_schema == NULL && copy.m_validator == NULL && copy.m_schema_raw == NULL);

      m_schema_raw = NULL;
    }
  else
    {
      m_schema_raw = strdup (copy.m_schema_raw);

      /* TODO: is this safe? */
      m_document.CopyFrom (copy.m_document, m_document.GetAllocator ());
      generate_schema_validator ();
    }

  m_is_loaded = true;
}

JSON_VALIDATOR &JSON_VALIDATOR::operator= (const JSON_VALIDATOR &copy)
{
  if (this != &copy)
    {
      this->~JSON_VALIDATOR ();
      new (this) JSON_VALIDATOR (copy);
    }

  return *this;
}

void
JSON_VALIDATOR::generate_schema_validator (void)
{
  m_schema = new rapidjson::SchemaDocument (m_document);
  m_validator = new rapidjson::SchemaValidator (*m_schema);
}

/*
 * validate the doc argument with this validator
 */

int
JSON_VALIDATOR::validate (const JSON_DOC *doc) const
{
  int error_code = NO_ERROR;

  if (m_validator == NULL)
    {
      assert (m_schema_raw == NULL);
      return NO_ERROR;
    }

  if (!doc->Accept (*m_validator))
    {
      JSON_STRING_BUFFER sb1, sb2;

      m_validator->GetInvalidSchemaPointer ().StringifyUriFragment (sb1);
      m_validator->GetInvalidDocumentPointer ().StringifyUriFragment (sb2);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALIDATED_BY_SCHEMA, 3, sb1.GetString (),
	      m_validator->GetInvalidSchemaKeyword (), sb2.GetString ());
      error_code = ER_JSON_INVALIDATED_BY_SCHEMA;
    }

  m_validator->Reset ();

  return error_code;
}

const char *
JSON_VALIDATOR::get_schema_raw () const
{
  return m_schema_raw;
}

void *
JSON_PRIVATE_ALLOCATOR::Malloc (size_t size)
{
  if (size)			//  behavior of malloc(0) is implementation defined.
    {
      char *p = (char *) db_private_alloc (NULL, size);
      if (prm_get_bool_value (PRM_ID_JSON_LOG_ALLOCATIONS))
        {
          er_print_callstack (ARG_FILE_LINE, "JSON_ALLOC: Traced pointer=%p\n", p);
        }
      return p;
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
  char *p;
  if (newSize == 0)
    {
      db_private_free (NULL, originalPtr);
      return NULL;
    }
  p = (char *) db_private_realloc (NULL, originalPtr, newSize);
  if (prm_get_bool_value (PRM_ID_JSON_LOG_ALLOCATIONS))
    {
      er_print_callstack (ARG_FILE_LINE, "Traced pointer=%p\n", p);
    }
  return p;
}

void
JSON_PRIVATE_ALLOCATOR::Free (void *ptr)
{
  db_private_free (NULL, ptr);
}

/* C functions */

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

/*
 * db_json_get_length ()
 * document (in)
 * json_array length: number of elements
 * json_object length: number of key-value pairs
 * else: 1
 */

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

      for (JSON_VALUE::ConstMemberIterator itr = document->MemberBegin (); itr != document->MemberEnd (); ++itr)
	{
	  length++;
	}

      return length;
    }

  return 0;
}

/*
 * json_depth()
 * one array or one object increases the depth by 1
 */

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
      return 1;
    }
}

/*
 * json_extract
 * extracts from within the json a value based on the given path
 * ex:
 * json_extract('{"a":["b", 123]}', '/a/1') yields 123
 */

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
      result = db_json_allocate_doc ();
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
  rapidjson::Writer <JSON_STRING_BUFFER> writer (buffer);
  char *json_body;

  buffer.Clear ();

  doc->Accept (writer);
  json_body = db_private_strdup (NULL, buffer.GetString ());

  return json_body;
}

void
db_json_add_member_to_object (JSON_DOC *doc, const char *name, const char *value)
{
  JSON_VALUE key, val;

  if (!doc->IsObject ())
    {
      doc->SetObject ();
    }

  key.SetString (name, strlen (name), doc->GetAllocator ());
  val.SetString (value, strlen (value), doc->GetAllocator ());
  doc->AddMember (key, val, doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, int value)
{
  JSON_VALUE key;

  if (!doc->IsObject ())
    {
      doc->SetObject ();
    }

  key.SetString (name, strlen (name), doc->GetAllocator ());
  doc->AddMember (key, JSON_VALUE ().SetInt (value), doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, const JSON_DOC *value)
{
  JSON_VALUE key, val;

  if (!doc->IsObject ())
    {
      doc->SetObject ();
    }

  key.SetString (name, strlen (name), doc->GetAllocator ());
  if (value != NULL)
    {
      val.CopyFrom (*value, doc->GetAllocator ());
    }
  else
    {
      val.SetNull ();
    }
  doc->AddMember (key, val, doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, double value)
{
  JSON_VALUE key;

  if (!doc->IsObject ())
    {
      doc->SetObject ();
    }

  /*
   * JSON_VALUE uses a MemoryPoolAllocator which doesn't free memory,
   * so when key gets out of scope, the string wouldn't be freed
   * the memory will be freed only when doc is deleted
   */
  key.SetString (name, strlen (name), doc->GetAllocator ());
  doc->AddMember (key, JSON_VALUE ().SetDouble (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, char *value)
{
  JSON_VALUE v;

  if (!doc->IsArray ())
    {
      doc->SetArray ();
    }

  /*
   * JSON_VALUE uses a MemoryPoolAllocator which doesn't free memory,
   * so when v gets out of scope, the string wouldn't be freed
   * the memory will be freed only when doc is deleted
   */
  v.SetString (value, strlen (value), doc->GetAllocator ());
  doc->PushBack (v, doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, int value)
{
  if (!doc->IsArray ())
    {
      doc->SetArray ();
    }

  doc->PushBack (JSON_VALUE ().SetInt (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, double value)
{
  if (!doc->IsArray ())
    {
      doc->SetArray ();
    }

  doc->PushBack (JSON_VALUE ().SetDouble (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, const JSON_DOC *value)
{
  JSON_VALUE new_doc;

  if (!doc->IsArray ())
    {
      doc->SetArray ();
    }

  if (value == NULL)
    {
      new_doc.SetNull ();
    }
  else
    {
      new_doc.CopyFrom (*value, doc->GetAllocator ());
    }
  doc->PushBack (new_doc, doc->GetAllocator ());
}

int
db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc)
{
  int error_code = NO_ERROR;

  doc = db_json_allocate_doc ();

  if (doc->Parse (json_raw).HasParseError ())
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
  JSON_DOC *new_doc = db_json_allocate_doc ();

  new_doc->CopyFrom (*doc, new_doc->GetAllocator ());

  return new_doc;
}

void
db_json_copy_doc (JSON_DOC *dest, const JSON_DOC *src)
{
  dest->CopyFrom (*src, dest->GetAllocator ());
}

int
db_json_insert_func (const JSON_DOC *value, JSON_DOC *doc, char *raw_path)
{
  JSON_POINTER p (raw_path);
  JSON_VALUE val, *resulting_json, *resulting_json_parent;
  int i, raw_path_len;
  char *raw_path_parent;

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (*doc);

  if (resulting_json != NULL)
    {
      return NO_ERROR;
    }

  val.CopyFrom (*value, doc->GetAllocator ());

  raw_path_len = strlen (raw_path);
  for (i = raw_path_len-1; i >= 0; i--)
    {
      if (raw_path[i] == '/')
	{
	  break;
	}
    }

  raw_path_parent = (char *) db_private_alloc (NULL, raw_path_len);
  if (i > 0)
    {
      strncpy (raw_path_parent, raw_path, i);
      raw_path_parent[i] = '\0';
    }
  else
    {
      strcpy (raw_path_parent, "");
    }
  JSON_POINTER pointer_parent (raw_path_parent);
  if (!pointer_parent.IsValid ())
    {
      /* this shouldn't happen */
      assert (false);
    }
  resulting_json_parent = pointer_parent.Get (*doc);

  if (resulting_json_parent != NULL)
    {
      if (resulting_json_parent->IsObject ())
	{
	  p.Set (*doc, val, doc->GetAllocator ());
	}
      else if (resulting_json_parent->IsArray ())
	{
	  resulting_json_parent->PushBack (val, doc->GetAllocator ());
	}
      else
	{
	  JSON_VALUE value;

	  value.SetArray ();
	  value.PushBack (*resulting_json_parent, doc->GetAllocator ());
	  resulting_json_parent->Swap (value);

	  resulting_json_parent->PushBack (val, doc->GetAllocator ());
	}
    }

  db_private_free (NULL, raw_path_parent);
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
  return db_json_get_type_of_value (doc);
}

DB_JSON_TYPE
db_json_get_type_of_value (const JSON_VALUE *val)
{
  if (val == NULL)
    {
      return DB_JSON_NULL;
    }

  if (val->IsString ())
    {
      return DB_JSON_STRING;
    }
  else if (val->IsInt ())
    {
      return DB_JSON_INT;
    }
  else if (val->IsFloat () || val->IsDouble ())
    {
      return DB_JSON_DOUBLE;
    }
  else if (val->IsObject ())
    {
      return DB_JSON_OBJECT;
    }
  else if (val->IsArray ())
    {
      return DB_JSON_ARRAY;
    }
  else if (val->IsNull ())
    {
      return DB_JSON_NULL;
    }

  return DB_JSON_UNKNOWN;
}

void
db_json_merge_two_json_objects (JSON_DOC *obj1, const JSON_DOC *obj2)
{
  JSON_VALUE obj2_copy;

  assert (db_json_get_type (obj1) == DB_JSON_OBJECT);
  assert (db_json_get_type (obj2) == DB_JSON_OBJECT);

  obj2_copy.CopyFrom (*obj2, obj1->GetAllocator ());

  for (JSON_VALUE::MemberIterator itr = obj2_copy.MemberBegin (); itr != obj2_copy.MemberEnd (); ++itr)
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

	      value.SetArray ();
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
  JSON_VALUE obj2_copy;

  assert (db_json_get_type (array1) == DB_JSON_ARRAY);
  assert (db_json_get_type (array2) == DB_JSON_ARRAY);

  obj2_copy.CopyFrom (*array2, array1->GetAllocator ());

  for (JSON_VALUE::ValueIterator itr = obj2_copy.Begin (); itr != obj2_copy.End (); ++itr)
    {
      array1->PushBack (*itr, array1->GetAllocator ());
    }
}

void
db_json_merge_two_json_by_array_wrapping (JSON_DOC *j1, const JSON_DOC *j2)
{
  JSON_DOC *j2_copy = db_json_allocate_doc ();

  j2_copy->CopyFrom (*j2, j2_copy->GetAllocator ());

  if (db_json_get_type (j1) != DB_JSON_ARRAY)
    {
      JSON_VALUE value;

      value.SetArray ();
      value.PushBack (*j1, j1->GetAllocator ());
      ((JSON_VALUE *)j1)->Swap (value);
    }

  if (db_json_get_type (j2) != DB_JSON_ARRAY)
    {
      JSON_VALUE value;

      value.SetArray ();
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
  return val == NULL ? NULL : val->get_schema_raw ();
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
  return new JSON_DOC ();
}

void db_json_delete_doc (JSON_DOC *&doc)
{
  delete doc;
  doc = NULL;
}

int
db_json_load_validator (const char *json_schema_raw, JSON_VALIDATOR *&validator)
{
  int error_code;

  assert (validator == NULL);

  validator = new JSON_VALIDATOR (json_schema_raw);

  error_code = validator->load ();
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
      return (strcmp (val1->get_schema_raw (), val2->get_schema_raw ()) == 0);
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

  if (db_json_get_type (dest) == db_json_get_type (source))
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
  return db_json_get_int_from_value (doc);
}

double
db_json_get_double_from_document (const JSON_DOC *doc)
{
  return db_json_get_double_from_value (doc);
}

char *
db_json_get_string_from_document (const JSON_DOC *doc)
{
  return db_json_get_string_from_value (doc);
}

int
db_json_get_int_from_value (const JSON_VALUE *val)
{
  if (val == NULL)
    {
      assert (false);
      return 0;
    }

  assert (db_json_get_type_of_value (val) == DB_JSON_INT);

  return val->GetInt ();
}

double
db_json_get_double_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return 0;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_DOUBLE
	  || db_json_get_type_of_value (doc) == DB_JSON_INT);

  return db_json_get_type_of_value (doc) == DB_JSON_DOUBLE ? doc->GetDouble () : doc->GetInt ();
}

char *
db_json_get_string_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return NULL;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_STRING);

  return db_private_strdup (NULL, doc->GetString ());
}

bool
db_json_value_has_numeric_type (const JSON_VALUE *doc)
{
  return db_json_get_type_of_value (doc) == DB_JSON_INT || db_json_get_type_of_value (doc) == DB_JSON_DOUBLE;
}

/*
 *  The following rules define containment:
 *  A candidate scalar is contained in a target scalar if and only if they are comparable and are equal.
 *  Two scalar values are comparable if they have the same JSON_TYPE() types,
 *  with the exception that values of types INTEGER and DOUBLE are also comparable to each other.
 *
 *  A candidate array is contained in a target array if and only if
 *  every element in the candidate is contained in some element of the target.
 *
 *  A candidate nonarray is contained in a target array if and only if the candidate
 *  is contained in some element of the target.
 *
 *  A candidate object is contained in a target object if and only if for each key in the candidate
 *  there is a key with the same name in the target and the value associated with the candidate key
 *  is contained in the value associated with the target key.
 */
int
db_json_value_is_contained_in_doc (const JSON_DOC *doc, const JSON_DOC *value, bool &result)
{
  return db_json_value_is_contained_in_doc_helper (doc, value, result);
}

int
db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result)
{
  int error_code = NO_ERROR;
  DB_JSON_TYPE doc_type, val_type;

  doc_type = db_json_get_type_of_value (doc);
  val_type = db_json_get_type_of_value (value);

  if (doc_type == val_type)
    {
      if (doc_type == DB_JSON_STRING)
	{
	  result = (strcmp (doc->GetString (), value->GetString ()) == 0);
	}
      else if (doc_type == DB_JSON_INT)
	{
	  result = (db_json_get_int_from_value (doc) == db_json_get_int_from_value (value));
	}
      else if (doc_type == DB_JSON_DOUBLE)
	{
	  result = (db_json_get_double_from_value (doc) == db_json_get_double_from_value (value));
	}
      else if (doc_type == DB_JSON_ARRAY)
	{
	  for (JSON_VALUE::ConstValueIterator itr_val = value->Begin (); itr_val != value->End (); ++itr_val)
	    {
	      bool res;

	      result = false;
	      for (JSON_VALUE::ConstValueIterator itr_doc = doc->Begin (); itr_doc != doc->End (); ++itr_doc)
		{
		  error_code = db_json_value_is_contained_in_doc_helper (itr_doc, itr_val, res);
		  if (error_code != NO_ERROR)
		    {
		      result = false;
		      return error_code;
		    }
		  result |= res;
		}
	      if (!result)
		{
		  return NO_ERROR;
		}
	    }
	  result = true;
	}
      else if (doc_type == DB_JSON_OBJECT)
	{
	  JSON_VALUE::ConstMemberIterator itr_val;

	  for (itr_val = value->MemberBegin (); itr_val != value->MemberEnd (); ++itr_val)
	    {
	      if (doc->HasMember (itr_val->name))
		{
		  error_code = db_json_value_is_contained_in_doc_helper (& (*doc)[itr_val->name], &itr_val->value,
			       result);
		  if (error_code != NO_ERROR)
		    {
		      result = false;
		      return error_code;
		    }
		  if (!result)
		    {
		      return NO_ERROR;
		    }
		}
	    }
	}
      else if (doc_type == DB_JSON_NULL)
	{
	  result = false;
	  return NO_ERROR;
	}
    }
  else if (db_json_value_has_numeric_type (doc) && db_json_value_has_numeric_type (value))
    {
      double v1 = db_json_get_double_from_value (doc);
      double v2 = db_json_get_double_from_value (value);

      result = (v1 == v2);
    }
  else
    {
      if (doc_type == DB_JSON_ARRAY)
	{
	  for (JSON_VALUE::ConstValueIterator itr_doc = doc->Begin (); itr_doc != doc->End (); ++itr_doc)
	    {
	      error_code = db_json_value_is_contained_in_doc_helper (itr_doc, value, result);
	      if (error_code != NO_ERROR)
		{
		  result = false;
		  return error_code;
		}
	      if (result)
		{
		  return NO_ERROR;
		}
	    }
	  result = false;
	}
      else
	{
	  result = false;
	  return NO_ERROR;
	}
    }

  return error_code;
}

void db_json_set_string_to_doc (JSON_DOC *doc, const char *str)
{
  doc->SetString (str, doc->GetAllocator ());
}

void db_json_set_double_to_doc (JSON_DOC *doc, double d)
{
  doc->SetDouble (d);
}

void db_json_set_int_to_doc (JSON_DOC *doc, int i)
{
  doc->SetInt (i);
}

bool db_json_are_docs_equal (const JSON_DOC *doc1, const JSON_DOC *doc2)
{
  if (doc1 == NULL || doc2 == NULL)
    {
      return false;
    }
  return *doc1 == *doc2;
}

/* end of C functions */

bool JSON_DOC::IsLeaf ()
{
  return !IsArray () && !IsObject ();
}
