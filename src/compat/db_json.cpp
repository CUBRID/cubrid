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
#include "error_manager.h"
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
};

typedef rapidjson::UTF8 <> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR > JSON_PRIVATE_MEMPOOL;
typedef rapidjson::GenericValue <JSON_ENCODING, JSON_PRIVATE_MEMPOOL > JSON_VALUE;
typedef rapidjson::GenericPointer <JSON_VALUE > JSON_POINTER;

class JSON_DOC: public rapidjson::GenericDocument <JSON_ENCODING, JSON_PRIVATE_MEMPOOL >
{
};

#define DB_JSON_MAX_STRING_SIZE 32

class JSON_VALIDATOR
{
  public:
    JSON_VALIDATOR (char *schema_raw);
    JSON_VALIDATOR (const JSON_VALIDATOR &copy);
    JSON_VALIDATOR &operator= (const JSON_VALIDATOR &copy);
    ~JSON_VALIDATOR ();

    int load ();
    int validate (const JSON_DOC *doc) const;
    char *get_schema_raw () const;

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
                                   int one_or_all,
                                   std::vector < std::string > &result);
static unsigned int db_json_get_depth_helper (const JSON_VALUE *doc);

JSON_VALIDATOR::JSON_VALIDATOR (char *schema_raw) : is_loaded (false),
  document (NULL),
  schema (NULL),
  validator (NULL)
{
  this->schema_raw = (char *) malloc (strlen (schema_raw) + 1);
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
      free_and_init (schema_raw);
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
      rapidjson::StringBuffer sb1, sb2;

      validator->GetInvalidSchemaPointer ().StringifyUriFragment (sb1);
      validator->GetInvalidDocumentPointer ().StringifyUriFragment (sb2);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALIDATED_BY_SCHEMA, 3, sb1.GetString (),
              validator->GetInvalidSchemaKeyword (), sb2.GetString ());
      error_code = ER_JSON_INVALIDATED_BY_SCHEMA;
    }
  validator->Reset ();

  return error_code;
}

char *
JSON_VALIDATOR::get_schema_raw () const
{
  return schema_raw;
}

void *
JSON_PRIVATE_ALLOCATOR::Malloc (size_t size)
{
  if (size)			//  behavior of malloc(0) is implementation defined.
    {
      return db_private_alloc (NULL, size);
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
      db_private_free (NULL, originalPtr);
      return NULL;
    }
  return db_private_realloc (NULL, originalPtr, newSize);
}

void
JSON_PRIVATE_ALLOCATOR::Free (void *ptr)
{
  db_private_free (NULL, ptr);
}

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;

/*C functions*/

int
db_json_is_valid (const char *json_str)
{
  rapidjson::Document doc;
  return doc.Parse (json_str).HasParseError ()? 0 : 1;
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
  return db_json_get_depth_helper (doc);
}

