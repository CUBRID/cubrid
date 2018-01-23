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

#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <locale>
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

static std::vector<std::pair<std::string, std::string> > uri_fragment_conversions =
{
  std::make_pair ("~", "~0"),
  std::make_pair ("/", "~1"),
  std::make_pair (" ", "%20")
};

static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);
static int db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result);
static DB_JSON_TYPE db_json_get_type_of_value (const JSON_VALUE *val);
static bool db_json_value_has_numeric_type (const JSON_VALUE *doc);
static int db_json_get_int_from_value (const JSON_VALUE *val);
static double db_json_get_double_from_value (const JSON_VALUE *doc);
static const char *db_json_get_string_from_value (const JSON_VALUE *doc);
static char *db_json_copy_string_from_value (const JSON_VALUE *doc);
static char *db_json_get_bool_as_str_from_value (const JSON_VALUE *doc);
STATIC_INLINE char *db_json_bool_to_string (bool b);
static void db_json_merge_two_json_objects (JSON_DOC &obj1, const JSON_DOC *obj2);
static void db_json_merge_two_json_arrays (JSON_DOC &array1, const JSON_DOC *array2);
static void db_json_merge_two_json_by_array_wrapping (JSON_DOC &j1, const JSON_DOC *j2);
static void db_json_copy_doc (JSON_DOC &dest, const JSON_DOC *src);

static void db_json_get_paths_helper (const JSON_VALUE &obj, const std::string &sql_path,
				      std::vector<std::string> &paths);
static void db_json_normalize_path (std::string &path_string);
static bool db_json_isspace (const unsigned char &ch);
static int db_json_convert_pointer_to_sql_path (const char *pointer_path, std::string &sql_path_out);
static int db_json_convert_sql_path_to_pointer (const char *sql_path, std::string &json_pointer_out);
static JSON_PATH_TYPE db_json_get_path_type (std::string &path_string);
STATIC_INLINE void db_json_build_path_special_chars (const JSON_PATH_TYPE &json_path_type,
    std::unordered_map<std::string, std::string> &special_chars);
STATIC_INLINE std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim);
STATIC_INLINE bool db_json_sql_path_is_valid (std::string &sql_path);
STATIC_INLINE int db_json_er_set_path_does_not_exist (const std::string &path, const JSON_DOC *doc);
STATIC_INLINE void db_json_replace_token_special_chars (std::string &token,
    const std::unordered_map<std::string, std::string> &special_chars);
static bool db_json_path_is_token_valid_array_index (const std::string &str, std::size_t start = 0,
    std::size_t end = 0);
static void db_json_doc_wrap_as_array (JSON_DOC &doc);
static void db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator);

STATIC_INLINE JSON_VALUE &db_json_doc_to_value (JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const JSON_VALUE &db_json_doc_to_value (const JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));

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
      free (m_schema_raw);
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

/*
* db_json_doc_to_value ()
* doc (in)
* value (out)
* We need this cast in order to use the overloaded methods
* JSON_DOC is derived from GenericDocument which also extends GenericValue
* Yet JSON_DOC and JSON_VALUE are two different classes because they are templatized and their type is not known
* at compile time
*/
static JSON_VALUE &
db_json_doc_to_value (JSON_DOC &doc)
{
  return reinterpret_cast<JSON_VALUE &> (doc);
}

static const JSON_VALUE &
db_json_doc_to_value (const JSON_DOC &doc)
{
  return reinterpret_cast<const JSON_VALUE &> (doc);
}

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
  else if (document->IsNull ())
    {
      return "JSON_NULL";
    }
  else if (document->IsBool())
    {
      return "BOOLEAN";
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
db_json_extract_document_from_path (const JSON_DOC *document, const char *raw_path, JSON_DOC *&result)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;
  result = NULL;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());
  const JSON_VALUE *resulting_json = NULL;

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

  key.SetString (name, (rapidjson::SizeType) strlen (name), doc->GetAllocator ());
  val.SetString (value, (rapidjson::SizeType) strlen (value), doc->GetAllocator ());
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

  key.SetString (name, (rapidjson::SizeType) strlen (name), doc->GetAllocator ());
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

  key.SetString (name, (rapidjson::SizeType) strlen (name), doc->GetAllocator ());
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
  key.SetString (name, (rapidjson::SizeType) strlen (name), doc->GetAllocator ());
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
  v.SetString (value, (rapidjson::SizeType) strlen (value), doc->GetAllocator ());
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

  if (json_raw == NULL)
    {
      return NO_ERROR;
    }

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
db_json_copy_doc (JSON_DOC &dest, const JSON_DOC *src)
{
  if (db_json_get_type (src) != DB_JSON_NULL)
    {
      dest.CopyFrom (*src, dest.GetAllocator ());
    }
  else
    {
      dest.SetNull ();
    }
}

