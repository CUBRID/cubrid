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

#include "dbtype.h"
#include "memory_alloc.h"
#include "system_parameter.h"

// we define COPY in storage_common.h, but so does rapidjson in its headers. We don't need the definition from storage
// common, so thankfully we can undef it here. But we should really consider remove that definition
#undef COPY

#include "rapidjson/allocators.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <sstream>

#include <algorithm>
#include <locale>
#include <unordered_map>
#include <vector>
#include <climits>

#include <cctype>

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

class JSON_ITERATOR
{
  public:
    JSON_ITERATOR (const JSON_DOC &document) : document (&document) {}

    virtual const JSON_VALUE *next() = 0;
    virtual bool has_next() = 0;
    virtual const JSON_VALUE *get() = 0;

  protected:
    const JSON_DOC *document;
};

class JSON_OBJECT_ITERATOR : JSON_ITERATOR
{
    JSON_OBJECT_ITERATOR (const JSON_DOC &document) : JSON_ITERATOR (document)
    {
      assert (document.IsObject());

      iterator = document.MemberBegin();
    }

    const JSON_VALUE *next();
    bool has_next();

    const JSON_VALUE *get()
    {
      return &iterator->value;
    }

  private:
    rapidjson::GenericMemberIterator<true, JSON_ENCODING, JSON_PRIVATE_MEMPOOL>::Iterator iterator;
};

class JSON_ARRAY_ITERATOR : JSON_ITERATOR
{
    JSON_ARRAY_ITERATOR (const JSON_DOC &document) : JSON_ITERATOR (document)
    {
      assert (document.IsArray());

      iterator = document.GetArray().Begin();
    }

    const JSON_VALUE *next();
    bool has_next();

    const JSON_VALUE *get()
    {
      return iterator;
    }

  private:
    rapidjson::GenericArray<true, JSON_VALUE>::ConstValueIterator iterator;
};

const JSON_VALUE *
JSON_ARRAY_ITERATOR::next()
{
  if (!has_next())
    {
      return NULL;
    }

  return iterator++;
}

bool
JSON_ARRAY_ITERATOR::has_next()
{
  return iterator != document->GetArray().End();
}

const JSON_VALUE *
JSON_OBJECT_ITERATOR::next()
{
  if (!has_next())
    {
      return NULL;
    }

  const JSON_VALUE *value = &iterator->value;
  ++iterator;

  return value;
}

bool
JSON_OBJECT_ITERATOR::has_next()
{
  return iterator != document->MemberEnd();
}

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

/*
* JSON_BASE_HANDLER - This class acts like a rapidjson Handler
*
* The Handler is used by the json document to make checks on all of its nodes
* It is applied recursively by the Accept function and acts like a map functions
* You should inherit this class each time you want a specific function to apply to all the nodes in the json document
* and override only the methods that apply to the desired types of nodes
*/
class JSON_BASE_HANDLER
{
  public:
    JSON_BASE_HANDLER() {};
    typedef typename JSON_DOC::Ch Ch;
    typedef unsigned SizeType;