static unsigned int
db_json_get_depth_helper (const JSON_VALUE *doc)
{
  if (doc->IsArray ())
    {
      unsigned int max = 0;
      for (JSON_VALUE::ConstValueIterator itr = doc->Begin (); itr != doc->End (); ++itr)
        {
          unsigned int depth = db_json_get_depth_helper ((JSON_VALUE *)itr);
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
          unsigned int depth = db_json_get_depth_helper ((JSON_VALUE *)&itr->value);
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

JSON_DOC *
db_json_extract_document_from_path (JSON_DOC *document, const char *raw_path)
{
  JSON_POINTER p (raw_path);
  JSON_VALUE *resulting_json;

  if (p.IsValid () && (resulting_json = p.Get (*document)) != NULL)
    {
      JSON_DOC *new_doc = new JSON_DOC ();
      new_doc->CopyFrom (*resulting_json, new_doc->GetAllocator ());
      return new_doc;
    }

  return NULL;
}

char *
db_json_get_raw_json_body_from_document (const JSON_DOC *doc)
{
  rapidjson::StringBuffer buffer;
  rapidjson::Writer < rapidjson::StringBuffer > writer (buffer);
  char *json_body;
  const char *buffer_str;

  buffer.Clear();

  doc->Accept (writer);
  buffer_str = buffer.GetString ();
  json_body = (char *) db_private_alloc (NULL, strlen (buffer_str) + 1);
  strcpy (json_body, buffer_str);

  return json_body;
}

JSON_DOC *
db_json_get_paths_for_search_func (const JSON_DOC *doc,
                                   const char *search_str,
                                   unsigned int one_or_all)
{
  std::vector<std::string> result;

  db_json_search_helper (doc, doc, "", search_str, one_or_all, result);
  JSON_DOC *new_doc = new JSON_DOC ();
  JSON_DOC aux;

  if (result.size () == 1)
    {
      aux.SetString (result[0].c_str (), aux.GetAllocator ());
    }
  else
    {
      aux.SetArray ();
      for (unsigned int i = 0; i < result.size (); i++)
        {
          aux.PushBack (rapidjson::StringRef (result[i].c_str ()), aux.GetAllocator ());
        }
    }

  new_doc->CopyFrom (aux, new_doc->GetAllocator());
  return new_doc;
}

/*
 * db_json_search_helper ()
 * this function generates all posible
 * paths based on the "whole_doc" argument and,
 * when it arrives at a scalar value,
 * checks whether it matches the search string
 *
 * return: void
 * whole_doc (in): the document where the search occurs
 * doc (in): every possible inner doc
 * current_path (in)
 * search_str (in)
 * one_or_all (in): 0 is one and 1 and all; one returns only the first occurence
 */

static void
db_json_search_helper (const JSON_VALUE *whole_doc,
                       const JSON_VALUE *doc,
                       const char *current_path,
                       const char *search_str,
                       int one_or_all,
                       std::vector < std::string > &result)
{
  if (one_or_all == 0 && result.size () == 1)
    {
      return;
    }

  if (!doc->IsArray () && !doc->IsObject ())
    {
      JSON_POINTER p (current_path);
      JSON_VALUE *resulting_json;

      if (p.IsValid () && (resulting_json = p.Get (*const_cast<JSON_VALUE *> (whole_doc))) != NULL)
        {
          char final_string[DB_JSON_MAX_STRING_SIZE];

          if (resulting_json->IsInt ())
            {
              snprintf (final_string, DB_JSON_MAX_STRING_SIZE, "%d", resulting_json->GetInt ());
            }
          else if (resulting_json->IsDouble ())
            {
              snprintf (final_string, DB_JSON_MAX_STRING_SIZE, "%lf", resulting_json->GetDouble ());
            }
          else if (resulting_json->IsString ())
            {
              strncpy (final_string, resulting_json->GetString (), DB_JSON_MAX_STRING_SIZE);
            }

          if (strstr (final_string, search_str) != NULL)
            {
              result.push_back (std::string (current_path));
            }
        }
      else
        {
          //everything should be valid
          assert (false);
        }
    }

  if (doc->IsArray ())
    {
      int index = 0;
      for (JSON_VALUE::ConstValueIterator itr = doc->Begin (); itr != doc->End (); ++itr)
        {
          char index_str[3];
          snprintf (index_str, 2, "%d", index);

          char *next_path = (char *) db_private_alloc (NULL, strlen (current_path) + strlen (index_str) + 2);
          strcpy (next_path, current_path);
          strcat (next_path, "/");
          strcat (next_path, index_str);

          db_json_search_helper (whole_doc, itr, next_path, search_str, one_or_all, result);

          index++;
          db_private_free (NULL, next_path);
        }
    }
  else if (doc->IsObject ())
    {
      for (JSON_VALUE::ConstMemberIterator itr = doc->MemberBegin (); itr != doc->MemberEnd (); ++itr)
        {
          char *next_path =
            (char *) db_private_alloc (NULL, strlen (current_path) + 1 + strlen (itr->name.GetString ()) + 1);
          strcpy (next_path, current_path);
          strcat (next_path, "/");
          strcat (next_path, itr->name.GetString ());

          db_json_search_helper (whole_doc, &itr->value, next_path, search_str, one_or_all, result);

          db_private_free (NULL, next_path);
        }
    }
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, char *value)
{
  assert (doc->IsObject());

  doc->AddMember (rapidjson::StringRef (name), rapidjson::StringRef (value), doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, int value)
{
  assert (doc->IsObject());

  doc->AddMember (rapidjson::StringRef (name), JSON_VALUE ().SetInt (value), doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, JSON_DOC *value)
{
  assert (doc->IsObject());

  if (value->IsArray())
    {
      doc->AddMember (rapidjson::StringRef (name), value->GetArray(), doc->GetAllocator ());
    }
  else if (value->IsObject())
    {
      doc->AddMember (rapidjson::StringRef (name), value->GetObject(), doc->GetAllocator ());
    }
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, float value)
{
  assert (doc->IsObject());

  doc->AddMember (rapidjson::StringRef (name), JSON_VALUE ().SetFloat (value), doc->GetAllocator ());
}

void
db_json_add_member_to_object (JSON_DOC *doc, char *name, double value)
{
  assert (doc->IsObject());

  doc->AddMember (rapidjson::StringRef (name), JSON_VALUE ().SetDouble (value), doc->GetAllocator ());
}

void
db_json_add_element_to_array (JSON_DOC *doc, char *value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  doc->PushBack (rapidjson::StringRef (value), doc->GetAllocator ());
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
db_json_add_element_to_array (JSON_DOC *doc, float value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  doc->PushBack (JSON_VALUE ().SetFloat (value), doc->GetAllocator ());
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
db_json_add_element_to_array (JSON_DOC *doc, JSON_DOC *value)
{
  if (!doc->IsArray())
    {
      doc->SetArray();
    }

  doc->PushBack (*value, doc->GetAllocator ());
}

JSON_DOC *
db_json_get_json_from_str (const char *json_raw, int &error_code)
{
  JSON_DOC *new_doc = new JSON_DOC;

  if (new_doc->Parse (json_raw).HasParseError())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (new_doc->GetParseError ()), new_doc->GetErrorOffset ());
      delete new_doc;
      error_code = ER_INVALID_JSON;
      return NULL;
    }

  error_code = NO_ERROR;
  return new_doc;
}

JSON_DOC *
db_json_get_copy_of_doc (const JSON_DOC *doc)
{
  JSON_DOC *new_doc = new JSON_DOC;
  new_doc->CopyFrom (*doc, new_doc->GetAllocator ());

  return new_doc;
}

void
db_json_copy_doc (JSON_DOC *dest, JSON_DOC *src)
{
  dest->CopyFrom (*src, dest->GetAllocator());
}

void
db_json_insert_func (JSON_DOC *doc, char *raw_path, char *str_value, int &error_code)
{
  JSON_POINTER p (raw_path);
  JSON_DOC inserting_doc;
  JSON_DOC aux;

  aux.CopyFrom (*doc, aux.GetAllocator ());

  if (!p.IsValid ())
    {
      error_code = ER_JSON_INVALID_PATH;
      return;
    }

  if (inserting_doc.Parse (str_value).HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (inserting_doc.GetParseError ()), inserting_doc.GetErrorOffset ());
      error_code = ER_INVALID_JSON;
      return;
    }

  p.Set (aux, inserting_doc, aux.GetAllocator ());
  doc->CopyFrom (aux, doc->GetAllocator ());
  error_code = NO_ERROR;
}

void
db_json_insert_func (JSON_DOC *doc, char *raw_path, JSON_DOC *value, int &error_code)
{
  JSON_POINTER p (raw_path);

  if (!p.IsValid ())
    {
      error_code = ER_JSON_INVALID_PATH;
      return;
    }

  p.Set (*doc, *value);
  error_code = NO_ERROR;
}

void
db_json_remove_func (JSON_DOC *doc, char *raw_path, int &error_code)
{
  JSON_POINTER p (raw_path);

  if (!p.IsValid ())
    {
      error_code = ER_JSON_INVALID_PATH;
      return;
    }

  p.Erase (*doc);
  error_code = NO_ERROR;
}

DB_JSON_TYPE
db_json_get_type (JSON_DOC *doc)
{
  if (doc->IsString())
    {
      return DB_JSON_STRING;
    }
  else if (doc->IsInt() || doc->IsDouble())
    {
      return DB_JSON_NUMBER;
    }
  else if (doc->IsObject())
    {
      return DB_JSON_OBJECT;
    }
  else if (doc->IsArray())
    {
      return DB_JSON_ARRAY;
    }

  return DB_JSON_UNKNOWN;
}

void
db_json_merge_two_json_objects (JSON_DOC *obj1, JSON_DOC *obj2)
{
  assert (db_json_get_type (obj1) == DB_JSON_OBJECT);
  assert (db_json_get_type (obj2) == DB_JSON_OBJECT);

  for (JSON_VALUE::MemberIterator itr = obj2->MemberBegin ();
       itr != obj2->MemberEnd (); ++itr)
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
              JSON_VALUE value ((*obj1) [name], obj1->GetAllocator ());
              (*obj1) [name].SetArray ();
              (*obj1) [name].PushBack (value, obj1->GetAllocator ());
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
db_json_merge_two_json_arrays (JSON_DOC *array1, JSON_DOC *array2)
{
  assert (db_json_get_type (array1) == DB_JSON_ARRAY);
  assert (db_json_get_type (array2) == DB_JSON_ARRAY);

  for (JSON_VALUE::ValueIterator itr = array2->Begin ();
       itr != array2->End (); ++itr)
    {
      array1->PushBack (*itr, array1->GetAllocator ());
    }
}

void
db_json_merge_two_json_by_array_wrapping (JSON_DOC *j1, JSON_DOC *j2)
{
  JSON_DOC *j1_aux = NULL, *j2_aux = NULL, doc;

  if (db_json_get_type (j1) != DB_JSON_ARRAY)
    {
      j1_aux = new JSON_DOC;
      db_json_add_element_to_array (j1_aux, j1);
    }
  if (db_json_get_type (j2) != DB_JSON_ARRAY)
    {
      j2_aux = new JSON_DOC;
      db_json_add_element_to_array (j2_aux, j2);
    }

  db_json_copy_doc (&doc, j1_aux == NULL ? j1 : j1_aux);
  db_json_merge_two_json_arrays (&doc, j2_aux == NULL ? j2 : j2_aux);

  db_json_copy_doc (j1, &doc);
  if (j1_aux != NULL)
    {
      delete j1_aux;
    }
  if (j2_aux != NULL)
    {
      delete j2_aux;
    }
}

int
db_json_object_contains_key (JSON_DOC *obj, const char *key, int &error_code)
{
  error_code = NO_ERROR;

  if (!obj->IsObject ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NO_JSON_OBJECT_PROVIDED, 0);
      error_code = ER_NO_JSON_OBJECT_PROVIDED;
      return 0;
    }

  return (int) obj->HasMember (key);
}

char *
db_json_get_schema_raw_from_validator (JSON_VALIDATOR *val)
{
  return val->get_schema_raw();
}

int
db_json_validate_json (char *json_body)
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

JSON_DOC *db_json_allocate_doc_and_set_type (DB_JSON_TYPE desired_type)
{
  JSON_DOC *doc = new JSON_DOC;

  switch (desired_type)
    {
    case DB_JSON_ARRAY:
      doc->SetArray ();
      break;
    case DB_JSON_OBJECT:
      doc->SetObject ();
      break;
    default:
      break;
    }

  return doc;
}

JSON_DOC *db_json_allocate_doc ()
{
  return new JSON_DOC;
}

void db_json_delete_doc (JSON_DOC *doc)
{
  delete doc;
}

JSON_VALIDATOR *
db_json_load_validator (char *json_schema_raw, int &error_code)
{
  JSON_VALIDATOR *validator = new JSON_VALIDATOR (json_schema_raw);
  error_code = validator->load ();

  if (error_code != NO_ERROR)
    {
      return NULL;
    }

  return validator;
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
db_json_delete_validator (JSON_VALIDATOR *validator)
{
  delete validator;
}

/*end of C functions*/