/*
 * db_json_insert_func () - Insert a document into destination document at given path
 *
 * return                  : error code
 * doc_to_be_inserted (in) : document to be inserted
 * doc_destination (in)    : destination document
 * raw_path (in)           : insertion path
 */
int
db_json_insert_func (const JSON_DOC *doc_to_be_inserted, JSON_DOC &doc_destination, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  // todo: handle NULL doc_to_be_inserted properly

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());
  JSON_VALUE *json_parent_p;

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (p.Get (doc_destination) != NULL)
    {
      // if it exists, just ignore
      // todo: is this a good behavior?
      return NO_ERROR;
    }

  // get parent path
  std::size_t found = json_pointer_string.find_last_of ("/");
  if (found == std::string::npos)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  // parent pointer
  const JSON_POINTER pointer_parent (json_pointer_string.substr (0, found).c_str());
  if (!pointer_parent.IsValid ())
    {
      /* this shouldn't happen */
      assert (false);
      return ER_FAILED;
    }

  json_parent_p = pointer_parent.Get (doc_destination);
  if (json_parent_p != NULL)
    {
      if (json_parent_p->IsObject ())
	{
	  p.Set (doc_destination, *doc_to_be_inserted, doc_destination.GetAllocator ());
	}
      else if (json_parent_p->IsArray ())
	{
	  // since PushBack does not guarantee its argument is not modified, we are forced to copy here. Hopefully,
	  // it doesn't do another copy inside.
	  JSON_VALUE copy_to_be_ins (db_json_doc_to_value (*doc_to_be_inserted), doc_destination.GetAllocator ());
	  json_parent_p->PushBack (copy_to_be_ins, doc_destination.GetAllocator ());
	}
      else
	{
	  db_json_value_wrap_as_array (*json_parent_p, doc_destination.GetAllocator ());

	  // since PushBack does not guarantee its argument is not modified, we are forced to copy here. Hopefully,
	  // it doesn't do another copy inside.
	  JSON_VALUE copy_to_be_ins (db_json_doc_to_value (*doc_to_be_inserted), doc_destination.GetAllocator ());
	  json_parent_p->PushBack (copy_to_be_ins, doc_destination.GetAllocator ());
	}
    }

  return NO_ERROR;
}

int
db_json_replace_func (const JSON_DOC *value, JSON_DOC *doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());

  if (!p.IsValid())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (p.Get (*doc) == NULL)
    {
      return db_json_er_set_path_does_not_exist (json_pointer_string, doc);
    }

  p.Set (*doc, *value, doc->GetAllocator());

  return NO_ERROR;
}

int
db_json_set_func (const JSON_DOC *value, JSON_DOC *doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());
  JSON_VALUE *resulting_json, *resulting_json_parent;

  if (!p.IsValid())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (*doc);

  if (resulting_json != NULL)
    {
      // replace
      p.Set (*doc, *value, doc->GetAllocator());
      return NO_ERROR;
    }

  std::size_t found = json_pointer_string.find_last_of ("/");
  if (found == std::string::npos)
    {
      assert (false);
    }

  const JSON_POINTER pointer_parent (json_pointer_string.substr (0, found).c_str());

  if (!pointer_parent.IsValid())
    {
      /* this shouldn't happen */
      assert (false);
    }

  resulting_json_parent = pointer_parent.Get (*doc);

  if (resulting_json_parent == NULL)
    {
      // we can only create a child value, not both parent and child
      return db_json_er_set_path_does_not_exist (json_pointer_string, doc);
    }

  p.Create (*doc);
  p.Set (*doc, *value, doc->GetAllocator());

  return NO_ERROR;
}

int
db_json_remove_func (JSON_DOC *doc, char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (p.Get (*doc) == NULL)
    {
      return db_json_er_set_path_does_not_exist (json_pointer_string, doc);
    }

  p.Erase (*doc);
  return NO_ERROR;
}