    bool Null()
    {
      return true;
    }
    bool Bool (bool b)
    {
      return true;
    }
    bool Int (int i)
    {
      return true;
    }
    bool Uint (unsigned i)
    {
      return true;
    }
    bool Int64 (int64_t i)
    {
      return true;
    }
    bool Uint64 (uint64_t i)
    {
      return true;
    }
    bool Double (double d)
    {
      return true;
    }
    bool RawNumber (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    bool String (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    bool StartObject()
    {
      return true;
    }
    bool Key (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    bool EndObject (SizeType memberCount)
    {
      return true;
    }
    bool StartArray()
    {
      return true;
    }
    bool EndArray (SizeType elementCount)
    {
      return true;
    }
};

// JSON WALKER
//
// Unlike handler, the walker can call two functions before and after walking/advancing in the JSON "tree".
// JSON Objects and JSON Arrays are considered tree children.
//
// How to use: extend this walker by implementing CallBefore and/or CallAfter functions. By default, they are empty
//
class JSON_WALKER
{
  public:
    int WalkDocument (JSON_DOC &document);

  protected:
    // we should not instantiate this class, but extend it
    JSON_WALKER() {}
    virtual ~JSON_WALKER() {}

    virtual int
    CallBefore (JSON_VALUE &value)
    {
      // do nothing
      return NO_ERROR;
    }

    virtual int
    CallAfter (JSON_VALUE &value)
    {
      // do nothing
      return NO_ERROR;
    }

  private:
    int WalkValue (JSON_VALUE &value);
};

/*
* JSON_DUPLICATE_KEYS_CHECKER - This class extends JSON_WALKER
*
* We use the WalkDocument function to iterate recursively through the json "tree"
* For each node we will call two functions (Before and After) to apply a logic to that node
* In this case, we will check in the CallBefore function if the current node has duplicate keys
*/
class JSON_DUPLICATE_KEYS_CHECKER : public JSON_WALKER
{
  public:
    JSON_DUPLICATE_KEYS_CHECKER () {}
    ~JSON_DUPLICATE_KEYS_CHECKER() {}

  private:
    int CallBefore (JSON_VALUE &value);
};

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;
const int JSON_DOC::MAX_CHUNK_SIZE = 64 * 1024; /* TODO does 64K serve our needs? */

static std::vector<std::pair<std::string, std::string> > uri_fragment_conversions =
{
  std::make_pair ("~", "~0"),
  std::make_pair ("/", "~1"),
  std::make_pair (" ", "%20")
};
static const char *db_Json_pointer_delimiters = "/";
static const char *db_Json_sql_path_delimiters = "$.[]\"";

static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);
static int db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result);
static DB_JSON_TYPE db_json_get_type_of_value (const JSON_VALUE *val);
static bool db_json_value_has_numeric_type (const JSON_VALUE *doc);
static int db_json_get_int_from_value (const JSON_VALUE *val);
static double db_json_get_double_from_value (const JSON_VALUE *doc);
static const char *db_json_get_string_from_value (const JSON_VALUE *doc);
static char *db_json_copy_string_from_value (const JSON_VALUE *doc);
static char *db_json_get_bool_as_str_from_value (const JSON_VALUE *doc);
static char *db_json_bool_to_string (bool b);
static void db_json_merge_two_json_objects (JSON_DOC &first, const JSON_DOC *second);
static void db_json_merge_two_json_arrays (JSON_DOC &array1, const JSON_DOC *array2);
static void db_json_merge_two_json_by_array_wrapping (JSON_DOC &j1, const JSON_DOC *j2);
static void db_json_copy_doc (JSON_DOC &dest, const JSON_DOC *src);

static void db_json_get_paths_helper (const JSON_VALUE &obj, const std::string &sql_path,
				      std::vector<std::string> &paths);
static void db_json_normalize_path (std::string &path_string);
static void db_json_remove_leading_zeros_index (std::string &index);
static bool db_json_isspace (const unsigned char &ch);
static bool db_json_iszero (const unsigned char &ch);
static int db_json_convert_pointer_to_sql_path (const char *pointer_path, std::string &sql_path_out);
static int db_json_convert_sql_path_to_pointer (const char *sql_path, std::string &json_pointer_out);
static JSON_PATH_TYPE db_json_get_path_type (std::string &path_string);
static void db_json_build_path_special_chars (const JSON_PATH_TYPE &json_path_type,
    std::unordered_map<std::string, std::string> &special_chars);
static std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim);
static bool db_json_sql_path_is_valid (std::string &sql_path);
static int db_json_er_set_path_does_not_exist (const char *file_name, const int line_no, const std::string &path,
    const JSON_DOC *doc);
static void db_json_replace_token_special_chars (std::string &token,
    const std::unordered_map<std::string, std::string> &special_chars);
static bool db_json_path_is_token_valid_array_index (const std::string &str, std::size_t start = 0,
    std::size_t end = 0);
static void db_json_doc_wrap_as_array (JSON_DOC &doc);
static void db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator);
static const char *db_json_get_json_type_as_str (const DB_JSON_TYPE &json_type);
static int db_json_er_set_expected_other_type (const char *file_name, const int line_no, const std::string &path,
    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type,
    const DB_JSON_TYPE &expected_type_optional = DB_JSON_NULL);
static int db_json_insert_helper (const JSON_DOC *value, JSON_DOC &doc, JSON_POINTER &p, const std::string &path);
static int db_json_contains_duplicate_keys (JSON_DOC &doc);
static int db_json_keys_func (const JSON_DOC &doc, JSON_DOC &result_json, const char *raw_path);

