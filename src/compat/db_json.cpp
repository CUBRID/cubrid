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

#include "db_json_path.hpp"
#include "db_json_types_internal.hpp"
#include "db_rapidjson.hpp"
#include "dbtype.h"
#include "memory_alloc.h"
#include "memory_private_allocator.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "porting_inline.hpp"
#include "query_dump.h"
#include "string_opfunc.h"
#include "system_parameter.h"

#include <algorithm>
#include <sstream>
#include <stack>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define TODO_OPTIMIZE_JSON_BODY_STRING true


#if TODO_OPTIMIZE_JSON_BODY_STRING
struct JSON_RAW_STRING_DELETER
{
  void operator() (char *p) const
  {
    db_private_free (NULL, p);
  }
};
#endif // TODO_OPTIMIZE_JSON_BODY_STRING

typedef rapidjson::GenericStringBuffer<JSON_ENCODING, JSON_PRIVATE_ALLOCATOR> JSON_STRING_BUFFER;
typedef rapidjson::GenericMemberIterator<true, JSON_ENCODING, JSON_PRIVATE_MEMPOOL>::Iterator JSON_MEMBER_ITERATOR;
typedef rapidjson::GenericArray<true, JSON_VALUE>::ConstValueIterator JSON_VALUE_ITERATOR;

typedef std::function<int (const JSON_VALUE &, const JSON_PATH &, bool &)> map_func_type;

namespace cubmem
{
  template <>
  void JSON_DOC_STORE::create_mutable_reference ()
  {
    set_mutable_reference (db_json_allocate_doc ());
  }

  template <>
  void JSON_DOC_STORE::delete_mutable ()
  {
    delete m_mutable_reference;
  }
}

// class JSON_ITERATOR - virtual interface to wrap array and object iterators
//
class JSON_ITERATOR
{
  public:
    // default ctor
    JSON_ITERATOR ()
      : m_input_doc (nullptr)
      , m_value_doc (nullptr)
    {
    }

    virtual ~JSON_ITERATOR ()
    {
      clear_content ();
    }

    // next iterator
    virtual void next () = 0;
    // does it have more values?
    virtual bool has_next () = 0;
    // get current value
    virtual const JSON_VALUE *get () = 0;
    // set input document
    virtual void set (const JSON_DOC &new_doc) = 0;

    // get a document from current iterator value
    const JSON_DOC *
    get_value_to_doc ()
    {
      const JSON_VALUE *value = get ();

      if (value == nullptr)
	{
	  return nullptr;
	}

      if (m_value_doc == nullptr)
	{
	  m_value_doc = db_json_allocate_doc ();
	}

      m_value_doc->CopyFrom (*value, m_value_doc->GetAllocator ());

      return m_value_doc;
    }

    void reset ()
    {
      m_input_doc = nullptr;            // clear input
    }

    bool is_empty () const
    {
      return m_input_doc == nullptr;    // no input
    }

    // delete only the content of the JSON_ITERATOR for reuse
    void clear_content ()
    {
      if (m_value_doc != nullptr)
	{
	  db_json_delete_doc (m_value_doc);
	}
    }

  protected:
    const JSON_DOC *m_input_doc;      // document being iterated
    JSON_DOC *m_value_doc;            // document that can store iterator "value"
};

// JSON Object iterator - iterates through object members
//
class JSON_OBJECT_ITERATOR : public JSON_ITERATOR
{
  public:
    JSON_OBJECT_ITERATOR ()
      : m_iterator ()
    {
      //
    }

    // advance to next member
    void next () override;
    // has more members
    bool has_next () override;

    // get current member value
    const JSON_VALUE *get () override
    {
      return &m_iterator->value;
    }

    // set input document and initialize iterator on first position
    void set (const JSON_DOC &new_doc) override
    {
      assert (new_doc.IsObject ());

      m_input_doc = &new_doc;
      m_iterator = new_doc.MemberBegin ();
    }

  private:
    JSON_MEMBER_ITERATOR m_iterator;
};

// JSON Array iterator - iterates through elements (values)
//
class JSON_ARRAY_ITERATOR : public JSON_ITERATOR
{
  public:
    JSON_ARRAY_ITERATOR ()
      : m_iterator ()
    {
      //
    }

    // next element
    void next () override;
    // has more elements
    bool has_next () override;

    const JSON_VALUE *get () override
    {
      return m_iterator;
    }

    void set (const JSON_DOC &new_doc) override
    {
      assert (new_doc.IsArray ());

      m_input_doc = &new_doc;
      m_iterator = new_doc.GetArray ().Begin ();
    }

  private:
    JSON_VALUE_ITERATOR m_iterator;
};

void
JSON_ARRAY_ITERATOR::next ()
{
  assert (has_next ());
  m_iterator++;
}

bool
JSON_ARRAY_ITERATOR::has_next ()
{
  if (m_input_doc == nullptr)
    {
      return false;
    }

  JSON_VALUE_ITERATOR end = m_input_doc->GetArray ().End ();

  return (m_iterator + 1) != end;
}

void
JSON_OBJECT_ITERATOR::next ()
{
  assert (has_next ());
  m_iterator++;
}

bool
JSON_OBJECT_ITERATOR::has_next ()
{
  if (m_input_doc == nullptr)
    {
      return false;
    }

  JSON_MEMBER_ITERATOR end = m_input_doc->MemberEnd ();

  return (m_iterator + 1) != end;
}

class JSON_VALIDATOR
{
  public:
    explicit JSON_VALIDATOR (const char *schema_raw);
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
    JSON_BASE_HANDLER () = default;
    virtual ~JSON_BASE_HANDLER () = default;
    typedef typename JSON_DOC::Ch Ch;
    typedef unsigned SizeType;