int
db_json_array_append_func (const JSON_DOC *value, JSON_DOC *doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());
  JSON_VALUE *resulting_json;

  if (!p.IsValid())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (*doc);

  if (resulting_json == NULL)
    {
      return db_json_er_set_path_does_not_exist (json_pointer_string, doc);
    }

  if (resulting_json->IsArray())
    {
      JSON_VALUE value_copy (*value, doc->GetAllocator ());
      resulting_json->PushBack (value_copy, doc->GetAllocator());
    }
  else
    {
      db_json_value_wrap_as_array (*resulting_json, doc->GetAllocator ());

      JSON_VALUE value_copy (*value, doc->GetAllocator ());
      resulting_json->PushBack (value_copy, doc->GetAllocator());
    }

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
  else if (val->IsBool())
    {
      return DB_JSON_BOOL;
    }

  return DB_JSON_UNKNOWN;
}

void
db_json_merge_two_json_objects (JSON_DOC &obj1, const JSON_DOC *obj2)
{
  JSON_VALUE obj2_copy;

  assert (db_json_get_type (&obj1) == DB_JSON_OBJECT);
  assert (db_json_get_type (obj2) == DB_JSON_OBJECT);

  obj2_copy.CopyFrom (*obj2, obj1.GetAllocator ());

  for (JSON_VALUE::MemberIterator itr = obj2_copy.MemberBegin (); itr != obj2_copy.MemberEnd (); ++itr)
    {
      const char *name = itr->name.GetString ();

      if (obj1.HasMember (name))
	{
	  if (obj1 [name].IsArray ())
	    {
	      obj1 [name].GetArray ().PushBack (itr->value, obj1.GetAllocator ());
	    }
	  else
	    {
	      db_json_value_wrap_as_array (obj1[name], obj1.GetAllocator ());
	      obj1 [name].PushBack (itr->value, obj1.GetAllocator ());
	    }
	}
      else
	{
	  obj1.AddMember (itr->name, itr->value, obj1.GetAllocator ());
	}
    }
}

void
db_json_merge_two_json_arrays (JSON_DOC &array1, const JSON_DOC *array2)
{
  JSON_VALUE obj2_copy;

  assert (db_json_get_type (&array1) == DB_JSON_ARRAY);
  assert (db_json_get_type (array2) == DB_JSON_ARRAY);

  obj2_copy.CopyFrom (*array2, array1.GetAllocator ());

  for (JSON_VALUE::ValueIterator itr = obj2_copy.Begin (); itr != obj2_copy.End (); ++itr)
    {
      array1.PushBack (*itr, array1.GetAllocator ());
    }
}

// todo: proper description for this function
//
void
db_json_merge_two_json_by_array_wrapping (JSON_DOC &j1, const JSON_DOC *j2)
{
  if (db_json_get_type (&j1) != DB_JSON_ARRAY)
    {
      db_json_doc_wrap_as_array (j1);
    }

  if (db_json_get_type (j2) != DB_JSON_ARRAY)
    {
      // create an array with a single member as j2, then call db_json_merge_two_json_arrays
      JSON_DOC j2_as_array;
      j2_as_array.SetArray ();

      // need a json value clone of j2, because PushBack does not guarantee the const restriction
      JSON_VALUE j2_as_value (db_json_doc_to_value (*j2), j2_as_array.GetAllocator ());
      j2_as_array.PushBack (j2_as_value, j2_as_array.GetAllocator ());

      // merge arrays
      db_json_merge_two_json_arrays (j1, &j2_as_array);

      // todo: we do some memory allocation and copying; maybe we can improve
    }
  else
    {
      db_json_merge_two_json_arrays (j1, j2);
    }
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
db_json_merge_func (const JSON_DOC *source, JSON_DOC *&dest)
{
  if (dest == NULL)
    {
      dest = db_json_allocate_doc ();
      db_json_copy_doc (*dest, source);
      return NO_ERROR;
    }

  if (db_json_get_type (dest) == db_json_get_type (source))
    {
      if (db_json_get_type (dest) == DB_JSON_OBJECT)
	{
	  db_json_merge_two_json_objects (*dest, source);
	}
      else if (db_json_get_type (dest) == DB_JSON_ARRAY)
	{
	  db_json_merge_two_json_arrays (*dest, source);
	}
      else
	{
	  db_json_merge_two_json_by_array_wrapping (*dest, source);
	}
    }
  else
    {
      db_json_merge_two_json_by_array_wrapping (*dest, source);
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

const char *
db_json_get_string_from_document (const JSON_DOC *doc)
{
  return db_json_get_string_from_value (doc);
}

char *
db_json_get_bool_as_str_from_document (const JSON_DOC *doc)
{
  return db_json_get_bool_as_str_from_value (doc);
}

char *
db_json_copy_string_from_document (const JSON_DOC *doc)
{
  return db_json_copy_string_from_value (doc);
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

const char *
db_json_get_string_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return NULL;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_STRING);

  return doc->GetString();
}

char *
db_json_copy_string_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return NULL;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_STRING);
  return db_private_strdup (NULL, doc->GetString());
}