STATIC_INLINE JSON_VALUE &db_json_doc_to_value (JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const JSON_VALUE &db_json_doc_to_value (const JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE JSON_DOC &db_json_value_to_doc (JSON_VALUE &value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const JSON_DOC &db_json_value_to_doc (const JSON_VALUE &value) __attribute__ ((ALWAYS_INLINE));
static int db_json_get_json_from_str (const char *json_raw, JSON_DOC &doc);
static int db_json_add_json_value_to_object (JSON_DOC &doc, const char *name, JSON_VALUE &value);

int JSON_DUPLICATE_KEYS_CHECKER::CallBefore (JSON_VALUE &value)
{
  std::vector<const char *> inserted_keys;

  if (value.IsObject())
    {
      for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
	{
	  const char *current_key = it->name.GetString();

	  for (unsigned int i = 0; i < inserted_keys.size(); i++)
	    {
	      if (strcmp (current_key, inserted_keys[i]) == 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_DUPLICATE_KEY, 1, current_key);
		  return ER_JSON_DUPLICATE_KEY;
		}
	    }

	  inserted_keys.push_back (current_key);
	}
    }

  return NO_ERROR;
}

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (m_document.GetParseError ()), m_document.GetErrorOffset ());
      return ER_JSON_INVALID_JSON;
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

static JSON_DOC &
db_json_value_to_doc (JSON_VALUE &value)
{
  return reinterpret_cast<JSON_DOC &> (value);
}

static const JSON_DOC &
db_json_value_to_doc (const JSON_VALUE &value)
{
  return reinterpret_cast<const JSON_DOC &> (value);
}

const JSON_DOC *
db_json_iterator_next (JSON_ITERATOR &json_itr)
{
  return &db_json_value_to_doc (*json_itr.next());
}

const JSON_DOC *
db_json_iterator_get (JSON_ITERATOR &json_itr)
{
  return &db_json_value_to_doc (*json_itr.get());
}

bool
db_json_iterator_has_next (JSON_ITERATOR &json_itr)
{
  return json_itr.has_next();
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
  else if (document->IsBool ())
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

static const char *
db_json_get_json_type_as_str (const DB_JSON_TYPE &json_type)
{
  switch (json_type)
    {
    case DB_JSON_ARRAY:
      return "JSON_ARRAY";
    case DB_JSON_OBJECT:
      return "JSON_OBJECT";
    case DB_JSON_INT:
      return "INTEGER";
    case DB_JSON_DOUBLE:
      return "DOUBLE";
    case DB_JSON_STRING:
      return "STRING";
    case DB_JSON_NULL:
      return "JSON_NULL";
    case DB_JSON_BOOL:
      return "BOOLEAN";
    default:
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
 * db_json_extract_document_from_path () - Extracts from within the json a value based on the given path
 *
 * return                  : error code
 * doc_to_be_inserted (in) : document to be inserted
 * doc_destination (in)    : destination document
 * raw_path (in)           : insertion path
 * example                 : json_extract('{"a":["b", 123]}', '/a/1') yields 123
 */

int
db_json_extract_document_from_path (const JSON_DOC *document, const char *raw_path, JSON_DOC *&result)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;
  result = NULL;

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());
  const JSON_VALUE *resulting_json = NULL;

  if (!p.IsValid ())
    {
      result = NULL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  // the json from the specified path
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

/*
* db_json_contains_path () - Checks if the document contains data at given path
*
* return                  : error code
* document (in)           : document where to search
* raw_path (in)           : check path
* result (out)            : true/false
*/
int
db_json_contains_path (const JSON_DOC *document, const char *raw_path, bool &result)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;
  result = false;

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str());

  // the actual search of the path
  result = p.IsValid();

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

static int
db_json_add_json_value_to_object (JSON_DOC &doc, const char *name, JSON_VALUE &value)
{
  JSON_VALUE key;

  if (!doc.IsObject())
    {
      doc.SetObject();
    }

  if (doc.HasMember (name))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_DUPLICATE_KEY, 1, name);
      return ER_JSON_DUPLICATE_KEY;
    }

  key.SetString (name, (rapidjson::SizeType) strlen (name), doc.GetAllocator());
  doc.AddMember (key, value, doc.GetAllocator());
  return NO_ERROR;
}

int
db_json_add_member_to_object (JSON_DOC *doc, const char *name, const char *value)
{
  JSON_VALUE val;
  val.SetString (value, (rapidjson::SizeType) strlen (value), doc->GetAllocator ());

  return db_json_add_json_value_to_object (*doc, name, val);
}

int
db_json_add_member_to_object (JSON_DOC *doc, const char *name, int value)
{
  JSON_VALUE val;
  val.SetInt (value);

  return db_json_add_json_value_to_object (*doc, name, val);
}

int
db_json_add_member_to_object (JSON_DOC *doc, const char *name, const JSON_DOC *value)
{
  JSON_VALUE val;

  if (value != NULL)
    {
      val.CopyFrom (*value, doc->GetAllocator ());
    }
  else
    {
      val.SetNull ();
    }

  return db_json_add_json_value_to_object (*doc, name, val);
}

int
db_json_add_member_to_object (JSON_DOC *doc, const char *name, double value)
{
  JSON_VALUE val;
  val.SetDouble (value);

  return db_json_add_json_value_to_object (*doc, name, val);
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

/*
* db_json_contains_duplicate_keys () - Checks at parse time if a json document has duplicate keys
*
* return                  : error_code
* doc (in)                : json document
*/
static int
db_json_contains_duplicate_keys (JSON_DOC &doc)
{
  JSON_DUPLICATE_KEYS_CHECKER dup_keys_checker;
  int error_code = NO_ERROR;

  // check recursively for duplicate keys in the json document
  error_code = dup_keys_checker.WalkDocument (doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
    }

  return error_code;
}

static int
db_json_get_json_from_str (const char *json_raw, JSON_DOC &doc)
{
  int error_code = NO_ERROR;
  if (json_raw == NULL)
    {
      return NO_ERROR;
    }

  if (doc.Parse (json_raw).HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (doc.GetParseError ()), doc.GetErrorOffset ());
      return ER_JSON_INVALID_JSON;
    }

  error_code = db_json_contains_duplicate_keys (doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  return NO_ERROR;
}

int
db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc)
{
  int err;

  assert (doc == NULL);

  doc = db_json_allocate_doc ();

  err = db_json_get_json_from_str (json_raw, *doc);
  if (err != NO_ERROR)
    {
      delete doc;
      doc = NULL;
    }

  return err;
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

static int
db_json_insert_helper (const JSON_DOC *value, JSON_DOC &doc, JSON_POINTER &p, const std::string &path)
{
  std::size_t found = path.find_last_of ("/");
  if (found == std::string::npos)
    {
      assert (false);
      return ER_FAILED;
    }

  // parent pointer
  const JSON_POINTER pointer_parent (path.substr (0, found).c_str());
  if (!pointer_parent.IsValid())
    {
      /* this shouldn't happen */
      assert (false);
      return ER_FAILED;
    }

  JSON_VALUE *resulting_json_parent = pointer_parent.Get (doc);
  // the parent does not exist
  if (resulting_json_parent == NULL)
    {
      // we can only create a child value, not both parent and child
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, path.substr (0, found), &doc);
    }

  // found type of parent
  DB_JSON_TYPE parent_json_type = db_json_get_type_of_value (resulting_json_parent);

  // we can insert only in JSON_OBJECT or JSON_ARRAY, else throw an error
  if (parent_json_type != DB_JSON_OBJECT && parent_json_type != DB_JSON_ARRAY)
    {
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, parent_json_type, DB_JSON_OBJECT, DB_JSON_ARRAY);
    }

  const std::string &last_token = path.substr (found + 1);
  bool token_is_valid_index = db_json_path_is_token_valid_array_index (last_token);

  if (parent_json_type == DB_JSON_ARRAY && !token_is_valid_index)
    {
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, parent_json_type, DB_JSON_OBJECT);
    }
  if (parent_json_type == DB_JSON_OBJECT && token_is_valid_index)
    {
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, parent_json_type, DB_JSON_ARRAY);
    }

  // put the value at the specified path
  p.Set (doc, *value, doc.GetAllocator());
  return NO_ERROR;
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

  if (doc_to_be_inserted == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());

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

  return db_json_insert_helper (doc_to_be_inserted, doc_destination, p, json_pointer_string);
}