    virtual bool Null ()
    {
      return true;
    }
    virtual bool Bool (bool b)
    {
      return true;
    }
    virtual bool Int (int i)
    {
      return true;
    }
    virtual bool Uint (unsigned i)
    {
      return true;
    }
    virtual bool Int64 (std::int64_t i)
    {
      return true;
    }
    virtual bool Uint64 (std::uint64_t i)
    {
      return true;
    }
    virtual bool Double (double d)
    {
      return true;
    }
    virtual bool RawNumber (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    virtual bool String (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    virtual bool StartObject ()
    {
      return true;
    }
    virtual bool Key (const Ch *str, SizeType length, bool copy)
    {
      return true;
    }
    virtual bool EndObject (SizeType memberCount)
    {
      return true;
    }
    virtual bool StartArray ()
    {
      return true;
    }
    virtual bool EndArray (SizeType elementCount)
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
    int WalkDocument (const JSON_DOC &document);

  protected:
    // we should not instantiate this class, but extend it
    virtual ~JSON_WALKER () = default;

    virtual int
    CallBefore (const JSON_VALUE &value)
    {
      // do nothing
      return NO_ERROR;
    }

    virtual int
    CallAfter (const JSON_VALUE &value)
    {
      // do nothing
      return NO_ERROR;
    }

    virtual int
    CallOnArrayIterate ()
    {
      // do nothing
      return NO_ERROR;
    }

    virtual int
    CallOnKeyIterate (const JSON_VALUE &key)
    {
      // do nothing
      return NO_ERROR;
    }

  private:
    int WalkValue (const JSON_VALUE &value);

  protected:
    bool m_stop;
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
    JSON_DUPLICATE_KEYS_CHECKER () = default;
    ~JSON_DUPLICATE_KEYS_CHECKER () override = default;

  private:
    int CallBefore (const JSON_VALUE &value) override;
};

class JSON_PATH_MAPPER : public JSON_WALKER
{
  public:
    JSON_PATH_MAPPER (map_func_type func);
    JSON_PATH_MAPPER (JSON_PATH_MAPPER &) = delete;
    ~JSON_PATH_MAPPER () override = default;

  private:
    int CallBefore (const JSON_VALUE &value) override;
    int CallAfter (const JSON_VALUE &value) override;
    int CallOnArrayIterate () override;
    int CallOnKeyIterate (const JSON_VALUE &key) override;

    map_func_type m_producer;
    std::stack<unsigned int> m_index;
    JSON_PATH m_current_path;
};

class JSON_SERIALIZER_LENGTH : public JSON_BASE_HANDLER
{
  public:
    JSON_SERIALIZER_LENGTH ()
      : m_length (0)
    {
      //
    }

    ~JSON_SERIALIZER_LENGTH () override = default;

    std::size_t GetLength () const
    {
      return m_length;
    }

    std::size_t GetTypePackedSize (void) const
    {
      return OR_INT_SIZE;
    }

    std::size_t GetStringPackedSize (const char *str) const
    {
      return or_packed_string_length (str, NULL);
    }

    bool Null () override;
    bool Bool (bool b) override;
    bool Int (int i) override;
    bool Uint (unsigned i) override;
    bool Int64 (std::int64_t i) override;
    bool Uint64 (std::uint64_t i) override;
    bool Double (double d) override;
    bool String (const Ch *str, SizeType length, bool copy) override;
    bool StartObject () override;
    bool Key (const Ch *str, SizeType length, bool copy) override;
    bool StartArray () override;
    bool EndObject (SizeType memberCount) override;
    bool EndArray (SizeType elementCount) override;

  private:
    std::size_t m_length;
};

class JSON_SERIALIZER : public JSON_BASE_HANDLER
{
  public:
    explicit JSON_SERIALIZER (OR_BUF &buffer)
      : m_error (NO_ERROR)
      , m_buffer (&buffer)
      , m_size_pointers ()
    {
      //
    }

    ~JSON_SERIALIZER () override = default;

    bool Null () override;
    bool Bool (bool b) override;
    bool Int (int i) override;
    bool Uint (unsigned i) override;
    bool Int64 (std::int64_t i) override;
    bool Uint64 (std::uint64_t i) override;
    bool Double (double d) override;
    bool String (const Ch *str, SizeType length, bool copy) override;
    bool StartObject () override;
    bool Key (const Ch *str, SizeType length, bool copy) override;
    bool StartArray () override;
    bool EndObject (SizeType memberCount) override;
    bool EndArray (SizeType elementCount) override;

  private:
    bool SaveSizePointers (char *ptr);
    void SetSizePointers (SizeType size);

    bool PackType (const DB_JSON_TYPE &type);
    bool PackString (const char *str);

    bool HasError ()
    {
      return m_error != NO_ERROR;
    }

    int m_error;                            // internal error code
    OR_BUF *m_buffer;                       // buffer to serialize to
    std::stack<char *> m_size_pointers;     // stack used by nested arrays & objects to save starting pointer.
    // member/element count is saved at the end
};

/*
 * JSON_PRETTY_WRITER - This class extends JSON_BASE_HANDLER
 *
 * The JSON document accepts the Handler and walks the document with respect to the DB_JSON_TYPE.
 * The context is kept in the m_level_iterable stack which contains the value from the current level, which
 * can be ARRAY, OBJECT or SCALAR. In case we are in an iterable (ARRAY/OBJECT) we need to keep track if of the first
 * element because it's important for printing the delimiters.
 *
 * The formatting output respects the following rules:
 * - Each array element or object member appears on a separate line, indented by one additional level as
 *   compared to its parent
 * - Each level of indentation adds two leading spaces
 * - A comma separating individual array elements or object members is printed before the newline that
 *   separates the two elements or members
 * - The key and the value of an object member are separated by a colon followed by a space (': ')
 * - An empty object or array is printed on a single line. No space is printed between the opening and closing brace
 */
class JSON_PRETTY_WRITER : public JSON_BASE_HANDLER
{
  public:
    JSON_PRETTY_WRITER ()
      : m_buffer ()
      , m_current_indent (0)
    {
      // default ctor
    }

    ~JSON_PRETTY_WRITER () override = default;

    bool Null () override;
    bool Bool (bool b) override;
    bool Int (int i) override;
    bool Uint (unsigned i) override;
    bool Int64 (std::int64_t i) override;
    bool Uint64 (std::uint64_t i) override;
    bool Double (double d) override;
    bool String (const Ch *str, SizeType length, bool copy) override;
    bool StartObject () override;
    bool Key (const Ch *str, SizeType length, bool copy) override;
    bool StartArray () override;
    bool EndObject (SizeType memberCount) override;
    bool EndArray (SizeType elementCount) override;

    std::string &ToString ()
    {
      return m_buffer;
    }

  private:
    void WriteDelimiters (bool is_key = false);
    void PushLevel (const DB_JSON_TYPE &type);
    void PopLevel ();
    void SetIndentOnNewLine ();

    struct level_context
    {
      DB_JSON_TYPE type;
      bool is_first;

      level_context (DB_JSON_TYPE type, bool is_first)
	: type (type)
	, is_first (is_first)
      {
	//
      }
    };

    std::string m_buffer;                         // the buffer that stores the json
    size_t m_current_indent;                      // number of white spaces for the current level
    static const size_t LEVEL_INDENT_UNIT = 2;    // number of white spaces of indent level
    std::stack<level_context> m_level_stack;      // keep track of the current iterable (ARRAY/OBJECT)
};

const int JSON_DOC::MAX_CHUNK_SIZE = 64 * 1024; /* TODO does 64K serve our needs? */

static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);
static int db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result);
static bool db_json_value_has_numeric_type (const JSON_VALUE *doc);
DB_JSON_TYPE db_json_get_type_of_value (const JSON_VALUE *val);
static int db_json_get_int_from_value (const JSON_VALUE *val);
static std::int64_t db_json_get_bigint_from_value (const JSON_VALUE *val);
static double db_json_get_double_from_value (const JSON_VALUE *doc);
static const char *db_json_get_string_from_value (const JSON_VALUE *doc);
static char *db_json_copy_string_from_value (const JSON_VALUE *doc);
static char *db_json_get_bool_as_str_from_value (const JSON_VALUE *doc);
static bool db_json_get_bool_from_value (const JSON_VALUE *doc);
static char *db_json_bool_to_string (bool b);
static void db_json_array_push_back (const JSON_VALUE &value, JSON_VALUE &dest_array,
				     JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_object_add_member (const JSON_VALUE &name, const JSON_VALUE &value, JSON_VALUE &dest_object,
				       JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_merge_two_json_objects_preserve (const JSON_VALUE &source, JSON_VALUE &dest,
    JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_merge_two_json_objects_patch (const JSON_VALUE &source, JSON_VALUE &dest,
    JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_merge_two_json_array_values (const JSON_VALUE &source, JSON_VALUE &dest,
    JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_merge_preserve_values (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator);
static void db_json_merge_patch_values (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator);

static void db_json_copy_doc (JSON_DOC &dest, const JSON_DOC *src);
static void db_json_get_paths_helper (const JSON_VALUE &obj, const std::string &sql_path,
				      std::vector<std::string> &paths);

static void db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator);
static const char *db_json_get_json_type_as_str (const DB_JSON_TYPE &json_type);
static int db_json_er_set_path_does_not_exist (const char *file_name, const int line_no, const JSON_PATH &path,
    const JSON_DOC *doc);
static int db_json_er_set_expected_other_type (const char *file_name, const int line_no, const JSON_PATH &path,
    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type,
    const DB_JSON_TYPE &expected_type_optional = DB_JSON_NULL);
static int db_json_contains_duplicate_keys (const JSON_DOC &doc);

static int db_json_get_json_from_str (const char *json_raw, JSON_DOC &doc, size_t json_raw_length);
static int db_json_add_json_value_to_object (JSON_DOC &doc, const char *name, JSON_VALUE &value);

static int db_json_deserialize_doc_internal (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator);

static int db_json_or_buf_underflow (OR_BUF *buf, size_t length);
static int db_json_unpack_string_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator);
static int db_json_unpack_int_to_value (OR_BUF *buf, JSON_VALUE &value);
static int db_json_unpack_bigint_to_value (OR_BUF *buf, JSON_VALUE &value);
static int db_json_unpack_bool_to_value (OR_BUF *buf, JSON_VALUE &value);
static int db_json_unpack_object_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator);
static int db_json_unpack_array_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator);

static void db_json_add_element_to_array (JSON_DOC *doc, const JSON_VALUE *value);

int
JSON_DUPLICATE_KEYS_CHECKER::CallBefore (const JSON_VALUE &value)
{
  std::vector<const char *> inserted_keys;

  if (value.IsObject ())
    {
      for (auto it = value.MemberBegin (); it != value.MemberEnd (); ++it)
	{
	  const char *current_key = it->name.GetString ();

	  for (unsigned int i = 0; i < inserted_keys.size (); i++)
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

JSON_PATH_MAPPER::JSON_PATH_MAPPER (map_func_type func)
  : m_producer (func)
  , m_current_path ()
{

}

int
JSON_PATH_MAPPER::CallBefore (const JSON_VALUE &value)
{
  if (value.IsArray ())
    {
      m_index.push (0);
    }

  if (value.IsObject () || value.IsArray ())
    {
      // should not be used before it gets changed. Only add a stack level
      // dummy
      m_current_path.push_array_index (0);
    }

  return NO_ERROR;
}

int
JSON_PATH_MAPPER::CallAfter (const JSON_VALUE &value)
{
  if (value.IsArray ())
    {
      m_index.pop ();
    }

  if (value.IsArray () || value.IsObject ())
    {
      m_current_path.pop ();
    }
  return m_producer (value, m_current_path, m_stop);
}

int
JSON_PATH_MAPPER::CallOnArrayIterate ()
{
  // todo: instead of pop + push, increment the last token
  m_current_path.pop ();

  m_current_path.push_array_index (m_index.top ()++);

  return NO_ERROR;
}

int
JSON_PATH_MAPPER::CallOnKeyIterate (const JSON_VALUE &key)
{
  m_current_path.pop ();
  std::string path_item = "\"";

  std::string object_key = key.GetString ();
  for (auto it = object_key.begin (); it != object_key.end (); ++it)
    {
      // todo: take care of all chars that need escaping during simple object key -> object key conversion
      if (*it == '"' || *it == '\\')
	{
	  it = object_key.insert (it, '\\') + 1;
	}
    }
  path_item += object_key;
  path_item += "\"";

  m_current_path.push_object_key (std::move (path_item));
  return NO_ERROR;
}

JSON_VALIDATOR::JSON_VALIDATOR (const char *schema_raw)
  : m_schema (NULL),
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
      m_schema = NULL;
    }

  if (m_validator != NULL)
    {
      delete m_validator;
      m_validator = NULL;
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

void
db_json_iterator_next (JSON_ITERATOR &json_itr)
{
  json_itr.next ();
}

const JSON_DOC *
db_json_iterator_get_document (JSON_ITERATOR &json_itr)
{
  return json_itr.get_value_to_doc ();
}

bool
db_json_iterator_has_next (JSON_ITERATOR &json_itr)
{
  return json_itr.has_next ();
}

void
db_json_set_iterator (JSON_ITERATOR *&json_itr, const JSON_DOC &new_doc)
{
  json_itr->set (new_doc);
}

void
db_json_reset_iterator (JSON_ITERATOR *&json_itr)
{
  if (json_itr != NULL)
    {
      json_itr->reset ();
    }
}

bool
db_json_iterator_is_empty (const JSON_ITERATOR &json_itr)
{
  return json_itr.is_empty ();
}

JSON_ITERATOR *
db_json_create_iterator (const DB_JSON_TYPE &type)
{
  if (type == DB_JSON_TYPE::DB_JSON_OBJECT)
    {
      return new JSON_OBJECT_ITERATOR ();
    }
  else if (type == DB_JSON_TYPE::DB_JSON_ARRAY)
    {
      return new JSON_ARRAY_ITERATOR ();
    }

  return NULL;
}

void
db_json_delete_json_iterator (JSON_ITERATOR *&json_itr)
{
  delete json_itr;
  json_itr = NULL;
}

void
db_json_clear_json_iterator (JSON_ITERATOR *&json_itr)
{
  if (json_itr != NULL)
    {
      json_itr->clear_content ();
    }
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

  const JSON_VALUE *to_valuep = &db_json_doc_to_value (*document);
  return db_json_get_json_type_as_str (db_json_get_type_of_value (to_valuep));
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
    case DB_JSON_BIGINT:
      return "BIGINT";
    case DB_JSON_DOUBLE:
      return "DOUBLE";
    case DB_JSON_STRING:
      return "STRING";
    case DB_JSON_NULL:
      return "JSON_NULL";
    case DB_JSON_BOOL:
      return "BOOLEAN";
    default:
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
      unsigned int length = 0;

      for (JSON_VALUE::ConstMemberIterator itr = document->MemberBegin (); itr != document->MemberEnd (); ++itr)
	{
	  length++;
	}

      return length;
    }

  return 0;
}

/*
 * json_depth ()
 * one array or one object increases the depth by 1
 */

unsigned int
db_json_get_depth (const JSON_DOC *doc)
{
  return db_json_value_get_depth (doc);
}

/*
 * db_json_unquote ()
 * skip escaping for JSON_DOC strings
 */

int
db_json_unquote (const JSON_DOC &doc, char *&result_str)
{
  assert (result_str == nullptr);

  if (!doc.IsString ())
    {
      result_str = db_json_get_raw_json_body_from_document (&doc);
    }
  else
    {
      result_str = db_private_strdup (NULL, doc.GetString ());

      if (result_str == nullptr)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  return NO_ERROR;
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

int
db_json_extract_document_from_path (const JSON_DOC *document, const std::string &path,
				    JSON_DOC_STORE &result, bool allow_wildcards)
{
  return db_json_extract_document_from_path (document, std::vector<std::string> { path }, result, allow_wildcards);
}

/*
 * db_json_extract_document_from_path () - Extracts from within the json a value based on the given path
 *
 * return                  : error code
 * doc_to_be_inserted (in) : document to be inserted
 * doc_destination (in)    : destination document
 * paths (in)              : paths from where to extract
 * result (out)            : resulting doc
 * allow_wildcards         :
 * example                 : json_extract('{"a":["b", 123]}', '/a/1') yields 123
 */
int
db_json_extract_document_from_path (const JSON_DOC *document, const std::vector<std::string> &paths,
				    JSON_DOC_STORE &result, bool allow_wildcards)
{
  int error_code = NO_ERROR;

  if (document == NULL)
    {
      if (result.is_mutable ())
	{
	  result.get_mutable ()->SetNull ();
	}
      return NO_ERROR;
    }

  std::vector<JSON_PATH> json_paths;

  for (const std::string &path : paths)
    {
      json_paths.emplace_back ();
      error_code = json_paths.back ().parse (path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      if (!allow_wildcards && json_paths.back ().contains_wildcard ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
    }

  // wrap result in json array in case we have multiple paths or we have a json_path containing wildcards
  bool array_result = false;
  if (json_paths.size () > 1)
    {
      array_result = true;
    }
  else
    {
      array_result = json_paths[0].contains_wildcard ();
    }

  std::vector<std::vector<const JSON_VALUE *>> produced_array (json_paths.size ());
  for (size_t i = 0; i < json_paths.size (); ++i)
    {
      produced_array[i] = std::move (json_paths[i].extract (*document));
    }

  if (array_result)
    {
      for (const auto &produced : produced_array)
	{
	  for (const JSON_VALUE *p : produced)
	    {
	      if (!result.is_mutable ())
		{
		  result.create_mutable_reference ();
		  result.get_mutable ()->SetArray ();
		}

	      db_json_add_element_to_array (result.get_mutable (), p);
	    }
	}
    }
  else
    {
      assert (produced_array.size () == 1 && (produced_array[0].empty () || produced_array[0].size () == 1));

      if (!produced_array[0].empty ())
	{
	  if (!result.is_mutable ())
	    {
	      result.create_mutable_reference ();
	    }

	  result.get_mutable ()->CopyFrom (*produced_array[0][0], result.get_mutable ()->GetAllocator ());
	}
    }

  return NO_ERROR;
}

/*
 * db_json_contains_path () - Checks if the document contains data at given path
 *
 * return                  : error code
 * document (in)           : document where to search
 * paths (in)              : paths
 * find_all (in)           : whether the document needs to contain all paths
 * result (out)            : true/false
 */
int
db_json_contains_path (const JSON_DOC *document, const std::vector<std::string> &paths, bool find_all, bool &result)
{
  int error_code = NO_ERROR;
  if (document == NULL)
    {
      return false;
    }

  std::vector<JSON_PATH> json_paths;
  for (const auto &path : paths)
    {
      json_paths.emplace_back ();
      error_code = json_paths.back ().parse (path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  bool contains_wildcard = false;
  for (const JSON_PATH &json_path : json_paths)
    {
      contains_wildcard = contains_wildcard || json_path.contains_wildcard ();
    }

  if (!contains_wildcard)
    {
      for (const JSON_PATH &json_path : json_paths)
	{
	  const JSON_VALUE *found = json_path.get (*document);
	  if (find_all && found == NULL)
	    {
	      result = false;
	      return NO_ERROR;
	    }
	  if (!find_all && found != NULL)
	    {
	      result = true;
	      return NO_ERROR;
	    }
	}
      result = find_all;
      return NO_ERROR;
    }

  std::unique_ptr<bool[]> found_set (new bool[paths.size ()]);
  for (std::size_t i = 0; i < paths.size (); ++i)
    {
      found_set[i] = false;
    }

  const map_func_type &f_find = [&json_paths, &found_set, find_all] (const JSON_VALUE &v,
				const JSON_PATH &accumulated_path, bool &stop) -> int
  {
    for (std::size_t i = 0; i < json_paths.size (); ++i)
      {
	if (!found_set[i] && JSON_PATH::match_pattern (json_paths[i], accumulated_path) == JSON_PATH::MATCH_RESULT::FULL_MATCH)
	  {
	    found_set[i] = true;
	    if (!find_all)
	      {
		stop = true;
		return NO_ERROR;
	      }
	  }
      }
    return NO_ERROR;
  };

  JSON_PATH_MAPPER json_contains_path_walker (f_find);
  json_contains_path_walker.WalkDocument (*document);

  for (std::size_t i = 0; i < paths.size (); ++i)
    {
      if (find_all && !found_set[i])
	{
	  result = false;
	  return NO_ERROR;
	}
      if (!find_all && found_set[i])
	{
	  result = true;
	  return NO_ERROR;
	}
    }

  result = find_all;
  return NO_ERROR;
}

char *
db_json_get_raw_json_body_from_document (const JSON_DOC *doc)
{
  JSON_STRING_BUFFER buffer;
  rapidjson::Writer<JSON_STRING_BUFFER> json_default_writer (buffer);

  buffer.Clear ();

  doc->Accept (json_default_writer);

  return db_private_strdup (NULL, buffer.GetString ());
}

char *
db_json_get_json_body_from_document (const JSON_DOC &doc)
{
#if TODO_OPTIMIZE_JSON_BODY_STRING
  /* TODO
  std::string json_body (std::unique_ptr<char, JSON_RAW_STRING_DELETER>
  		 (db_json_get_raw_json_body_from_document (&doc), JSON_RAW_STRING_DELETER ()).get ());

  doc.SetJsonBody (json_body);
  return doc.GetJsonBody ().c_str ();
  */
#endif // TODO_OPTIMIZE_JSON_BODY_STRING

  return db_json_get_raw_json_body_from_document (&doc);
}

static int
db_json_add_json_value_to_object (JSON_DOC &doc, const char *name, JSON_VALUE &value)
{
  JSON_VALUE key;

  if (!doc.IsObject ())
    {
      doc.SetObject ();
    }

  if (doc.HasMember (name))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_DUPLICATE_KEY, 1, name);
      return ER_JSON_DUPLICATE_KEY;
    }

  key.SetString (name, (rapidjson::SizeType) strlen (name), doc.GetAllocator ());
  doc.AddMember (key, value, doc.GetAllocator ());

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
db_json_add_member_to_object (JSON_DOC *doc, const char *name, std::int64_t value)
{
  JSON_VALUE val;

  val.SetInt64 (value);

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
db_json_add_element_to_array (JSON_DOC *doc, std::int64_t value)
{
  if (!doc->IsArray ())
    {
      doc->SetArray ();
    }

  doc->PushBack (JSON_VALUE ().SetInt64 (value), doc->GetAllocator ());
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

static void
db_json_add_element_to_array (JSON_DOC *doc, const JSON_VALUE *value)
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
db_json_contains_duplicate_keys (const JSON_DOC &doc)
{
  JSON_DUPLICATE_KEYS_CHECKER dup_keys_checker;
  int error_code = NO_ERROR;

  // check recursively for duplicate keys in the json document
  error_code = dup_keys_checker.WalkDocument (doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }

  return error_code;
}

static int
db_json_get_json_from_str (const char *json_raw, JSON_DOC &doc, size_t json_raw_length)
{
  int error_code = NO_ERROR;

  if (json_raw == NULL)
    {
      return NO_ERROR;
    }

  if (doc.Parse (json_raw, json_raw_length).HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (doc.GetParseError ()), doc.GetErrorOffset ());
      return ER_JSON_INVALID_JSON;
    }

  error_code = db_json_contains_duplicate_keys (doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

int
db_json_get_json_from_str (const char *json_raw, JSON_DOC *&doc, size_t json_raw_length)
{
  int err;

  assert (doc == NULL);

  doc = db_json_allocate_doc ();

  err = db_json_get_json_from_str (json_raw, *doc, json_raw_length);
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

#if TODO_OPTIMIZE_JSON_BODY_STRING
  /* TODO
  new_doc->SetJsonBody (doc->GetJsonBody ());
  */
#endif // TODO_OPTIMIZE_JSON_BODY_STRING

  return new_doc;
}

static void
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
 * db_json_insert_func () - Insert a value into the destination document at the given path or at the path ignoring a trailing
 *                          0 array index (if insert at full path would fail). JSON_INSERT results in no-op if a value is found
 *
 * return                  : error code
 * value (in)              : document to be inserted
 * doc (in)                : destination document
 * raw_path (in)           : insertion path
 */
int
db_json_insert_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (p.is_root_path ())
    {
      return NO_ERROR;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  JSON_VALUE *parent_val = p.get_parent ().get (doc);

  if (p.points_to_array_cell ())
    {
      if (p.is_last_token_array_index_zero () && db_json_get_type_of_value (parent_val) != DB_JSON_ARRAY)
	{
	  // we ignore a trailing 0 array index. We found a value => no op
	  return NO_ERROR;
	}
      else
	{
	  db_json_value_wrap_as_array (*parent_val, doc.GetAllocator ());
	  if (p.is_last_array_index_less_than (parent_val->GetArray ().Size ()))
	    {
	      return NO_ERROR;
	    }
	  else
	    {
	      p.set (doc, *value);
	    }
	}
    }
  else
    {
      if (db_json_get_type_of_value (parent_val) != DB_JSON_OBJECT)
	{
	  return db_json_er_set_expected_other_type (ARG_FILE_LINE, p.get_parent (),
		 db_json_get_type_of_value (p.get (doc)), DB_JSON_OBJECT);
	}
      if (p.get (doc) != NULL)
	{
	  return NO_ERROR;
	}
      else
	{
	  p.set (doc, *value);
	}
    }
  return NO_ERROR;
}

/*
 * db_json_replace_func () - Replaces the value from the specified path or at the path ignoring a trailing 0 array index
 *                           (if the full path does not exist) in a JSON document with a new value.
 *
 * return                  : error code
 * value (in)          : the value to be set at the specified path
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_replace_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (p.is_root_path ())
    {
      p.set (doc, *value);
      return NO_ERROR;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  JSON_VALUE *parent_val = p.get_parent ().get (doc);

  if (p.points_to_array_cell ())
    {
      if (p.is_last_token_array_index_zero ())
	{
	  if (!parent_val->IsArray ())
	    {
	      // we ignore a trailing 0 array index token and we replace what we found
	      parent_val->CopyFrom (*value, doc.GetAllocator ());
	    }
	  else if (parent_val->GetArray ().Size () > 0 /* check array is not empty */)
	    {
	      p.set (doc, *value);
	    }
	  else
	    {
	      // no_op if array is empty
	      return NO_ERROR;
	    }
	}
      else
	{
	  if (parent_val->IsArray () && p.is_last_array_index_less_than (parent_val->GetArray ().Size ()))
	    {
	      p.set (doc, *value);
	    }
	  else
	    {
	      // no_op
	      return NO_ERROR;
	    }
	}
    }
  else
    {
      if (!parent_val->IsObject ())
	{
	  return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
	}
      else
	{
	  if (p.get (doc) != NULL)
	    {
	      p.set (doc, *value);
	    }
	  else
	    {
	      // no_op if name does not exist in the object
	      return NO_ERROR;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * db_json_set_func () - Inserts or updates data in a JSON document at a specified path or at the path ignoring a trailing 0 array index
 *                       (if the full path does not exist) in a JSON document with a new value.
 *
 * return                  : error code
 * value (in)              : the value to be set at the specified path
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_set_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // test if exists for now
  if (p.get (doc) != NULL)
    {
      p.set (doc, *value);
      return NO_ERROR;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  JSON_VALUE *parent_val = p.get_parent ().get (doc);

  if (p.points_to_array_cell ())
    {
      if (p.is_last_token_array_index_zero ())
	{
	  if (db_json_get_type_of_value (parent_val) == DB_JSON_ARRAY)
	    {
	      p.set (doc, *value);
	    }
	  else
	    {
	      // we ignore a trailing 0 array index and we replace what we found
	      p.get_parent ().set (doc, *value);
	    }
	}
      else
	{
	  db_json_value_wrap_as_array (*parent_val, doc.GetAllocator ());
	  p.set (doc, *value);
	}
    }
  else
    {
      if (db_json_get_type_of_value (parent_val) != DB_JSON_OBJECT)
	{
	  return db_json_er_set_expected_other_type (ARG_FILE_LINE, p.get_parent (),
		 db_json_get_type_of_value (p.get (doc)), DB_JSON_OBJECT);
	}
      p.set (doc, *value);
    }

  return NO_ERROR;
}

/*
 * db_json_remove_func () - Removes data from a JSON document at the specified path or at the path ignoring a trailing 0 array index
 *                          (if the full path does not exist)
 *
 * return                  : error code
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_remove_func (JSON_DOC &doc, const char *raw_path)
{
  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (p.is_root_path ())
    {
      // json_remove on root is invalid
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  if (p.get (doc) == NULL)
    {
      if (!p.is_last_token_array_index_zero () || p.get_parent ().is_root_path ())
	{
	  return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
	}

      p.get_parent ().erase (doc);
      return NO_ERROR;
    }

  p.erase (doc);

  return NO_ERROR;
}

/*
 * db_json_search_func () - Find json values that match the pattern and gather their paths
 *
 * return                  : error code
 * doc (in)                : json document
 * pattern (in)            : pattern to match against
 * esc_char (in)           : escape sequence used to match the pattern
 * paths (out)             : full paths found
 * patterns (in)           : patterns we match against
 * find_all (in)           : whether we need to gather all matches
 */
int
db_json_search_func (const JSON_DOC &doc, const DB_VALUE *pattern, const DB_VALUE *esc_char,
		     std::vector<JSON_PATH> &paths, const std::vector<std::string> &patterns, bool find_all)
{
  std::vector<JSON_PATH> json_paths;
  for (const auto &path : patterns)
    {
      json_paths.emplace_back ();
      int error_code = json_paths.back ().parse (path.c_str ());
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  std::string raw_json_string = db_get_string (pattern);

  char *quoted_str;
  size_t quoted_sz;
  db_string_escape_str (raw_json_string.c_str (), raw_json_string.length (), &quoted_str, &quoted_sz);
  raw_json_string = quoted_str;
  db_private_free (NULL, quoted_str);

  const std::string encoded_pattern = db_json_json_string_as_utf8 (raw_json_string);

  DB_VALUE encoded_pattern_dbval;
  db_make_string (&encoded_pattern_dbval, encoded_pattern.c_str ());
  db_string_put_cs_and_collation (&encoded_pattern_dbval, INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);

  const map_func_type &f_search = [&json_paths, &paths, &encoded_pattern_dbval, esc_char, find_all] (const JSON_VALUE &jv,
				  const JSON_PATH &crt_path, bool &stop) -> int
  {
    if (!jv.IsString ())
      {
	return NO_ERROR;
      }

    const char *json_str = jv.GetString ();
    DB_VALUE str_val;

    db_make_string (&str_val, json_str);
    db_string_put_cs_and_collation (&str_val, INTL_CODESET_UTF8, LANG_COLL_UTF8_BINARY);

    int match;
    int error_code = db_string_like (&str_val, &encoded_pattern_dbval, esc_char, &match);
    if (error_code != NO_ERROR || !match)
      {
	return error_code;
      }

    for (std::size_t i = 0; i < json_paths.size (); ++i)
      {
	JSON_PATH::MATCH_RESULT res = JSON_PATH::match_pattern (json_paths[i], crt_path);

	if (res == JSON_PATH::MATCH_RESULT::PREFIX_MATCH || res == JSON_PATH::MATCH_RESULT::FULL_MATCH)
	  {
	    paths.push_back (crt_path);
	    if (!find_all)
	      {
		stop = true;
	      }
	    return NO_ERROR;
	  }
      }

    return NO_ERROR;
  };

  JSON_PATH_MAPPER json_search_walker (f_search);
  return json_search_walker.WalkDocument (doc);
}

/*
 * db_json_array_append_func () - In a given JSON document, append the value to the end of the array indicated by the path
 *                                or at the path ignoring a trailing 0 array index (if appending at full path would fail)
 *
 * return                  : error code
 * value (in)              : the value to be added in the array
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_array_append_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_VALUE value_copy (*value, doc.GetAllocator());
  JSON_VALUE *json_val = p.get (doc);

  if (p.is_root_path ())
    {
      db_json_value_wrap_as_array (*json_val, doc.GetAllocator ());
      json_val->GetArray ().PushBack (value_copy, doc.GetAllocator ());
      return NO_ERROR;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  JSON_VALUE *parent_val = p.get_parent ().get (doc);

  if (p.points_to_array_cell ())
    {
      if (db_json_get_type_of_value (parent_val) == DB_JSON_ARRAY)
	{
	  if (!p.is_last_array_index_less_than (parent_val->GetArray ().Size ()))
	    {
	      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
	    }

	  assert (json_val != NULL);
	  db_json_value_wrap_as_array (*json_val, doc.GetAllocator ());
	  json_val->GetArray ().PushBack (value_copy, doc.GetAllocator ());
	}
      else
	{
	  if (!p.is_last_token_array_index_zero ())
	    {
	      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
	    }

	  db_json_value_wrap_as_array (*parent_val, doc.GetAllocator ());
	  parent_val->GetArray ().PushBack (value_copy, doc.GetAllocator ());
	}
    }
  else
    {
      // only valid case is when path exists and its parent is a json object
      if (db_json_get_type_of_value (parent_val) != DB_JSON_OBJECT || json_val == NULL)
	{
	  return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
	}

      db_json_value_wrap_as_array (*json_val, doc.GetAllocator ());
      json_val->GetArray ().PushBack (value_copy, doc.GetAllocator ());
    }
  return NO_ERROR;
}

/*
 * db_json_array_insert_func () - In a given JSON document, Insert the given value in the array at the path
 *                                or at the path ignoring a trailing 0 array index (if insert at full path would fail)
 *
 * return                  : error code
 * value (in)              : the value to be added in the array
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_array_insert_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
{
  if (value == NULL)
    {
      // unexpected
      assert (false);
      return ER_FAILED;
    }

  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (!p.points_to_array_cell ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_PATH_IS_NOT_ARRAY_CELL, 1, p.dump_json_path ().c_str ());
      return ER_JSON_PATH_IS_NOT_ARRAY_CELL;
    }

  if (!p.parent_exists (doc))
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }

  db_json_value_wrap_as_array (*p.get_parent ().get (doc), doc.GetAllocator ());

  const JSON_PATH parent_path = p.get_parent ();
  JSON_VALUE *json_parent = parent_path.get (doc);

  if (!p.is_last_array_index_less_than (json_parent->GetArray ().Size ()))
    {
      p.set (doc, *value);
      return NO_ERROR;
    }

  json_parent->GetArray ().PushBack (JSON_VALUE (), doc.GetAllocator ());
  size_t last_token_idx = p.get_last_token ()->get_array_index ();
  for (rapidjson::SizeType i = json_parent->GetArray ().Size () - 1; i >= last_token_idx + 1; --i)
    {
      json_parent->GetArray ()[i] = std::move (json_parent->GetArray ()[i - 1]);
    }
  p.set (doc, *value);

  return NO_ERROR;
}

/*
 * db_json_merge_two_json_objects_patch () - Merge the source object into the destination object handling duplicate
 *                                           keys
 *
 * return                  : error code
 * dest (in)               : json where to merge
 * source (in)             : json to merge
 * example                 : let dest = '{"a" : "b"}'
 *                           let source = '{"a" : 3}'
 *                           after JSON_MERGE_PATCH (dest, source), dest = {"a" : 3}
 */
void
db_json_merge_two_json_objects_patch (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator)
{
  assert (dest.IsObject () && source.IsObject ());

  // iterate through each member from the source json and insert it into the dest
  for (JSON_VALUE::ConstMemberIterator itr = source.MemberBegin (); itr != source.MemberEnd (); ++itr)
    {
      const char *name = itr->name.GetString ();

      // if the key is in both jsons
      if (dest.HasMember (name))
	{
	  // if the second argument value is DB_JSON_NULL, remove that member
	  if (itr->value.IsNull ())
	    {
	      dest.RemoveMember (name);
	    }
	  else
	    {
	      // recursively merge_patch with the current values from both JSON_OBJECTs
	      db_json_merge_patch_values (itr->value, dest[name], allocator);
	    }
	}
      else if (!itr->value.IsNull ())
	{
	  db_json_object_add_member (itr->name, itr->value, dest, allocator);
	}
    }
}

/*
 * db_json_merge_two_json_objects_preserve () - Merge the source object into the destination object,
 *                                              preserving duplicate keys (adding their values in a JSON_ARRAY)
 *
 * return                  : error code
 * dest (in)               : json where to merge
 * source (in)             : json to merge
 * patch (in)              : (true/false) preserve or not the duplicate keys
 * example                 : let dest = '{"a" : "b"}'
 *                           let source = '{"c" : "d"}'
 *                           after JSON_MERGE (dest, source), dest = {"a" : "b", "c" : "d"}
 */
void
db_json_merge_two_json_objects_preserve (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator)
{
  assert (dest.IsObject () && source.IsObject ());

  // iterate through each member from the source json and insert it into the dest
  for (JSON_VALUE::ConstMemberIterator itr = source.MemberBegin (); itr != source.MemberEnd (); ++itr)
    {
      const char *name = itr->name.GetString ();

      // if the key is in both jsons
      if (dest.HasMember (name))
	{
	  db_json_merge_preserve_values (itr->value, dest[name], allocator);
	}
      else
	{
	  db_json_object_add_member (itr->name, itr->value, dest, allocator);
	}
    }
}

//
// db_json_merge_two_json_array_values () - append source array into destination array
//
// source (in)    : source array
// dest (in/out)  : destination array
// allocator (in) : allocator
//
static void
db_json_merge_two_json_array_values (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator)
{
  assert (source.IsArray ());
  assert (dest.IsArray ());

  JSON_VALUE source_copy (source, allocator);

  for (JSON_VALUE::ValueIterator itr = source_copy.Begin (); itr != source_copy.End (); ++itr)
    {
      dest.PushBack (*itr, allocator);
    }
}

//
// db_json_array_push_back () - push value to array
//
static void
db_json_array_push_back (const JSON_VALUE &value, JSON_VALUE &dest_array, JSON_PRIVATE_MEMPOOL &allocator)
{
  assert (dest_array.IsArray ());

  // PushBack cannot guarantee const property, so we need a copy of value. also a local allocator is needed
  dest_array.PushBack (JSON_VALUE (value, allocator), allocator);
}

//
// db_json_object_add_member () - add member (name & value) to object
//
static void
db_json_object_add_member (const JSON_VALUE &name, const JSON_VALUE &value, JSON_VALUE &dest_object,
			   JSON_PRIVATE_MEMPOOL &allocator)
{
  assert (dest_object.IsObject ());

  // AddMember cannot guarantee const property, so we need copies of name and value
  dest_object.AddMember (JSON_VALUE (name, allocator), JSON_VALUE (value, allocator), allocator);
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

JSON_DOC *
db_json_allocate_doc ()
{
  JSON_DOC *doc = new JSON_DOC ();
  return doc;
}

JSON_DOC *
db_json_make_json_object ()
{
  JSON_DOC *doc = new JSON_DOC ();
  doc->SetObject ();
  return doc;
}

JSON_DOC *
db_json_make_json_array ()
{
  JSON_DOC *doc = new JSON_DOC();
  doc->SetArray ();
  return doc;
}

void
db_json_delete_doc (JSON_DOC *&doc)
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
  else
    {
      return val1 == NULL && val2 == NULL;
    }
}

static void
db_json_merge_preserve_values (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator)
{
  if (source.IsObject () && dest.IsObject ())
    {
      db_json_merge_two_json_objects_preserve (source, dest, allocator);
    }
  else
    {
      if (!dest.IsArray ())
	{
	  db_json_value_wrap_as_array (dest, allocator);
	}
      if (source.IsArray ())
	{
	  db_json_merge_two_json_array_values (source, dest, allocator);
	}
      else
	{
	  db_json_array_push_back (source, dest, allocator);
	}
    }
}

static void
db_json_merge_patch_values (const JSON_VALUE &source, JSON_VALUE &dest, JSON_PRIVATE_MEMPOOL &allocator)
{
  if (source.IsObject ())
    {
      if (!dest.IsObject ())
	{
	  dest.SetObject ();
	}
      db_json_merge_two_json_objects_patch (source, dest, allocator);
    }
  else
    {
      dest.CopyFrom (source, allocator);
    }
}

/*
 * db_json_merge_patch_func () - Merge the source json into destination json and patch
 *                               members having duplicate keys
 *
 * return                   : error code
 * dest (in)                : json where to merge
 * source (in)              : json to merge
 *
 * example                  : let x = { "a": 1, "b": 2 }
 *                                y = { "a": 3, "c": 4 }
 *                                z = { "a": 5, "d": 6 }
 *
 * JSON_MERGE_PATCH (x, y, z) = {"a": 5, "b": 2, "c": 4, "d": 6}
 */
int
db_json_merge_patch_func (const JSON_DOC *source, JSON_DOC *&dest)
{
  if (dest == NULL)
    {
      dest = db_json_allocate_doc ();
      db_json_copy_doc (*dest, source);
      return NO_ERROR;
    }

  const JSON_VALUE &source_value = db_json_doc_to_value (*source);
  JSON_VALUE &dest_value = db_json_doc_to_value (*dest);

  db_json_merge_patch_values (source_value, dest_value, dest->GetAllocator ());

  return NO_ERROR;
}

/*
 * db_json_merge_preserve_func () - Merge the source json into destination json preserving
 *                                  members having duplicate keys
 *
 * return                   : error code
 * dest (in)                : json where to merge
 * source (in)              : json to merge
 *
 * example                  : let x = { "a": 1, "b": 2 }
 *                                y = { "a": 3, "c": 4 }
 *                                z = { "a": 5, "d": 6 }
 *
 * JSON_MERGE_PRESERVE (x, y, z) = {"a": [1, 3, 5], "b": 2, "c": 4, "d": 6}
 */
int
db_json_merge_preserve_func (const JSON_DOC *source, JSON_DOC *&dest)
{
  if (dest == NULL)
    {
      dest = db_json_allocate_doc ();
      db_json_copy_doc (*dest, source);
      return NO_ERROR;
    }

  const JSON_VALUE &source_value = db_json_doc_to_value (*source);
  JSON_VALUE &dest_value = db_json_doc_to_value (*dest);

  db_json_merge_preserve_values (source_value, dest_value, dest->GetAllocator ());

  return NO_ERROR;
}

DB_JSON_TYPE
db_json_get_type (const JSON_DOC *doc)
{
  return db_json_get_type_of_value (doc);
}

int
db_json_get_int_from_document (const JSON_DOC *doc)
{
  return db_json_get_int_from_value (doc);
}

std::int64_t
db_json_get_bigint_from_document (const JSON_DOC *doc)
{
  return db_json_get_bigint_from_value (doc);
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

bool
db_json_get_bool_from_document (const JSON_DOC *doc)
{
  return db_json_get_bool_from_value (doc);
}

char *
db_json_copy_string_from_document (const JSON_DOC *doc)
{
  return db_json_copy_string_from_value (doc);
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
  else if (val->IsInt64 () || val->IsUint ())
    {
      return DB_JSON_BIGINT;
    }
  else if (val->IsFloat () || val->IsDouble () || val->IsUint64 ())
    {
      /* quick fix: treat uint64 as double since we don't have an ubigint type */
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

std::int64_t
db_json_get_bigint_from_value (const JSON_VALUE *val)
{
  if (val == NULL)
    {
      assert (false);
      return 0;
    }

  assert (db_json_get_type_of_value (val) == DB_JSON_BIGINT);

  return val->IsInt64 () ? val->GetInt64 () : val->GetUint ();
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
  return db_json_bool_to_string (db_json_get_bool_from_value (doc));
}

bool
db_json_get_bool_from_value (const JSON_VALUE *doc)
{
  if (doc == NULL)
    {
      assert (false);
      return false;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_BOOL);
  return doc->GetBool ();
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
db_json_er_set_path_does_not_exist (const char *file_name, const int line_no, const JSON_PATH &path,
				    const JSON_DOC *doc)
{
  // get the json body
  char *raw_json_body = db_json_get_raw_json_body_from_document (doc);
  cubmem::private_unique_ptr<char> unique_ptr (raw_json_body, NULL);

  er_set (ER_ERROR_SEVERITY, file_name, line_no, ER_JSON_PATH_DOES_NOT_EXIST, 2,
	  path.dump_json_path ().c_str (), raw_json_body);

  return ER_JSON_PATH_DOES_NOT_EXIST;
}

static int
db_json_er_set_expected_other_type (const char *file_name, const int line_no, const JSON_PATH &path,
				    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type,
				    const DB_JSON_TYPE &expected_type_optional)
{
  const char *found_type_str = db_json_get_json_type_as_str (found_type);
  std::string expected_type_str = db_json_get_json_type_as_str (expected_type);

  if (expected_type_optional != DB_JSON_NULL)
    {
      expected_type_str += " or ";
      expected_type_str += db_json_get_json_type_as_str (expected_type_optional);
    }

  er_set (ER_ERROR_SEVERITY, file_name, line_no, ER_JSON_EXPECTED_OTHER_TYPE, 3,
	  path.dump_json_path ().c_str (), expected_type_str.c_str (), found_type_str);

  return ER_JSON_EXPECTED_OTHER_TYPE;
}

int
db_json_normalize_path_string (const char *pointer_path, std::string &output)
{
  JSON_PATH jp;
  int error_code = jp.parse (pointer_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  output = jp.dump_json_path ();

  return NO_ERROR;
}

int
db_json_path_unquote_object_keys_external (std::string &sql_path)
{
  return db_json_path_unquote_object_keys (sql_path);
}

/*
 * db_json_path_contains_wildcard () - Check whether a given sql_path contains wildcards
 *
 * sql_path (in)       : null-terminated string
 */
bool
db_json_path_contains_wildcard (const char *sql_path)
{
  if (sql_path == NULL)
    {
      return false;
    }

  bool unescaped_backslash = false;
  bool in_quotes = false;
  for (std::size_t i = 0; sql_path[i] != '\0'; ++i)
    {
      if (!in_quotes && sql_path[i] == '*')
	{
	  return true;
	}

      if (!unescaped_backslash && sql_path[i] == '"')
	{
	  in_quotes = !in_quotes;
	}

      if (sql_path[i] == '\\')
	{
	  unescaped_backslash = !unescaped_backslash;
	}
      else
	{
	  unescaped_backslash = false;
	}
    }
  return false;
}

/*
 * db_json_get_paths_helper () - Recursive function to get the paths from a json object
 *
 * obj (in)                : current object
 * sql_path (in)           : the path for the current object
 * paths (in/out)          : vector where we will store all the paths
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

/*
 * db_json_get_all_paths_func () - Returns the paths from a JSON document as a JSON array
 *
 * doc (in)                : json document
 * result_json (in)        : a json array that contains all the paths
 */
int
db_json_get_all_paths_func (const JSON_DOC &doc, JSON_DOC *&result_json)
{
  JSON_PATH p;
  const JSON_VALUE *head = p.get (doc);
  std::vector<std::string> paths;

  // call the helper to get the paths
  db_json_get_paths_helper (*head, "$", paths);

  result_json->SetArray ();

  for (auto &path : paths)
    {
      JSON_VALUE val;
      val.SetString (path.c_str (), result_json->GetAllocator ());
      result_json->PushBack (val, result_json->GetAllocator ());
    }

  return NO_ERROR;
}

/*
 * db_json_pretty_func () - Returns the stringified version of a JSON document
 *
 * doc (in)                : json document
 * result_str (in)         : a string that contains the json in a pretty format
 *                           NOTE: Memory for the result_str is obtained with db_private_strdup and needs to be freed
 */
void
db_json_pretty_func (const JSON_DOC &doc, char *&result_str)
{
  assert (result_str == nullptr);

  JSON_PRETTY_WRITER json_pretty_writer;

  doc.Accept (json_pretty_writer);

  result_str = db_private_strdup (NULL, json_pretty_writer.ToString ().c_str ());
}

std::string
db_json_json_string_as_utf8 (std::string raw_json_string)
{
  assert (raw_json_string.length () >= 2 && raw_json_string[0] == '"');

  JSON_DOC *doc = nullptr;
  if (db_json_get_json_from_str (raw_json_string.c_str (), doc, raw_json_string.length ()) != NO_ERROR)
    {
      assert (false);
      return "";
    }

  std::string res = doc->IsString () ? doc->GetString () : "";
  delete doc;

  return res;
}

/*
 * db_json_keys_func () - Returns the keys from the top-level value of a JSON object as a JSON array
 *
 * return                  : error code
 * doc (in)                : json document
 * result_json (in)        : a json array that contains all the paths
 * raw_path (in)           : specified path
 */
int
db_json_keys_func (const JSON_DOC &doc, JSON_DOC &result_json, const char *raw_path)
{
  JSON_PATH p;
  int error_code = p.parse (raw_path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  const JSON_VALUE *head = p.get (doc);

  // the specified path does not exist in the current JSON document
  if (head == NULL)
    {
      return db_json_er_set_path_does_not_exist (ARG_FILE_LINE, p, &doc);
    }
  else if (head->IsObject ())
    {
      result_json.SetArray ();

      for (auto it = head->MemberBegin (); it != head->MemberEnd (); ++it)
	{
	  JSON_VALUE val;

	  val.SetString (it->name.GetString (), result_json.GetAllocator ());
	  result_json.PushBack (val, result_json.GetAllocator ());
	}
    }

  return NO_ERROR;
}

bool
db_json_value_has_numeric_type (const JSON_VALUE *doc)
{
  return db_json_get_type_of_value (doc) == DB_JSON_INT || db_json_get_type_of_value (doc) == DB_JSON_BIGINT
	 || db_json_get_type_of_value (doc) == DB_JSON_DOUBLE;
}

/*
 *  The following rules define containment:
 *  A candidate scalar is contained in a target scalar if and only if they are comparable and are equal.
 *  Two scalar values are comparable if they have the same JSON_TYPE () types,
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
      else if (doc_type == DB_JSON_BIGINT)
	{
	  result = (db_json_get_bigint_from_value (doc) == db_json_get_bigint_from_value (value));
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

void
db_json_set_string_to_doc (JSON_DOC *doc, const char *str, unsigned len)
{
  doc->SetString (str, len, doc->GetAllocator ());
}

void
db_json_set_double_to_doc (JSON_DOC *doc, double d)
{
  doc->SetDouble (d);
}

void
db_json_set_int_to_doc (JSON_DOC *doc, int i)
{
  doc->SetInt (i);
}

void
db_json_set_bigint_to_doc (JSON_DOC *doc, std::int64_t i)
{
  doc->SetInt64 (i);
}

bool
db_json_are_docs_equal (const JSON_DOC *doc1, const JSON_DOC *doc2)
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

bool
db_json_doc_has_numeric_type (const JSON_DOC *doc)
{
  return db_json_value_has_numeric_type (doc);
}

bool
db_json_doc_is_uncomparable (const JSON_DOC *doc)
{
  DB_JSON_TYPE type = db_json_get_type (doc);

  return (type == DB_JSON_ARRAY || type == DB_JSON_OBJECT);
}

//
// db_value_to_json_path () - get path string from value; if value is not a string, an error is returned
//
// return          : error code
// path_value (in) : path value
// fcode (in)      : JSON function (for verbose error)
// path_str (out)  : path string
//
int
db_value_to_json_path (const DB_VALUE &path_value, FUNC_CODE fcode, std::string &path_str)
{
  if (!TP_IS_CHAR_TYPE (db_value_domain_type (&path_value)))
    {
      int error_code = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 2, fcode_get_uppercase_name (fcode), "STRING");
      return error_code;
    }
  path_str = { db_get_string (&path_value), (size_t) db_get_string_size (&path_value) };
  return NO_ERROR;
}

/* db_value_to_json_doc - create a JSON_DOC from db_value.
 *
 * return         : error code
 * db_val(in)     : input db_value
 * force_copy(in) : whether json_doc needs to own the json_doc
 * json_doc(out)  : output JSON_DOC pointer
 */
int
db_value_to_json_doc (const DB_VALUE &db_val, bool force_copy, JSON_DOC_STORE &json_doc)
{
  int error_code = NO_ERROR;

  if (db_value_is_null (&db_val))
    {
      json_doc.create_mutable_reference ();
      db_json_make_document_null (json_doc.get_mutable ());
      return NO_ERROR;
    }

  switch (db_value_domain_type (&db_val))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    {
      DB_VALUE utf8_str;
      const DB_VALUE *json_str_val;
      error_code = db_json_copy_and_convert_to_utf8 (&db_val, &utf8_str, &json_str_val);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      JSON_DOC *json_doc_ptr = NULL;
      error_code = db_json_get_json_from_str (db_get_string (json_str_val), json_doc_ptr, db_get_string_size (json_str_val));
      json_doc.set_mutable_reference (json_doc_ptr);
      pr_clear_value (&utf8_str);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      return error_code;
    }

    case DB_TYPE_JSON:
      if (force_copy)
	{
	  json_doc.set_mutable_reference (db_json_get_copy_of_doc (db_val.data.json.document));
	}
      else
	{
	  json_doc.set_immutable_reference (db_val.data.json.document);
	}
      return NO_ERROR;

    case DB_TYPE_NULL:
      json_doc.create_mutable_reference ();
      return NO_ERROR;

    default:
      // todo: more specific error
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_EXPECTING_JSON_DOC, 1,
	      pr_type_name (db_value_domain_type (&db_val)));
      return ER_JSON_EXPECTING_JSON_DOC;
    }
}

/* db_value_to_json_value - create a JSON_DOC treated as JSON_VALUE from db_value.
 *
 * return     : error code
 * db_val(in) : input db_value
 * json_val(out) : output JSON_DOC pointer
 *
 * TODO: if db_val is a JSON value, document is copied. Sometimes, we only need to "read" the value, so copying is not
 *       necessary. adapt function for those cases.
 */
int
db_value_to_json_value (const DB_VALUE &db_val, JSON_DOC_STORE &json_doc)
{
  if (db_value_is_null (&db_val))
    {
      json_doc.create_mutable_reference ();
      db_json_make_document_null (json_doc.get_mutable ());
      return NO_ERROR;
    }

  switch (DB_VALUE_DOMAIN_TYPE (&db_val))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    {
      DB_VALUE utf8_str;
      const DB_VALUE *json_str_val;
      int error_code = db_json_copy_and_convert_to_utf8 (&db_val, &utf8_str, &json_str_val);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      json_doc.create_mutable_reference ();

      db_json_set_string_to_doc (json_doc.get_mutable (), db_get_string (json_str_val),
				 (unsigned) db_get_string_size (json_str_val));
      pr_clear_value (&utf8_str);
    }
    break;
    case DB_TYPE_ENUMERATION:
      json_doc.create_mutable_reference ();
      db_json_set_string_to_doc (json_doc.get_mutable (), db_get_enum_string (&db_val),
				 (unsigned) db_get_enum_string_size (&db_val));
      break;

    default:
      DB_VALUE dest;
      TP_DOMAIN_STATUS status = tp_value_cast (&db_val, &dest, &tp_Json_domain, false);
      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
	  return ER_QSTR_INVALID_DATA_TYPE;
	}

      // if db_val is json a copy to dest is made so we can own it
      json_doc.set_mutable_reference (db_get_json_document (&dest));
    }

  return NO_ERROR;
}

int
db_value_to_json_key (const DB_VALUE &db_val, std::string &key_str)
{
  DB_VALUE cnv_to_str;
  const DB_VALUE *str_valp = &db_val;

  db_make_null (&cnv_to_str);

  if (!DB_IS_STRING (&db_val))
    {
      TP_DOMAIN_STATUS status = tp_value_cast (&db_val, &cnv_to_str, &tp_String_domain, false);
      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
	  return ER_QSTR_INVALID_DATA_TYPE;
	}
      str_valp = &cnv_to_str;
    }
  key_str = { db_get_string (str_valp), (size_t) db_get_string_size (str_valp) };
  pr_clear_value (&cnv_to_str);

  return NO_ERROR;
}

void
db_make_json_from_doc_store_and_release (DB_VALUE &value, JSON_DOC_STORE &doc_store)
{
  db_make_json (&value, doc_store.release_mutable_reference (), true);
}

static void
db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator)
{
  if (value.IsArray ())
    {
      return;
    }

  JSON_VALUE swap_value;

  swap_value.SetArray ();
  swap_value.PushBack (value, allocator);
  swap_value.Swap (value);
}

int
JSON_WALKER::WalkDocument (const JSON_DOC &document)
{
  m_stop = false;
  return WalkValue (db_json_doc_to_value (document));
}

int
JSON_WALKER::WalkValue (const JSON_VALUE &value)
{
  int error_code = NO_ERROR;

  if (m_stop)
    {
      return NO_ERROR;
    }
  error_code = CallBefore (value);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (m_stop)
    {
      return NO_ERROR;
    }

  if (value.IsObject ())
    {
      for (auto it = value.MemberBegin (); it != value.MemberEnd (); ++it)
	{
	  CallOnKeyIterate (it->name);
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	  error_code = WalkValue (it->value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	}
    }
  else if (value.IsArray ())
    {
      for (const JSON_VALUE *it = value.Begin (); it != value.End (); ++it)
	{
	  CallOnArrayIterate ();
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	  error_code = WalkValue (*it);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (m_stop)
	    {
	      return NO_ERROR;
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

bool
JSON_SERIALIZER::SaveSizePointers (char *ptr)
{
  // save the current pointer
  m_size_pointers.push (ptr);

  // skip the size
  m_error = or_put_int (m_buffer, 0);

  return !HasError ();
}

void
JSON_SERIALIZER::SetSizePointers (SizeType size)
{
  char *buf = m_size_pointers.top ();
  m_size_pointers.pop ();

  assert (buf >= m_buffer->buffer && buf < m_buffer->ptr);

  // overwrite that pointer with the correct size
  or_pack_int (buf, (int) size);
}

bool
JSON_SERIALIZER::PackType (const DB_JSON_TYPE &type)
{
  m_error = or_put_int (m_buffer, static_cast<int> (type));
  return !HasError ();
}

bool
JSON_SERIALIZER::PackString (const char *str)
{
  m_error = or_put_string_aligned_with_length (m_buffer, str);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Null ()
{
  m_length += GetTypePackedSize ();
  return true;
}

bool
JSON_SERIALIZER::Null ()
{
  return PackType (DB_JSON_NULL);
}

bool
JSON_SERIALIZER_LENGTH::Bool (bool b)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int (0 or 1)
  m_length += GetTypePackedSize () + OR_INT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Bool (bool b)
{
  if (!PackType (DB_JSON_BOOL))
    {
      return false;
    }

  m_error = or_put_int (m_buffer, b ? 1 : 0);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Int (int i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_INT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Int (int i)
{
  if (!PackType (DB_JSON_INT))
    {
      return false;
    }

  int is_uint = 0;
  or_put_int (m_buffer, is_uint);

  m_error = or_put_int (m_buffer, i);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Uint (unsigned i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_INT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Uint (unsigned i)
{
  if (!PackType (DB_JSON_INT))
    {
      return false;
    }

  int is_uint = 1;
  or_put_int (m_buffer, is_uint);

  m_error = or_put_int (m_buffer, i);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Int64 (std::int64_t i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int64
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_BIGINT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Int64 (std::int64_t i)
{
  if (!PackType (DB_JSON_BIGINT))
    {
      return false;
    }

  int is_uint64 = 0;
  or_put_int (m_buffer, is_uint64);

  m_error = or_put_bigint (m_buffer, i);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Uint64 (std::uint64_t i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int64
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_BIGINT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Uint64 (std::uint64_t i)
{
  if (!PackType (DB_JSON_BIGINT))
    {
      return false;
    }

  int is_uint64 = 1;
  or_put_int (m_buffer, is_uint64);

  m_error = or_put_bigint (m_buffer, i);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::Double (double d)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is double
  m_length += GetTypePackedSize () + OR_DOUBLE_SIZE;
  return true;
}

bool
JSON_SERIALIZER::Double (double d)
{
  if (!PackType (DB_JSON_DOUBLE))
    {
      return false;
    }

  m_error = or_put_double (m_buffer, d);
  return !HasError ();
}

bool
JSON_SERIALIZER_LENGTH::String (const Ch *str, SizeType length, bool copy)
{
  m_length += GetTypePackedSize () + GetStringPackedSize (str);
  return true;
}

bool
JSON_SERIALIZER::String (const Ch *str, SizeType length, bool copy)
{
  return PackType (DB_JSON_STRING) && PackString (str);
}

bool
JSON_SERIALIZER_LENGTH::Key (const Ch *str, SizeType length, bool copy)
{
  // we encode directly the key because we know we are dealing with object
  m_length += GetStringPackedSize (str);
  return true;
}

bool
JSON_SERIALIZER::Key (const Ch *str, SizeType length, bool copy)
{
  return PackString (str);
}

bool
JSON_SERIALIZER_LENGTH::StartObject ()
{
  m_length += GetTypePackedSize ();
  m_length += OR_INT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::StartObject ()
{
  if (!PackType (DB_JSON_OBJECT))
    {
      return false;
    }

  // add pointer to stack, because we need to come back to overwrite this pointer with the correct size
  // we will know that in EndObject function
  return SaveSizePointers (m_buffer->ptr);
}

bool
JSON_SERIALIZER_LENGTH::StartArray ()
{
  m_length += GetTypePackedSize ();
  m_length += OR_INT_SIZE;
  return true;
}

bool
JSON_SERIALIZER::StartArray ()
{
  if (!PackType (DB_JSON_ARRAY))
    {
      return false;
    }

  // add pointer to stack, because we need to come back to overwrite this pointer with the correct size
  // we will know that in EndObject function
  return SaveSizePointers (m_buffer->ptr);
}

bool
JSON_SERIALIZER_LENGTH::EndObject (SizeType memberCount)
{
  return true;
}

bool
JSON_SERIALIZER::EndObject (SizeType memberCount)
{
  // overwrite the count
  SetSizePointers (memberCount);
  return true;
}

bool
JSON_SERIALIZER_LENGTH::EndArray (SizeType elementCount)
{
  return true;
}

bool
JSON_SERIALIZER::EndArray (SizeType elementCount)
{
  // overwrite the count
  SetSizePointers (elementCount);
  return true;
}

void
JSON_PRETTY_WRITER::WriteDelimiters (bool is_key)
{
  // just a scalar, no indentation needed
  if (m_level_stack.empty ())
    {
      return;
    }

  // there are 3 cases the current element can be
  // 1) an element from an ARRAY
  // 2) a key from an OBJECT
  // 3) a value from an OBJECT
  // when dealing with array elements, all elements except the first need to write a comma before writing his value
  // when dealing with objects, all keys except the first need to write a comma before writing the current key value
  if (is_key || m_level_stack.top ().type == DB_JSON_TYPE::DB_JSON_ARRAY)
    {
      // not the first key or the first element from ARRAY, so we need to separate elements
      if (!m_level_stack.top ().is_first)
	{
	  m_buffer.append (",");
	}
      else
	{
	  // for the first key or element skip the comma
	  m_level_stack.top ().is_first = false;
	}

      SetIndentOnNewLine ();
    }
  else
    {
      // the case we are in an OBJECT and print a value
      assert (m_level_stack.top ().type == DB_JSON_TYPE::DB_JSON_OBJECT);
      m_buffer.append (" ");
    }
}

void
JSON_PRETTY_WRITER::PushLevel (const DB_JSON_TYPE &type)
{
  // advance one level
  m_current_indent += LEVEL_INDENT_UNIT;

  // push the new context
  m_level_stack.push (level_context (type, true));
}

void
JSON_PRETTY_WRITER::PopLevel ()
{
  // reestablish the old context
  m_current_indent -= LEVEL_INDENT_UNIT;
  m_level_stack.pop ();
}

void
JSON_PRETTY_WRITER::SetIndentOnNewLine ()
{
  m_buffer.append ("\n").append (m_current_indent, ' ');
}

bool
JSON_PRETTY_WRITER::Null ()
{
  WriteDelimiters ();

  m_buffer.append ("null");

  return true;
}

bool
JSON_PRETTY_WRITER::Bool (bool b)
{
  WriteDelimiters ();

  m_buffer.append (b ? "true" : "false");

  return true;
}

bool
JSON_PRETTY_WRITER::Int (int i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool
JSON_PRETTY_WRITER::Uint (unsigned i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool
JSON_PRETTY_WRITER::Int64 (std::int64_t i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool
JSON_PRETTY_WRITER::Uint64 (std::uint64_t i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool
JSON_PRETTY_WRITER::Double (double d)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (d));

  return true;
}

bool
JSON_PRETTY_WRITER::String (const Ch *str, SizeType length, bool copy)
{
  WriteDelimiters ();

  m_buffer.append ("\"").append (str).append ("\"");

  return true;
}

bool
JSON_PRETTY_WRITER::StartObject ()
{
  WriteDelimiters ();

  m_buffer.append ("{");

  PushLevel (DB_JSON_TYPE::DB_JSON_OBJECT);

  return true;
}

bool
JSON_PRETTY_WRITER::Key (const Ch *str, SizeType length, bool copy)
{
  WriteDelimiters (true);

  m_buffer.append ("\"").append (str).append ("\"").append (":");

  return true;
}

bool
JSON_PRETTY_WRITER::StartArray ()
{
  WriteDelimiters ();

  m_buffer.append ("[");

  PushLevel (DB_JSON_TYPE::DB_JSON_ARRAY);

  return true;
}

bool
JSON_PRETTY_WRITER::EndObject (SizeType memberCount)
{
  PopLevel ();

  if (memberCount != 0)
    {
      // go the next line and set the correct indentation
      SetIndentOnNewLine ();
    }

  m_buffer.append ("}");

  return true;
}

bool
JSON_PRETTY_WRITER::EndArray (SizeType elementCount)
{
  PopLevel ();

  if (elementCount != 0)
    {
      // go the next line and set the correct indentation
      SetIndentOnNewLine ();
    }

  m_buffer.append ("]");

  return true;
}

/*
 * db_json_serialize () - serialize a json document
 *
 * return     : pair (buffer, length)
 * doc (in)   : the document that we want to serialize
 *
 * buffer will contain the json serialized
 * length is the buffer size (we can not use strlen!)
 */
int
db_json_serialize (const JSON_DOC &doc, or_buf &buffer)
{
  JSON_SERIALIZER js (buffer);
  int error_code = NO_ERROR;

  if (!doc.Accept (js))
    {
      error_code = ER_TF_BUFFER_OVERFLOW;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
    }

  return error_code;
}

std::size_t
db_json_serialize_length (const JSON_DOC &doc)
{
  JSON_SERIALIZER_LENGTH jsl;

  doc.Accept (jsl);

  return jsl.GetLength ();
}

/*
 * db_json_or_buf_underflow () - Check if the buffer return underflow
 *
 * return            : error_code
 * buf (in)          : the buffer which contains the data
 * length (in)       : the length of the string that we want to retrieve
 *
 * We do this check separately because we want to avoid an additional memory copy when getting the data from the buffer
 * for storing it in the json document
 */
static int
db_json_or_buf_underflow (or_buf *buf, size_t length)
{
  if ((buf->ptr + length) > buf->endptr)
    {
      return or_underflow (buf);
    }

  return NO_ERROR;
}

static int
db_json_unpack_string_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator)
{
  size_t str_length;
  int rc = NO_ERROR;

  // get the string length
  str_length = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  rc = db_json_or_buf_underflow (buf, str_length);
  if (rc != NO_ERROR)
    {
      // we need to assert error here because or_underflow sets the error unlike or_overflow
      // which only returns the error code
      ASSERT_ERROR ();
      return rc;
    }

  // set the string directly from the buffer to avoid additional copy
  value.SetString (buf->ptr, static_cast<rapidjson::SizeType> (str_length - 1), doc_allocator);
  // update the buffer pointer
  buf->ptr += str_length;

  // still need to take care of the alignment
  rc = or_align (buf, INT_ALIGNMENT);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  return NO_ERROR;
}

static int
db_json_unpack_int_to_value (OR_BUF *buf, JSON_VALUE &value)
{
  int rc = NO_ERROR;
  int int_value;

  int is_uint = or_get_int (buf, &rc);

  // unpack int
  int_value = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  if (is_uint)
    {
      value.SetUint ((unsigned) int_value);
    }
  else
    {
      value.SetInt (int_value);
    }

  return NO_ERROR;
}

static int
db_json_unpack_bigint_to_value (OR_BUF *buf, JSON_VALUE &value)
{
  int rc = NO_ERROR;
  DB_BIGINT bigint_value;

  int is_uint64 = or_get_int (buf, &rc);

  // unpack bigint
  bigint_value = or_get_bigint (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  if (is_uint64)
    {
      value.SetUint64 ((std::uint64_t) bigint_value);
    }
  else
    {
      value.SetInt64 (bigint_value);
    }

  return NO_ERROR;
}

static int
db_json_unpack_double_to_value (OR_BUF *buf, JSON_VALUE &value)
{
  int rc = NO_ERROR;
  double double_value;

  // unpack double
  double_value = or_get_double (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  value.SetDouble (double_value);

  return NO_ERROR;
}

static int
db_json_unpack_bool_to_value (OR_BUF *buf, JSON_VALUE &value)
{
  int rc = NO_ERROR;
  int int_value;

  int_value = or_get_int (buf, &rc); // it can be 0 or 1
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  assert (int_value == 0 || int_value == 1);

  value.SetBool (int_value == 1);

  return NO_ERROR;
}

static int
db_json_unpack_object_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator)
{
  int rc = NO_ERROR;
  int size;

  value.SetObject ();

  // get the member count of the object
  size = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  // for each key-value pair we need to deserialize the value
  for (int i = 0; i < size; i++)
    {
      // get the key
      JSON_VALUE key;
      rc = db_json_unpack_string_to_value (buf, key, doc_allocator);
      if (rc != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return rc;
	}

      // get the value
      JSON_VALUE child;
      rc = db_json_deserialize_doc_internal (buf, child, doc_allocator);
      if (rc != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
	  return rc;
	}

      value.AddMember (key, child, doc_allocator);
    }

  return NO_ERROR;
}

static int
db_json_unpack_array_to_value (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator)
{
  int rc = NO_ERROR;
  int size;

  value.SetArray ();

  // get the member count of the array
  size = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
      return rc;
    }

  // preallocate
  value.Reserve (size, doc_allocator);
  // for each member we need to deserialize it
  for (int i = 0; i < size; i++)
    {
      JSON_VALUE child;
      rc = db_json_deserialize_doc_internal (buf, child, doc_allocator);
      if (rc != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_OVERFLOW, 0);
	  return rc;
	}

      value.PushBack (child, doc_allocator);
    }

  return NO_ERROR;
}

/*
 * db_json_deserialize_doc_internal () - this is where the deserialization actually happens
 *
 * return             : error_code
 * buf (in)           : the buffer which contains the json serialized
 * doc_allocator (in) : the allocator used to create json "tree"
 * value (in)         : the current value from the json document
 */
static int
db_json_deserialize_doc_internal (OR_BUF *buf, JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &doc_allocator)
{
  DB_JSON_TYPE json_type;
  int rc = NO_ERROR;

  // get the json scalar value
  json_type = static_cast<DB_JSON_TYPE> (or_get_int (buf, &rc));
  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
      return rc;
    }

  switch (json_type)
    {
    case DB_JSON_INT:
      rc = db_json_unpack_int_to_value (buf, value);
      break;

    case DB_JSON_BIGINT:
      rc = db_json_unpack_bigint_to_value (buf, value);
      break;

    case DB_JSON_DOUBLE:
      rc = db_json_unpack_double_to_value (buf, value);
      break;

    case DB_JSON_STRING:
      rc = db_json_unpack_string_to_value (buf, value, doc_allocator);
      break;

    case DB_JSON_BOOL:
      rc = db_json_unpack_bool_to_value (buf, value);
      break;

    case DB_JSON_NULL:
      value.SetNull ();
      break;

    case DB_JSON_OBJECT:
      rc = db_json_unpack_object_to_value (buf, value, doc_allocator);
      break;

    case DB_JSON_ARRAY:
      rc = db_json_unpack_array_to_value (buf, value, doc_allocator);
      break;

    default:
      /* we shouldn't get here */
      assert (false);
      return ER_FAILED;
    }

  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
    }

  return rc;
}

/*
 * db_json_deserialize () - deserialize a json reconstructing the object from a buffer
 *
 * return        : error code
 * json_raw (in) : buffer of the json serialized
 * doc (in)      : json document deserialized
 */
int
db_json_deserialize (OR_BUF *buf, JSON_DOC *&doc)
{
  int error_code = NO_ERROR;

  // create the document that we want to reconstruct
  doc = db_json_allocate_doc ();

  // the conversion from JSON_DOC to JSON_VALUE is needed because we want a reference to current node
  // from json "tree" while iterating
  error_code = db_json_deserialize_doc_internal (buf, db_json_doc_to_value (*doc), doc->GetAllocator ());
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_json_delete_doc (doc);
    }

  return error_code;
}