STATIC_INLINE char *
db_json_bool_to_string (bool b)
{
  return b ? db_private_strdup (NULL, "true") : db_private_strdup (NULL, "false");
}

char *
db_json_get_bool_as_str_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return NULL;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_BOOL);
  return db_json_bool_to_string (doc->GetBool());
}

static JSON_PATH_TYPE
db_json_get_path_type (std::string &path_string)
{
  db_json_normalize_path (path_string);

  if (path_string.empty())
    {
      return JSON_PATH_TYPE::JSON_PATH_EMPTY;
    }
  else if (path_string[0] == '$')
    {
      return JSON_PATH_TYPE::JSON_PATH_SQL_JSON;
    }
  else
    {
      return JSON_PATH_TYPE::JSON_PATH_POINTER;
    }
}

/*
* db_json_build_path_special_chars ()
* json_path_type (in)
* special_chars (out)
* rapid json pointer supports URI Fragment Representation
* https://tools.ietf.org/html/rfc3986
* we need a map in order to know how to escape special characters
* example from sql_path to pointer_path: $."/a" -> #/~1a
*/
STATIC_INLINE void
db_json_build_path_special_chars (const JSON_PATH_TYPE &json_path_type,
				  std::unordered_map<std::string, std::string> &special_chars)
{
  for (auto it = uri_fragment_conversions.begin(); it != uri_fragment_conversions.end(); ++it)
    {
      if (json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
	{
	  special_chars.insert (*it);
	}
      else
	{
	  special_chars.insert (std::make_pair (it->second, it->first));
	}
    }
}

/*
* db_json_split_path_by_delimiters ()
* path (in)
* delim (in) supports multiple delimiters
* returns a vector with tokens split by delimiters from the given string
*/
STATIC_INLINE std::vector<std::string>
db_json_split_path_by_delimiters (const std::string &path, const std::string &delim)
{
  std::vector<std::string> tokens;
  std::size_t start = 0;
  std::size_t end = path.find_first_of (delim, start);

  while (end != std::string::npos)
    {
      // do not tokenize on escaped quotes
      if (path[end] != '"' || path[end - 1] != '\\')
	{
	  const std::string &substring = path.substr (start, end - start);
	  if (!substring.empty())
	    {
	      tokens.push_back (substring);
	    }

	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty())
    {
      tokens.push_back (substring);
    }

  return tokens;
}

STATIC_INLINE bool
db_json_sql_path_is_valid (std::string &sql_path)
{
  // skip leading white spaces
  db_json_normalize_path (sql_path);
  if (sql_path.empty ())
    {
      // empty
      return false;
    }

  if (sql_path[0] != '$')
    {
      // first character should always be '$'
      return false;
    }
  // start parsing path string by skipping dollar character
  for (std::size_t i = 1; i < sql_path.length(); ++i)
    {
      // to begin a next token we have only 2 possibilities:
      // with dot we start an object name
      // with bracket we start an index
      switch (sql_path[i])
	{
	case '[':
	{
	  std::size_t end_bracket_offset = sql_path.find_first_of (']', ++i);
	  if (end_bracket_offset == sql_path.npos)
	    {
	      // unacceptable
	      assert (false);
	      return false;
	    }
	  if (!db_json_path_is_token_valid_array_index (sql_path, i, end_bracket_offset))
	    {
	      // expecting a valid index
	      return false;
	    }
	  // move after ']'
	  i = end_bracket_offset + 1;
	}
	break;

	case '.':
	  i++;

	  if (sql_path[i] == '"')
	    {
	      i++;

	      // right now this method accepts escaped quotes with backslash
	      while (i < sql_path.length() && (sql_path[i] != '"' || sql_path[i - 1] == '\\'))
		{
		  i++;
		}

	      if (i == sql_path.length())
		{
		  return false;
		}
	    }
	  else
	    {
	      // we can have an object name without quotes only if the first character is a letter
	      // otherwise we need to put in between quotes
	      if (!std::isalpha (sql_path[i++]))
		{
		  return false;
		}

	      while (i < sql_path.length() && (sql_path[i] != '.' && sql_path[i] != '['))
		{
		  i++;
		}

	      i--;
	    }
	  break;

	default:
	  return false;
	}
    }

  return true;
}

STATIC_INLINE int
db_json_er_set_path_does_not_exist (const std::string &path, const JSON_DOC *doc)
{
  std::string sql_path_string;
  int error_code;

  error_code = db_json_convert_pointer_to_sql_path (path.c_str(), sql_path_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_PATH_DOES_NOT_EXIST, 2,
	  sql_path_string.c_str(), db_json_get_raw_json_body_from_document (doc));
  return ER_JSON_PATH_DOES_NOT_EXIST;
}

/*
* db_json_replace_token_special_chars ()
* token (in)
* special_chars (in)
* this function does the special characters replacements in a token based on mapper
* Example: object~1name -> object/name
*/
STATIC_INLINE void
db_json_replace_token_special_chars (std::string &token,
				     const std::unordered_map<std::string, std::string> &special_chars)
{
  for (auto it = special_chars.begin(); it != special_chars.end(); ++it)
    {
      size_t pos = 0;
      while ((pos = token.find (it->first, pos)) != std::string::npos)
	{
	  token.replace (pos, it->first.length(), it->second);
	  pos += it->second.length();
	}
    }
}

/*
* db_json_convert_pointer_to_sql_path ()
* pointer_path (in)
* sql_path_out (out): the result
* A pointer path is converted to SQL standard path
* Example: /0/name1/name2/2 -> $[0]."name1"."name2"[2]
*/
static int
db_json_convert_pointer_to_sql_path (const char *pointer_path, std::string &sql_path_out)
{
  std::string pointer_path_string (pointer_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (pointer_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_EMPTY ||
      json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      // path is not JSON path format; consider it SQL path.
      sql_path_out = pointer_path_string;
      return NO_ERROR;
    }

  std::unordered_map<std::string, std::string> special_chars;
  sql_path_out = "$";

  db_json_build_path_special_chars (json_path_type, special_chars);

  // starting the conversion of path
  // first we need to split into tokens
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (pointer_path_string, "/");

  for (std::size_t i = 0; i < tokens.size(); ++i)
    {
      std::string &token = tokens[i];

      if (db_json_path_is_token_valid_array_index (token))
	{
	  sql_path_out += "[";
	  sql_path_out += token;
	  sql_path_out += "]";
	}
      else
	{
	  sql_path_out += ".\"";
	  // replace special characters if necessary based on mapper
	  db_json_replace_token_special_chars (token, special_chars);
	  sql_path_out += token;
	  sql_path_out += "\"";
	}
    }

  return NO_ERROR;
}

static bool
db_json_isspace (const unsigned char &ch)
{
  return std::isspace (ch) != 0;
}

static void
db_json_normalize_path (std::string &path_string)
{
  // trim leading spaces
  auto first_non_space = std::find_if_not (path_string.begin(), path_string.end(), db_json_isspace);
  path_string.erase (path_string.begin(), first_non_space);
}

/*
* db_json_convert_sql_path_to_pointer ()
* sql_path (in)
* json_pointer_out (out): the result
* An sql_path is converted to rapidjson standard path
* Example: $[0]."name1".name2[2] -> /0/name1/name2/2
*/
static int
db_json_convert_sql_path_to_pointer (const char *sql_path, std::string &json_pointer_out)
{
  std::string sql_path_string (sql_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (sql_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_EMPTY ||
      json_path_type == JSON_PATH_TYPE::JSON_PATH_POINTER)
    {
      // path is not SQL path format; consider it JSON pointer.
      json_pointer_out = sql_path_string;
      return NO_ERROR;
    }

  if (!db_json_sql_path_is_valid (sql_path_string))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  std::unordered_map<std::string, std::string> special_chars;

  db_json_build_path_special_chars (json_path_type, special_chars);

  // first we need to split into tokens
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (sql_path_string, "$.[]\"");

  // build json pointer
  json_pointer_out = "";
  for (unsigned int i = 0; i < tokens.size(); ++i)
    {
      json_pointer_out += "/" + tokens[i];
    }

  return NO_ERROR;
}

static void
db_json_get_paths_helper (const JSON_VALUE &obj, const std::string &sql_path, std::vector<std::string> &paths)
{
  if (obj.IsArray())
    {
      int count = 0;

      for (auto it = obj.GetArray().begin(); it != obj.GetArray().end(); ++it)
	{
	  std::stringstream ss;
	  ss << sql_path << "[" << count++ << "]";
	  db_json_get_paths_helper (*it, ss.str(), paths);
	}
    }
  else if (obj.IsObject())
    {
      for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it)
	{
	  std::stringstream ss;
	  ss << sql_path << '.' << '"' << it->name.GetString() << '"';
	  db_json_get_paths_helper (it->value, ss.str(), paths);
	}
    }

  paths.push_back (sql_path);
}

int
db_json_get_all_paths_func (const JSON_DOC &doc, JSON_DOC *&result_json)
{
  JSON_POINTER p ("");
  const JSON_VALUE *head = p.Get (doc);
  std::vector<std::string> paths;

  db_json_get_paths_helper (*head, "$", paths);

  result_json->SetArray();

  for (auto it = paths.begin(); it != paths.end(); ++it)
    {
      JSON_VALUE val;
      val.SetString (it->c_str(), result_json->GetAllocator());
      result_json->PushBack (val, result_json->GetAllocator());
    }

  return NO_ERROR;
}

int
db_json_keys_func (const JSON_DOC &doc, JSON_DOC *&result_json, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());

  if (!p.IsValid())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  const JSON_VALUE *head = p.Get (doc);
  std::string key;

  if (head == NULL)
    {
      return db_json_er_set_path_does_not_exist (json_pointer_string, &doc);
    }
  else if (head->IsObject())
    {
      result_json->SetArray();

      for (auto it = head->MemberBegin(); it != head->MemberEnd(); ++it)
	{
	  JSON_VALUE val;

	  key = it->name.GetString();
	  val.SetString (key.c_str(), result_json->GetAllocator());
	  result_json->PushBack (val, result_json->GetAllocator());
	}
    }

  return NO_ERROR;
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
	      bool res = false;

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
	  result = true; // empty json value is considered included: json_contains('{"a":1}', '{}') => true
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

void
db_json_make_document_null (JSON_DOC *doc)
{
  if (doc != NULL)
    {
      doc->SetNull ();
    }
}

bool db_json_doc_has_numeric_type (const JSON_DOC *doc)
{
  return db_json_value_has_numeric_type (doc);
}

bool db_json_doc_is_uncomparable (const JSON_DOC *doc)
{
  DB_JSON_TYPE type = db_json_get_type (doc);

  return (type == DB_JSON_ARRAY || type == DB_JSON_OBJECT);
}

/*
 * db_json_path_is_token_valid_array_index () - verify if token is a valid array index. token can be a substring of
 *                                              first argument (by default the entire argument).
 *
 * return     : true if all token characters are digits (valid index)
 * str (in)   : token or the string that token belong to
 * start (in) : start of token; default is start of string
 * end (in)   : end of token; default is end of string; 0 is considered default value
 */
static bool
db_json_path_is_token_valid_array_index (const std::string &str, std::size_t start, std::size_t end)
{
  if (end == 0)
    {
      // default is end of string
      end = str.length ();
    }
  for (auto it = str.cbegin () + start; it < str.cbegin () + end; it++)
    {
      if (!std::isdigit (*it))
	{
	  return false;
	}
    }
  // all are digits; this is a valid array index
  return true;
}

/************************************************************************/
/* JSON_DOC implementation                                              */
/************************************************************************/

bool JSON_DOC::IsLeaf ()
{
  return !IsArray () && !IsObject ();
}

static void
db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator)
{
  JSON_VALUE swap_value;

  swap_value.SetArray ();
  swap_value.PushBack (value, allocator);
  swap_value.Swap (value);
}

static void
db_json_doc_wrap_as_array (JSON_DOC &doc)
{
  return db_json_value_wrap_as_array (doc, doc.GetAllocator ());
}