/*
 * db_json_replace_func () - Replaces the value from the specified path in a JSON document with a new value
 *
 * return                  : error code
 * new_value (in)          : the value to be set at the specified path
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_replace_func (const JSON_DOC *new_value, JSON_DOC &doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  if (new_value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (p.Get (doc) == NULL)
    {
      // if the path does not exist, raise an error
      // the user should know that the command will have no effect
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, json_pointer_string, &doc);
    }

  // replace the value from the specified path with the new value
  p.Set (doc, *new_value, doc.GetAllocator ());

  return NO_ERROR;
}

/*
 * db_json_set_func () - Inserts or updates data in a JSON document at a specified path
 *
 * return                  : error code
 * value (in)              : the value to be set at the specified path
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_set_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());
  JSON_VALUE *resulting_json;

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (doc);

  if (resulting_json != NULL)
    {
      // replace the old value with the new one if the path exists
      p.Set (doc, *value, doc.GetAllocator ());
      return NO_ERROR;
    }

  // here starts the INSERTION part
  return db_json_insert_helper (value, doc, p, json_pointer_string);
}

/*
 * db_json_remove_func () - Removes data from a JSON document at the specified path
 *
 * return                  : error code
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_remove_func (JSON_DOC &doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  // if the path does not exist, the user should know that the path has no effect
  if (p.Get (doc) == NULL)
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, json_pointer_string, &doc);
    }

  // erase the value from the specified path
  p.Erase (doc);

  return NO_ERROR;
}

/*
 * db_json_array_append_func () - Append the value to the end of the indicated array within a JSON document
 *
 * return                  : error code
 * value (in)              : the value to be added in the array
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_array_append_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());
  JSON_VALUE *resulting_json;

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  resulting_json = p.Get (doc);

  if (resulting_json == NULL)
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, json_pointer_string, &doc);
    }

  // the specified path is not an array
  // it means we have just one element at the specified path
  if (!resulting_json->IsArray ())
    {
      // we need to create an array with the value from the specified path
      // example: for json {"a" : "b"} and path '/a' --> {"a" : ["b"]}
      db_json_value_wrap_as_array (*resulting_json, doc.GetAllocator ());
    }

  // add the value at the end of the array
  JSON_VALUE value_copy (*value, doc.GetAllocator ());
  resulting_json->PushBack (value_copy, doc.GetAllocator ());

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
  else if (val->IsBool ())
    {
      return DB_JSON_BOOL;
    }

  return DB_JSON_UNKNOWN;
}

/*
 * db_json_merge_two_json_objects () - Merge the source object into the destination object
 *
 * return                  : error code
 * dest (in)               : json where to merge
 * source (in)             : json to merge
 * example                 : let dest = '{"a" : "b"}'
 *                           let source = '{"c" : "d"}'
 *                           after JSON_MERGE(dest, source), dest = {"a" : "b", "c" : "d"}
 */
void
db_json_merge_two_json_objects (JSON_DOC &dest, const JSON_DOC *source)
{
  JSON_VALUE source_copy;

  assert (db_json_get_type (&dest) == DB_JSON_OBJECT);
  assert (db_json_get_type (source) == DB_JSON_OBJECT);

  // create a copy for the source json
  source_copy.CopyFrom (*source, dest.GetAllocator ());

  // iterate through each member from the source json and insert it into the dest
  for (JSON_VALUE::MemberIterator itr = source_copy.MemberBegin (); itr != source_copy.MemberEnd (); ++itr)
    {
      const char *name = itr->name.GetString ();

      // if the key is in both jsons
      if (dest.HasMember (name))
	{
	  if (dest [name].IsArray ())
	    {
	      dest [name].GetArray ().PushBack (itr->value, dest.GetAllocator ());
	    }
	  else
	    {
	      db_json_value_wrap_as_array (dest[name], dest.GetAllocator ());
	      dest [name].PushBack (itr->value, dest.GetAllocator ());
	    }
	}
      else
	{
	  dest.AddMember (itr->name, itr->value, dest.GetAllocator ());
	}
    }
}

/*
 * db_json_merge_two_json_arrays () - Merge the source json into destination json
 *
 * return                  : error code
 * dest (in)               : json where to merge
 * source (in)             : json to merge
 * example                 : let dest = '[1, 2]'
 *                           let source = '[true, false]'
 *                           after JSON_MERGE(dest, source), dest = [1, 2, true, false]
 */
void
db_json_merge_two_json_arrays (JSON_DOC &dest, const JSON_DOC *source)
{
  JSON_VALUE source_copy;

  assert (db_json_get_type (&dest) == DB_JSON_ARRAY);
  assert (db_json_get_type (source) == DB_JSON_ARRAY);

  source_copy.CopyFrom (*source, dest.GetAllocator ());

  for (JSON_VALUE::ValueIterator itr = source_copy.Begin (); itr != source_copy.End (); ++itr)
    {
      dest.PushBack (*itr, dest.GetAllocator ());
    }
}

/*
 * db_json_merge_two_json_by_array_wrapping () - Merge the source json into destination json
 * This method should be called when jsons have different types
 *
 * return                  : error code
 * dest (in)               : json where to merge
 * source (in)             : json to merge
 */
void
db_json_merge_two_json_by_array_wrapping (JSON_DOC &dest, const JSON_DOC *source)
{
  if (db_json_get_type (&dest) != DB_JSON_ARRAY)
    {
      db_json_doc_wrap_as_array (dest);
    }

  if (db_json_get_type (source) != DB_JSON_ARRAY)
    {
      // create an array with a single member as source, then call db_json_merge_two_json_arrays
      JSON_DOC source_as_array;
      source_as_array.SetArray ();

      // need a json value clone of source, because PushBack does not guarantee the const restriction
      JSON_VALUE source_as_value (db_json_doc_to_value (*source), source_as_array.GetAllocator ());
      source_as_array.PushBack (source_as_value, source_as_array.GetAllocator ());

      // merge arrays
      db_json_merge_two_json_arrays (dest, &source_as_array);

      // todo: we do some memory allocation and copying; maybe we can improve
    }
  else
    {
      db_json_merge_two_json_arrays (dest, source);
    }
}

int
db_json_object_contains_key (JSON_DOC *obj, const char *key, int &result)
{
  if (!obj->IsObject ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_NO_JSON_OBJECT_PROVIDED, 0);
      return ER_JSON_NO_JSON_OBJECT_PROVIDED;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (document.GetParseError ()), document.GetErrorOffset ());
      return ER_JSON_INVALID_JSON;
    }

  return NO_ERROR;
}

JSON_DOC *db_json_allocate_doc ()
{
  JSON_DOC *doc = new JSON_DOC();
  return doc;
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

  return doc->GetString ();
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
  return db_private_strdup (NULL, doc->GetString ());
}

static char *
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
  return db_json_bool_to_string (doc->GetBool ());
}

static JSON_PATH_TYPE
db_json_get_path_type (std::string &path_string)
{
  db_json_normalize_path (path_string);

  if (path_string.empty ())
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
static void
db_json_build_path_special_chars (const JSON_PATH_TYPE &json_path_type,
				  std::unordered_map<std::string, std::string> &special_chars)
{
  for (auto it = uri_fragment_conversions.begin (); it != uri_fragment_conversions.end (); ++it)
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
static std::vector<std::string>
db_json_split_path_by_delimiters (const std::string &path, const std::string &delim)
{
  std::vector<std::string> tokens;
  std::size_t start = 0;
  std::size_t end = path.find_first_of (delim, start);

  while (end != std::string::npos)
    {
      if (path[end] == '"')
	{
	  std::size_t index_of_closing_quote = path.find_first_of ("\"", end+1);
	  if (index_of_closing_quote == std::string::npos)
	    {
	      assert (false);
	      tokens.clear ();
	      return tokens;
	      /* this should have been catched earlier */
	    }
	  else
	    {
	      tokens.push_back (path.substr (end + 1, index_of_closing_quote - end - 1));
	      end = index_of_closing_quote;
	      start = end + 1;
	    }
	}
      // do not tokenize on escaped quotes
      else if (path[end] != '"' || ((end >= 1) && path[end - 1] != '\\'))
	{
	  const std::string &substring = path.substr (start, end - start);
	  if (!substring.empty ())
	    {
	      tokens.push_back (substring);
	    }

	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty ())
    {
      tokens.push_back (substring);
    }

  std::size_t tokens_size = tokens.size();
  for (std::size_t i = 0; i < tokens_size; i++)
    {
      if (db_json_path_is_token_valid_array_index (tokens[i]))
	{
	  db_json_remove_leading_zeros_index (tokens[i]);
	}
    }

  return tokens;
}

/*
 * db_json_sql_path_is_valid () - Check if a given path is a SQL valid path
 *
 * return                  : true/false
 * sql_path (in)           : path to be checked
 */
static bool
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
  for (std::size_t i = 1; i < sql_path.length (); ++i)
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
	  // move to ']'. i will be incremented.
	  i = end_bracket_offset;
	}
	break;

	case '.':
	  i++;

	  if (sql_path[i] == '"')
	    {
	      i++;

	      // right now this method accepts escaped quotes with backslash
	      while (i < sql_path.length () && (sql_path[i] != '"' || sql_path[i - 1] == '\\'))
		{
		  i++;
		}

	      if (i == sql_path.length ())
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

	      while (i < sql_path.length () && (sql_path[i] != '.' && sql_path[i] != '['))
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

/*
 * db_json_er_set_path_does_not_exist () - Set an error if the path does not exist in JSON document
 * This method is called internaly in the json functions if we can not access the element from the specified path
 *
 * return                  : error code
 * path (in)               : path that does not exist
 * doc (in)                : json document
 */
static int
db_json_er_set_path_does_not_exist (const char *file_name, const int line_no, const std::string &path,
				    const JSON_DOC *doc)
{
  std::string sql_path_string;
  int error_code;

  // the path must be SQL path
  error_code = db_json_convert_pointer_to_sql_path (path.c_str (), sql_path_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // get the json body
  char *raw_json_body = db_json_get_raw_json_body_from_document (doc);
  PRIVATE_UNIQUE_PTR<char> unique_ptr (raw_json_body, NULL);

  er_set (ER_ERROR_SEVERITY, file_name, line_no, ER_JSON_PATH_DOES_NOT_EXIST, 2,
	  sql_path_string.c_str (), raw_json_body);

  return ER_JSON_PATH_DOES_NOT_EXIST;
}

static int
db_json_er_set_expected_other_type (const char *file_name, const int line_no, const std::string &path,
				    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type, const DB_JSON_TYPE &expected_type_optional)
{
  std::string sql_path_string;
  int error_code = NO_ERROR;

  // the path must be SQL path
  error_code = db_json_convert_pointer_to_sql_path (path.c_str(), sql_path_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  const char *found_type_str = db_json_get_json_type_as_str (found_type);
  std::string expected_type_str = db_json_get_json_type_as_str (expected_type);

  if (expected_type_optional != DB_JSON_NULL)
    {
      expected_type_str += " or ";
      expected_type_str += db_json_get_json_type_as_str (expected_type_optional);
    }

  er_set (ER_ERROR_SEVERITY, file_name, line_no, ER_JSON_EXPECTED_OTHER_TYPE, 3,
	  sql_path_string.c_str(), expected_type_str.c_str(), found_type_str);

  return ER_JSON_EXPECTED_OTHER_TYPE;
}

/*
* db_json_replace_token_special_chars ()
* token (in)
* special_chars (in)
* this function does the special characters replacements in a token based on mapper
* Example: object~1name -> object/name
*/
static void
db_json_replace_token_special_chars (std::string &token,
				     const std::unordered_map<std::string, std::string> &special_chars)
{
  bool replaced = false;

  // iterate character by character and detect special characters
  for (size_t token_idx = 0; token_idx < token.length (); /* incremented in for body */)
    {
      replaced = false;
      // compare with special characters
      for (auto special_it = special_chars.begin (); special_it != special_chars.end (); ++special_it)
	{
	  // compare special characters with sequence following token_it
	  if (token_idx + special_it->first.length () <= token.length ())
	    {
	      if (token.compare (token_idx, special_it->first.length (), special_it->first.c_str ()) == 0)
		{
		  // replace
		  token.replace (token_idx, special_it->first.length (), special_it->second);
		  // skip replaced
		  token_idx += special_it->second.length ();

		  replaced = true;
		  // next loop
		  break;
		}
	    }
	}

      if (!replaced)
	{
	  // no match; next character
	  token_idx++;
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

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_EMPTY
      || json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
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
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (pointer_path_string, db_Json_pointer_delimiters);

  for (std::size_t i = 0; i < tokens.size (); ++i)
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
  auto first_non_space = std::find_if_not (path_string.begin (), path_string.end (), db_json_isspace);
  path_string.erase (path_string.begin (), first_non_space);
}

static bool
db_json_iszero (const unsigned char &ch)
{
  return ch == '0';
}

/* db_json_remove_leading_zeros_index () - Erase leading zeros from sql path index
*
* index (in)                : current object
* example: $[000123] -> $[123]
*/
static void
db_json_remove_leading_zeros_index (std::string &index)
{
  // trim leading zeros
  auto first_non_zero = std::find_if_not (index.begin(), index.end(), db_json_iszero);
  index.erase (index.begin(), first_non_zero);

  if (index.empty())
    {
      index = "0";
    }
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

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_EMPTY
      || json_path_type == JSON_PATH_TYPE::JSON_PATH_POINTER)
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
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (sql_path_string, db_Json_sql_path_delimiters);

  // build json pointer
  json_pointer_out = "";
  for (unsigned int i = 0; i < tokens.size (); ++i)
    {
      db_json_replace_token_special_chars (tokens[i], special_chars);
      json_pointer_out += "/" + tokens[i];
    }

  return NO_ERROR;
}

/* db_json_get_paths_helper () - Recursive function to get the paths from a json object
 *
 * obj (in)                : current object
 * sql_path (in)           : the path for the current object
 * paths (in)              : vector where we will store all the paths
 */
static void
db_json_get_paths_helper (const JSON_VALUE &obj, const std::string &sql_path, std::vector<std::string> &paths)
{
  // iterate through the array or object and call recursively the function until we reach a single object
  if (obj.IsArray ())
    {
      int count = 0;

      for (auto it = obj.GetArray ().begin (); it != obj.GetArray ().end (); ++it)
	{
	  std::stringstream ss;
	  ss << sql_path << "[" << count++ << "]";
	  db_json_get_paths_helper (*it, ss.str (), paths);
	}
    }
  else if (obj.IsObject ())
    {
      for (auto it = obj.MemberBegin (); it != obj.MemberEnd (); ++it)
	{
	  std::stringstream ss;
	  ss << sql_path << '.' << '"' << it->name.GetString () << '"';
	  db_json_get_paths_helper (it->value, ss.str (), paths);
	}
    }

  // add the current result
  paths.push_back (sql_path);
}

/* db_json_get_all_paths_func () - Returns the paths from a JSON document as a JSON array
 *
 * doc (in)                : json document
 * result_json (in)        : a json array that contains all the paths
 */
int
db_json_get_all_paths_func (const JSON_DOC &doc, JSON_DOC *&result_json)
{
  JSON_POINTER p ("");
  const JSON_VALUE *head = p.Get (doc);
  std::vector<std::string> paths;

  // call the helper to get the paths
  db_json_get_paths_helper (*head, "$", paths);

  result_json->SetArray ();

  for (auto it = paths.begin (); it != paths.end (); ++it)
    {
      JSON_VALUE val;
      val.SetString (it->c_str (), result_json->GetAllocator ());
      result_json->PushBack (val, result_json->GetAllocator ());
    }

  return NO_ERROR;
}

/* db_json_keys_func () - Returns the keys from the top-level value of a JSON object as a JSON array
 *
 * return                  : error code
 * doc (in)                : json document
 * result_json (in)        : a json array that contains all the paths
 * raw_path (in)           : specified path
 */
static int
db_json_keys_func (const JSON_DOC &doc, JSON_DOC &result_json, const char *raw_path)
{
  int error_code = NO_ERROR;
  std::string json_pointer_string;

  // path must be JSON pointer
  error_code = db_json_convert_sql_path_to_pointer (raw_path, json_pointer_string);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_POINTER p (json_pointer_string.c_str ());

  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  const JSON_VALUE *head = p.Get (doc);

  // the specified path does not exist in the current JSON document
  if (head == NULL)
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, json_pointer_string, &doc);
    }
  else if (head->IsObject ())
    {
      result_json.SetArray ();

      for (auto it = head->MemberBegin (); it != head->MemberEnd (); ++it)
	{
	  JSON_VALUE val;

	  val.SetString (it->name.GetString(), result_json.GetAllocator ());
	  result_json.PushBack (val, result_json.GetAllocator ());
	}
    }

  return NO_ERROR;
}

int
db_json_keys_func (const JSON_DOC &doc, JSON_DOC *&result_json, const char *raw_path)
{
  assert (result_json == NULL);
  result_json = db_json_allocate_doc();

  return db_json_keys_func (doc, *result_json, raw_path);
}

int
db_json_keys_func (const char *json_raw, JSON_DOC *&result_json, const char *raw_path)
{
  JSON_DOC doc;
  int error_code = NO_ERROR;
  error_code = db_json_get_json_from_str (json_raw, doc);

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  assert (result_json == NULL);
  result_json = db_json_allocate_doc();

  return db_json_keys_func (doc, *result_json, raw_path);
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
  // json pointer will corespond the symbol '-' to JSON_ARRAY length
  // so if we have the json {"A":[1,2,3]} and the path /A/-
  // this will point to the 4th element of the array (zero indexed)
  if (str.compare ("-") == 0)
    {
      return true;
    }

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

int
JSON_WALKER::WalkDocument (JSON_DOC &document)
{
  return WalkValue (db_json_doc_to_value (document));
}

int
JSON_WALKER::WalkValue (JSON_VALUE &value)
{
  int error_code = NO_ERROR;

  error_code = CallBefore (value);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (value.IsObject ())
    {
      for (auto it = value.MemberBegin (); it != value.MemberEnd (); ++it)
	{
	  error_code = WalkValue (it->value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}
    }
  else if (value.IsArray ())
    {
      for (JSON_VALUE *it = value.Begin (); it != value.End (); ++it)
	{
	  error_code = WalkValue (*it);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}
    }

  error_code = CallAfter (value);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}
