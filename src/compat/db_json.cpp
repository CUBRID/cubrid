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
#include "memory_private_allocator.hpp"
#include "object_primitive.h"
#include "query_dump.h"
#include "string_opfunc.h"
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
#include <unordered_set>
#include <vector>
#include <stack>
#include <memory>
#include <climits>
#include <functional>

#include <cctype>

#define TODO_OPTIMIZE_JSON_BODY_STRING true

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

#if TODO_OPTIMIZE_JSON_BODY_STRING
struct JSON_RAW_STRING_DELETER
{
  void operator() (char *p) const
  {
    db_private_free (NULL, p);
  }
};
#endif // TODO_OPTIMIZE_JSON_BODY_STRING

typedef rapidjson::UTF8 <> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator <JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;
typedef rapidjson::GenericValue <JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_VALUE;
typedef rapidjson::GenericPointer <JSON_VALUE> JSON_POINTER;
typedef rapidjson::GenericStringBuffer<JSON_ENCODING, JSON_PRIVATE_ALLOCATOR> JSON_STRING_BUFFER;
typedef rapidjson::GenericMemberIterator<true, JSON_ENCODING, JSON_PRIVATE_MEMPOOL>::Iterator JSON_MEMBER_ITERATOR;
typedef rapidjson::GenericArray<true, JSON_VALUE>::ConstValueIterator JSON_VALUE_ITERATOR;

class JSON_DOC: public rapidjson::GenericDocument <JSON_ENCODING, JSON_PRIVATE_MEMPOOL>
{
  public:
    bool IsLeaf ();

#if TODO_OPTIMIZE_JSON_BODY_STRING
    /* TODO:
    In the future, it will be better if instead of constructing the json_body each time we need it,
    we can have a boolean flag which indicates if the json_body is up to date or not.
    We will set the flag to false when we apply functions that modify the JSON_DOC (like json_set, json_insert etc.)
    If we apply functions that only retrieves values from JSON_DOC, the flag will remain unmodified

    When we need the json_body, we will traverse only once the json "tree" and update the json_body and also the flag,
    so next time we will get the json_body in O(1)

    const std::string &GetJsonBody () const
    {
      return json_body;
    }

    template<typename T>
    void SetJsonBody (T &&body) const
    {
      json_body = std::forward<T> (body);
    }
    */
#endif // TODO_OPTIMIZE_JSON_BODY_STRING
  private:
    static const int MAX_CHUNK_SIZE;
#if TODO_OPTIMIZE_JSON_BODY_STRING
    /* mutable std::string json_body; */
#endif // TODO_OPTIMIZE_JSON_BODY_STRING
};

typedef std::function<int (const JSON_VALUE &, const std::string &, bool &)> map_func_type;

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
    int WalkDocument (JSON_DOC &document);

  protected:
    // we should not instantiate this class, but extend it
    JSON_WALKER () = default;
    virtual ~JSON_WALKER () = default;

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

    virtual int
    CallOnArrayIterate ()
    {
      // do nothing
      return NO_ERROR;
    }

    virtual int
    CallOnKeyIterate (JSON_VALUE &key)
    {
      // do nothing
      return NO_ERROR;
    }

  private:
    int WalkValue (JSON_VALUE &value);

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
    int CallBefore (JSON_VALUE &value) override;
};

class JSON_PATH_MAPPER : public JSON_WALKER
{
  public:
    JSON_PATH_MAPPER (map_func_type func);
    JSON_PATH_MAPPER (JSON_PATH_MAPPER &) = delete;
    ~JSON_PATH_MAPPER () override = default;

  private:
    int CallBefore (JSON_VALUE &value) override;
    int CallAfter (JSON_VALUE &value) override;
    int CallOnArrayIterate () override;
    int CallOnKeyIterate (JSON_VALUE &key) override;

    map_func_type m_producer;
    std::stack<unsigned int> m_index;
    std::stack<std::string> m_accumulated_paths;
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

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;
const int JSON_DOC::MAX_CHUNK_SIZE = 64 * 1024; /* TODO does 64K serve our needs? */

static std::vector<std::pair<std::string, std::string> > uri_fragment_conversions =
{
  std::make_pair ("~", "~0"),
  std::make_pair ("/", "~1")
};
static const char *db_Json_pointer_delimiters = "/";
static const char *db_Json_sql_path_delimiters = "$.[]\"";

static unsigned int db_json_value_get_depth (const JSON_VALUE *doc);
static int db_json_value_is_contained_in_doc_helper (const JSON_VALUE *doc, const JSON_VALUE *value, bool &result);
static DB_JSON_TYPE db_json_get_type_of_value (const JSON_VALUE *val);
static bool db_json_value_has_numeric_type (const JSON_VALUE *doc);
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
static void db_json_normalize_path (std::string &path_string);
static void db_json_remove_leading_zeros_index (std::string &index);
static bool db_json_isspace (const unsigned char &ch);
static bool db_json_iszero (const unsigned char &ch);
static JSON_PATH_TYPE db_json_get_path_type (std::string &path_string);
static void db_json_build_path_special_chars (const JSON_PATH_TYPE &json_path_type,
    std::unordered_map<std::string, std::string> &special_chars);
static std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim, bool allow_empty);
static std::size_t skip_whitespaces (const std::string &path, std::size_t token_begin);
static bool db_json_sql_path_is_valid (std::string &sql_path, bool allow_wildcards);
static bool db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin);
static bool db_json_path_quote_and_validate_unquoted_object_key (std::string &path, std::size_t &token_begin);
static bool db_json_path_is_token_valid_unquoted_object_key (const std::string &path, std::size_t &token_begin);
static void json_path_strip_whitespaces (std::string &sql_path);
static int db_json_er_set_path_does_not_exist (const char *file_name, const int line_no, const std::string &path,
    const JSON_DOC *doc);
static void db_json_replace_token_special_chars (std::string &token,
    const std::unordered_map<std::string, std::string> &special_chars);
static bool db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards,
    std::size_t start = 0, std::size_t end = 0);
static void db_json_doc_wrap_as_array (JSON_DOC &doc);
static void db_json_value_wrap_as_array (JSON_VALUE &value, JSON_PRIVATE_MEMPOOL &allocator);
static const char *db_json_get_json_type_as_str (const DB_JSON_TYPE &json_type);
static int db_json_er_set_expected_other_type (const char *file_name, const int line_no, const std::string &path,
    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type,
    const DB_JSON_TYPE &expected_type_optional = DB_JSON_NULL);
static int db_json_array_shift_values (const JSON_DOC *value, JSON_DOC &doc, const std::string &path);
static int db_json_resolve_json_parent (JSON_DOC &doc, const std::string &path, JSON_VALUE *&resulting_json_parent);
static int db_json_insert_helper (const JSON_DOC *value, JSON_DOC &doc, JSON_POINTER &p, const std::string &path);
static int db_json_contains_duplicate_keys (JSON_DOC &doc);

STATIC_INLINE JSON_VALUE &db_json_doc_to_value (JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const JSON_VALUE &db_json_doc_to_value (const JSON_DOC &doc) __attribute__ ((ALWAYS_INLINE));
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

int JSON_DUPLICATE_KEYS_CHECKER::CallBefore (JSON_VALUE &value)
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
  , m_accumulated_paths ()
{
  m_accumulated_paths.push ("$");
}

int JSON_PATH_MAPPER::CallBefore (JSON_VALUE &value)
{
  if (value.IsArray ())
    {
      m_index.push (0);
    }

  if (value.IsObject () || value.IsArray ())
    {
      // should not be used. Only add a stack level
      m_accumulated_paths.push ("");
    }

  return NO_ERROR;
}

int JSON_PATH_MAPPER::CallAfter (JSON_VALUE &value)
{
  if (value.IsArray ())
    {
      m_index.pop ();
    }

  if (value.IsArray () || value.IsObject ())
    {
      m_accumulated_paths.pop ();
    }
  return m_producer (value, m_accumulated_paths.top (), m_stop);
}

int JSON_PATH_MAPPER::CallOnArrayIterate ()
{
  m_accumulated_paths.pop ();
  std::string path_item = m_accumulated_paths.top ();
  path_item += "[";
  path_item += std::to_string (m_index.top ()++);
  path_item += "]";

  m_accumulated_paths.push (path_item);
  return NO_ERROR;
}

int JSON_PATH_MAPPER::CallOnKeyIterate (JSON_VALUE &key)
{
  m_accumulated_paths.pop ();
  std::string path_item = m_accumulated_paths.top ();
  path_item += ".\"";

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

  m_accumulated_paths.push (path_item);
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
db_json_extract_document_from_path (const JSON_DOC *document, const std::vector<std::string> &paths, JSON_DOC *&result,
				    bool allow_wildcards)
{
  int error_code = NO_ERROR;

  if (document == NULL)
    {
      if (result != NULL)
	{
	  result->SetNull ();
	}
      return NO_ERROR;
    }

  std::vector<std::string> transformed_paths;
  for (const auto &path : paths)
    {
      transformed_paths.emplace_back ();
      error_code = db_json_convert_pointer_to_sql_path (path.c_str (), transformed_paths.back (), allow_wildcards);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  // wrap result in json array in case we have multiple paths or we have a json_path containing wildcards
  bool array_result = false;
  if (transformed_paths.size () > 1)
    {
      array_result = true;
    }
  else
    {
      array_result = db_json_path_contains_wildcard (transformed_paths[0].c_str ());
    }

  std::vector<std::string> regs;
  error_code = db_json_paths_to_regex (transformed_paths, regs, true);
  if (error_code)
    {
      return error_code;
    }

  // we gather extracted values in an array to match with the order of the given path arguments
  std::vector<std::vector<const JSON_VALUE *>> produced_array (transformed_paths.size ());
  const map_func_type &f = [&regs, &produced_array] (const JSON_VALUE &jv, const std::string &crt_path, bool &stop) -> int
  {
    for (std::size_t i = 0; i < regs.size (); ++i)
      {
	bool path_compatible;
	int error_code = regex_matches (regs[i].c_str (), crt_path.c_str (), CUB_REG_EXTENDED, &path_compatible);
	if (error_code)
	  {
	    ASSERT_ERROR ();
	    stop = true;
	    return error_code;
	  }

	if (path_compatible)
	  {
	    produced_array[i].push_back (&jv);
	  }
      }
    return NO_ERROR;
  };

  JSON_PATH_MAPPER json_extract_walker (f);
  json_extract_walker.WalkDocument (const_cast<JSON_DOC &> (*document));

  if (array_result)
    {
      for (const auto &produced : produced_array)
	{
	  for (const JSON_VALUE *p : produced)
	    {
	      if (result == NULL)
		{
		  result = db_json_allocate_doc ();
		  result->SetArray ();
		}

	      db_json_add_element_to_array (result, p);
	    }
	}
    }
  else
    {
      assert (produced_array.size () == 1 && (produced_array.empty () || produced_array.size () == 1));

      if (!produced_array[0].empty ())
	{
	  if (result == NULL)
	    {
	      result = db_json_allocate_doc ();
	    }

	  result->CopyFrom (*produced_array[0][0], result->GetAllocator ());
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

  std::vector<std::string> transformed_paths;
  for (const auto &path : paths)
    {
      transformed_paths.emplace_back ();
      error_code = db_json_convert_pointer_to_sql_path (path.c_str (), transformed_paths.back ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  std::vector<std::string> regs;
  error_code = db_json_paths_to_regex (transformed_paths, regs);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  std::unique_ptr<bool[]> found_set (new bool[paths.size ()]);
  for (std::size_t i = 0; i < paths.size (); ++i)
    {
      found_set[i] = false;
    }

  const map_func_type &f_find = [&regs, &found_set, find_all] (const JSON_VALUE &v, const std::string &accumulated_path,
				bool &stop) -> int
  {
    for (std::size_t i = 0; i < regs.size (); ++i)
      {
	bool path_compatible;
	int error_code = regex_matches (regs[i].c_str (), accumulated_path.c_str (), CUB_REG_EXTENDED, &path_compatible);
	if (error_code)
	  {
	    ASSERT_ERROR ();
	    stop = true;
	    return error_code;
	  }
	if (!found_set[i] && path_compatible)
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
  // todo: remove const_cast
  json_contains_path_walker.WalkDocument (const_cast<JSON_DOC &> (*document));

  result = find_all;
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
db_json_contains_duplicate_keys (JSON_DOC &doc)
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
db_json_resolve_json_parent (JSON_DOC &doc, const std::string &path, JSON_VALUE *&resulting_json_parent)
{
  std::size_t found = path.find_last_of ('/');
  if (found == std::string::npos)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  // parent pointer
  JSON_POINTER pointer_parent (path.substr (0, found).c_str ());
  if (!pointer_parent.IsValid ())
    {
      /* this shouldn't happen */
      assert (false);
      return ER_FAILED;
    }

  resulting_json_parent = pointer_parent.Get (doc);
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
  bool token_is_valid_index = db_json_path_is_token_valid_array_index (last_token, false);

  if (parent_json_type == DB_JSON_ARRAY && !token_is_valid_index)
    {
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, parent_json_type, DB_JSON_OBJECT);
    }
  if (parent_json_type == DB_JSON_OBJECT && token_is_valid_index)
    {
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, parent_json_type, DB_JSON_ARRAY);
    }

  return NO_ERROR;
}

static int
db_json_insert_helper (const JSON_DOC *value, JSON_DOC &doc, JSON_POINTER &p, const std::string &path)
{
  int error_code = NO_ERROR;
  JSON_VALUE *resulting_json_parent = NULL;

  // we don't need result_json_parent after this statement
  error_code = db_json_resolve_json_parent (doc, path, resulting_json_parent);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // put the value at the specified path
  p.Set (doc, *value, doc.GetAllocator ());

  return error_code;
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
  JSON_VALUE *resulting_json = NULL;

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
 * db_json_paths_to_regex ()
 *
 * transform path strings into regexes by escaping special characters '$', '[', '.', ']' and
 * replace [*], .*, ** with patterns that match accordingly
 *
 * paths (in): json paths
 * regs (in/out): resulting regex patterns
 * match_exactly (in) : whether to match whole string or to match any prefix
 *
 */
int
db_json_paths_to_regex (const std::vector<std::string> &paths, std::vector<std::string> &regs, bool match_exactly)
{
  regs.reserve (paths.size ());
  for (auto &path : paths)
    {
      std::stringstream ss;
      // match start of string
      ss << "^";
      for (size_t i = 0; i < path.length (); ++i)
	{
	  switch (path[i])
	    {
	    case '$':
	      ss << "\\$";
	      break;
	    case '[':
	      ss << "\\[";
	      break;
	    case ']':
	      ss << "]";
	      break;
	    case '.':
	      ss << "\\.";
	      break;
	    // todo: probably most of the special characters for POSIX regex language should be escaped
	    case '^':
	      ss << "\\^";
	      break;
	    case '|':
	      ss << "\\|";
	      break;
	    case '\\':
	      ss << "\\\\";
	      break;
	    case '*':
	      if (i < path.length () - 1 && path[i + 1] == '*')
		{
		  // wild_card '**'. Match any string
		  ss << ".*";
		  ++i;
		}
	      else if (i > 0 && path[i - 1] == '[')
		{
		  // wild_card '[*]'. Match numbers only
		  ss << "([0-9])+";
		}
	      else if (i > 0 && path[i - 1] == '.')
		{
		  // wild_card '.*'. Match any string between quotes (path must have been validated before)
		  // match strings between quotes that do not contain unescaped quotes
		  // todo: there are other characters that require same treatment as quotes;
		  // they can be treated by applying the same pattern
		  ss << "\"(([^\"\\])*|([\\]([\\][\\])*\")*)*\"";
		}
	      else
		{
		  // Not a wildcard '$."*"'
		  ss << "\\*";
		}
	      break;
	    default:
	      ss << path[i];
	      break;
	    }
	}
      if (!match_exactly)
	{
	  ss << "([^$])*";
	}
      // match end of string
      ss << "$";

      regs.emplace_back (ss.str ());
    }
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
 * regs (in)               : compiled regexes
 * find_all (in)           : whether we need to gather all matches
 */
int
db_json_search_func (JSON_DOC &doc, const DB_VALUE *pattern, const DB_VALUE *esc_char, std::vector<std::string> &paths,
		     const std::vector<std::string> &regs, bool find_all)
{
  std::unordered_set<std::string> paths_gathered;
  const map_func_type &f_search = [&regs, &paths, pattern, esc_char, find_all, &paths_gathered] (const JSON_VALUE &jv,
				  const std::string &crt_path, bool &stop) -> int
  {
    if (!jv.IsString ())
      {
	return NO_ERROR;
      }

    const char *json_str = jv.GetString ();
    DB_VALUE str_val;

    db_make_null (&str_val);
    int error_code = db_make_string (&str_val, (char *) json_str);
    if (error_code)
      {
	return error_code;
      }

    int match;
    error_code = db_string_like (&str_val, pattern, esc_char, &match);
    if (error_code != NO_ERROR || !match)
      {
	return error_code;
      }

    for (std::size_t i = 0; i < regs.size (); ++i)
      {
	bool path_compatible;
	int error_code = regex_matches (regs[i].c_str (), crt_path.c_str (), CUB_REG_EXTENDED, &path_compatible);
	if (error_code)
	  {
	    ASSERT_ERROR ();
	    stop = true;
	    return error_code;
	  }

	if (path_compatible && paths_gathered.find (crt_path) == paths_gathered.end ())
	  {
	    paths.push_back (crt_path);
	    paths_gathered.insert (crt_path);
	    if (!find_all)
	      {
		stop = true;
		return NO_ERROR;
	      }
	  }
      }

    return NO_ERROR;
    // no regex was matched
  };

  JSON_PATH_MAPPER json_search_walker (f_search);
  return json_search_walker.WalkDocument (doc);
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

static int
db_json_array_shift_values (const JSON_DOC *value, JSON_DOC &doc, const std::string &path)
{
  int error_code = NO_ERROR;
  JSON_VALUE *resulting_json_parent = NULL;

  error_code = db_json_resolve_json_parent (doc, path, resulting_json_parent);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  assert (resulting_json_parent != NULL);
  if (!resulting_json_parent->IsArray ())
    {
      assert (resulting_json_parent->IsObject ());
      return db_json_er_set_expected_other_type (ARG_FILE_LINE, path, DB_JSON_OBJECT, DB_JSON_ARRAY);
    }


  int last_token_index = std::stoi (path.substr (path.find_last_of ('/') + 1));

  // add the value at the end of the array
  JSON_VALUE value_copy (*value, doc.GetAllocator ());
  resulting_json_parent->GetArray ().PushBack (value_copy, doc.GetAllocator ());

  // move the value to its correct index by swapping adjacent values
  for (int i = resulting_json_parent->GetArray ().Size () - 1; i > last_token_index; --i)
    {
      resulting_json_parent->GetArray ()[i].Swap (resulting_json_parent->GetArray ()[i - 1]);
    }

  return NO_ERROR;
}

/*
 * db_json_array_insert_func () - Insert the value to the path from the indicated array within a JSON document
 *
 * return                  : error code
 * value (in)              : the value to be added in the array
 * doc (in)                : json document
 * raw_path (in)           : specified path
 */
int
db_json_array_insert_func (const JSON_DOC *value, JSON_DOC &doc, const char *raw_path)
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
  if (!p.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  JSON_VALUE *resulting_json = p.Get (doc);
  if (resulting_json != NULL)
    {
      // need to shift any following values to the right
      return db_json_array_shift_values (value, doc, json_pointer_string);
    }

  // here starts the INSERTION part
  return db_json_insert_helper (value, doc, p, json_pointer_string);
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
  else if (val->IsInt64 ())
    {
      return DB_JSON_BIGINT;
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
      else
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

JSON_DOC *db_json_allocate_doc ()
{
  JSON_DOC *doc = new JSON_DOC ();
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
  if (source.IsObject () && dest.IsObject ())
    {
      db_json_merge_two_json_objects_patch (source, dest, allocator);
    }
  else
    {
      dest.CopyFrom (source, allocator);
    }
}

/*
 * db_json_merge_func () - Merge the source json into destination json
 *
 * return                   : error code
 * dest (in)                : json where to merge
 * source (in)              : json to merge
 * patch (in)               : how to handle duplicate keys
 *
 * example                  : let x = { "a": 1, "b": 2 }
 *                                y = { "a": 3, "c": 4 }
 *                                z = { "a": 5, "d": 6 }
 *
 * result PATCH             : {"a": 5, "b": 2, "c": 4, "d": 6}
 * result PRESERVE          : {"a": [1, 3, 5], "b": 2, "c": 4, "d": 6}
 */
int
db_json_merge_func (const JSON_DOC *source, JSON_DOC *&dest, bool patch)
{
  if (dest == NULL)
    {
      dest = db_json_allocate_doc ();
      db_json_copy_doc (*dest, source);
      return NO_ERROR;
    }

  const JSON_VALUE &source_value = db_json_doc_to_value (*source);
  JSON_VALUE &dest_value = db_json_doc_to_value (*dest);

  if (patch)
    {
      db_json_merge_patch_values (source_value, dest_value, dest->GetAllocator ());
    }
  else
    {
      db_json_merge_preserve_values (source_value, dest_value, dest->GetAllocator ());
    }

  return NO_ERROR;
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

  return val->GetInt64 ();
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
      return NULL;
    }

  assert (db_json_get_type_of_value (doc) == DB_JSON_BOOL);
  return doc->GetBool ();
}

static JSON_PATH_TYPE
db_json_get_path_type (std::string &path_string)
{
  db_json_normalize_path (path_string);

  if (path_string.empty () || path_string[0] != '$')
    {
      return JSON_PATH_TYPE::JSON_PATH_POINTER;
    }
  else
    {
      return JSON_PATH_TYPE::JSON_PATH_SQL_JSON;
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
 * allow_empty (in) whether to allow empty tokens e.g. json_pointer -> json_path needs to allow empty tokens
 * returns a vector with tokens split by delimiters from the given string
 */
static std::vector<std::string>
db_json_split_path_by_delimiters (const std::string &path, const std::string &delim, bool allow_empty)
{
  std::vector<std::string> tokens;
  std::size_t start = 0;
  std::size_t end = path.find_first_of (delim, start);

  while (end != std::string::npos)
    {
      if (path[end] == '"')
	{
	  std::size_t index_of_closing_quote = path.find_first_of ('"', end + 1);
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
	  if (!substring.empty () || allow_empty)
	    {
	      tokens.push_back (substring);
	    }
	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty () || allow_empty)
    {
      tokens.push_back (substring);
    }

  std::size_t tokens_size = tokens.size ();
  for (std::size_t i = 0; i < tokens_size; i++)
    {
      if (db_json_path_is_token_valid_array_index (tokens[i], false))
	{
	  db_json_remove_leading_zeros_index (tokens[i]);
	}
    }

  return tokens;
}

/*
 * json_path_strip_whitespaces () - Remove whitespaces in json_path
 *
 * sql_path (in/out)       : json path
 * NOTE: This can be only called after validation because spaces are not allowed in some cases (e.g. $[1 1] is illegal)
 */
static void
json_path_strip_whitespaces (std::string &sql_path)
{
  std::string result;
  result.reserve (sql_path.length () + 1);

  bool skip_spaces = true;
  bool unescaped_backslash = false;
  for (size_t i = 0; i < sql_path.length (); ++i)
    {
      if (i > 0 && !unescaped_backslash && sql_path[i] == '"')
	{
	  skip_spaces = !skip_spaces;
	}

      if (sql_path[i] == '\\')
	{
	  unescaped_backslash = !unescaped_backslash;
	}
      else
	{
	  unescaped_backslash = false;
	}

      if (skip_spaces && sql_path[i] == ' ')
	{
	  continue;
	}

      result.push_back (sql_path[i]);
    }

  sql_path = std::move (result);
}

/*
* skip_whitespaces  () - Advance offset to first non_space
*
* return              : offset of first non_space character
* sql_path (in)       : path
* pos (in)            : starting position offset
*/
static std::size_t
skip_whitespaces (const std::string &path, std::size_t pos)
{
  for (; pos < path.length () && path[pos] == ' '; ++pos);
  return pos;
}

/*
 * db_json_path_is_token_valid_quoted_object_key () - Check if a quoted object_key is valid
 *
 * return                  : true/false
 * path (in)               : path to be checked
 * token_begin (in/out)    : beginning offset of the token, is replaced with beginning of the next token or path.length ()
 */
static bool
db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin)
{
  std::size_t i = token_begin + 1;
  bool unescaped_backslash = false;
  std::size_t backslash_nr = 0;
  // stop at unescaped '"'; note that there should be an odd nr of backslashes before '"' for it to be escaped
  for (; i < path.length () && (path[i] != '"' || unescaped_backslash); ++i)
    {
      if (path[i] == '\\')
	{
	  unescaped_backslash = !unescaped_backslash;
	}
      else
	{
	  unescaped_backslash = false;
	}
    }

  if (i == path.length ())
    {
      return false;
    }

  token_begin = skip_whitespaces (path, i + 1);
  return true;
}

/*
 * db_json_path_is_token_valid_unquoted_object_key () - Validate and quote an object_key
 *
 * return               : validation result
 * path (in/out)        : path to be checked
 * token_begin (in/out) : is replaced with beginning of the next token or path.length ()
 */
static bool
db_json_path_quote_and_validate_unquoted_object_key (std::string &path, std::size_t &token_begin)
{
  std::size_t i = token_begin;
  bool validation_result = db_json_path_is_token_valid_unquoted_object_key (path, i);
  if (validation_result)
    {
      // we normalize object_keys by quoting them - e.g. $.objectkey we represent as $."objectkey"
      path.insert (token_begin, "\"");
      path.insert (i + 1, "\"");

      token_begin = skip_whitespaces (path, i + 2 /* we inserted 2 quotation marks */);
    }
  return validation_result;
}

/*
 * db_json_path_is_token_valid_unquoted_object_key () - Check if an unquoted object_key is valid
 *
 * return                  : true/false
 * path (in)               : path to be checked
 * token_begin (in/out)    : beginning offset of the token, is replaced with first char's position
 *                           outside of the current valid token
 */
static bool
db_json_path_is_token_valid_unquoted_object_key (const std::string &path, std::size_t &token_begin)
{
  if (path == "")
    {
      return false;
    }
  std::size_t i = token_begin;

  // todo: this needs change. Besides alphanumerics, object keys can be valid ECMAScript identifiers as defined in
  // http://www.ecma-international.org/ecma-262/5.1/#sec-7.6
  if (i < path.length () && !std::isalpha (static_cast<unsigned char> (path[i])))
    {
      return false;
    }

  ++i;
  for (; i < path.length () && std::isalnum (static_cast<unsigned char> (path[i])); ++i);

  token_begin = i;

  return true;
}

/*
 * db_json_sql_path_is_valid () - Check if a given path is a SQL valid path
 *
 * return                  : true/false
 * sql_path (in/out)       : path to be checked
 * allow_wild_cards (in)   : whether json_path wildcards are allowed
 */
static bool
db_json_sql_path_is_valid (std::string &sql_path, bool allow_wildcards)
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
  std::size_t i = skip_whitespaces (sql_path, 1);
  while (i < sql_path.length ())
    {
      // to begin a next token we have only 3 possibilities:
      // with dot we start an object name
      // with bracket we start an index
      // with * we have the beginning of a '**' wildcard
      switch (sql_path[i])
	{
	case '[':
	{
	  std::size_t end_bracket_offset;
	  i = skip_whitespaces (sql_path, i + 1);

	  end_bracket_offset = sql_path.find_first_of (']', i);
	  if (end_bracket_offset == std::string::npos)
	    {
	      return false;
	    }
	  if (!db_json_path_is_token_valid_array_index (sql_path, allow_wildcards, i, end_bracket_offset))
	    {
	      return false;
	    }
	  i = skip_whitespaces (sql_path, end_bracket_offset + 1);
	  break;
	}
	case '.':
	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length ())
	    {
	      return false;
	    }
	  switch (sql_path[i])
	    {
	    case '"':
	      if (!db_json_path_is_token_valid_quoted_object_key (sql_path, i))
		{
		  return false;
		}
	      break;
	    case '*':
	      if (!allow_wildcards)
		{
		  return false;
		}
	      i = skip_whitespaces (sql_path, i + 1);
	      break;
	    default:
	      // unquoted object_keys
	      if (!db_json_path_quote_and_validate_unquoted_object_key (sql_path, i))
		{
		  return false;
		}
	      break;
	    }
	  break;

	case '*':
	  // only ** wildcard is allowed in this case
	  if (!allow_wildcards || ++i >= sql_path.length () || sql_path[i] != '*')
	    {
	      return false;
	    }

	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length ())
	    {
	      // ** wildcard requires suffix
	      return false;
	    }
	  break;

	default:
	  return false;
	}
    }
  json_path_strip_whitespaces (sql_path);
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
  cubmem::private_unique_ptr<char> unique_ptr (raw_json_body, NULL);

  er_set (ER_ERROR_SEVERITY, file_name, line_no, ER_JSON_PATH_DOES_NOT_EXIST, 2,
	  sql_path_string.c_str (), raw_json_body);

  return ER_JSON_PATH_DOES_NOT_EXIST;
}

static int
db_json_er_set_expected_other_type (const char *file_name, const int line_no, const std::string &path,
				    const DB_JSON_TYPE &found_type, const DB_JSON_TYPE &expected_type,
				    const DB_JSON_TYPE &expected_type_optional)
{
  std::string sql_path_string;
  int error_code = NO_ERROR;

  // the path must be SQL path
  error_code = db_json_convert_pointer_to_sql_path (path.c_str (), sql_path_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
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
	  sql_path_string.c_str (), expected_type_str.c_str (), found_type_str);

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
  size_t start = 0;
  size_t end = 0;
  size_t step = 1;

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
	      if (token.compare (token_idx, special_it->first.length (), special_it->first) == 0)
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

      start += step;
    }
}

/*
 * db_json_convert_pointer_to_sql_path ()
 * pointer_path (in)
 * sql_path_out (out): the result
 * allow_wildcards (in):
 * A pointer path is converted to SQL standard path
 * Example: /0/name1/name2/2 -> $[0]."name1"."name2"[2]
 */
int
db_json_convert_pointer_to_sql_path (const char *pointer_path, std::string &sql_path_out, bool allow_wildcards)
{
  std::string pointer_path_string (pointer_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (pointer_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      // path is not JSON path format; consider it SQL path.
      sql_path_out = pointer_path_string;
      if (!db_json_sql_path_is_valid (sql_path_out, allow_wildcards))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
      return NO_ERROR;
    }

  std::unordered_map<std::string, std::string> special_chars;
  sql_path_out = "$";

  db_json_build_path_special_chars (json_path_type, special_chars);

  // starting the conversion of path
  // first we need to split into tokens
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (pointer_path_string, db_Json_pointer_delimiters,
				    true);

  for (std::size_t i = 0; i < tokens.size (); ++i)
    {
      if (i == 0 )
	{
	  // todo: can special json pointer characters be present in the first token?
	  if (!tokens[0].empty ())
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }
	  else
	    {
	      continue;
	    }
	}

      if (db_json_path_is_token_valid_array_index (tokens[i], false))
	{
	  sql_path_out += "[";
	  sql_path_out += tokens[i];
	  sql_path_out += "]";
	}
      else
	{
	  db_json_replace_token_special_chars (tokens[i], special_chars);
	  char *quoted_token;
	  int quoted_size;
	  db_string_escape (tokens[i].c_str (), tokens[i].size (), &quoted_token, &quoted_size);
	  tokens[i].resize (quoted_size);
	  tokens[i].assign (quoted_token, quoted_size - 1);
	  db_private_free (NULL, quoted_token);

	  std::size_t token_pos = 0;
	  // todo: clarify escaping things e.g. '"' character in the token needs to be escaped for sure
	  if (!db_json_path_is_token_valid_quoted_object_key (tokens[i], token_pos))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }

	  sql_path_out += ".";
	  // replace special characters if necessary based on mapper
	  sql_path_out += tokens[i];
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

/*
 * db_json_remove_leading_zeros_index () - Erase leading zeros from sql path index
 *
 * index (in)                : current object
 * example: $[000123] -> $[123]
 */
static void
db_json_remove_leading_zeros_index (std::string &index)
{
  // trim leading zeros
  auto first_non_zero = std::find_if_not (index.begin (), index.end (), db_json_iszero);
  index.erase (index.begin (), first_non_zero);

  if (index.empty ())
    {
      index = "0";
    }
}

/*
 * db_json_convert_sql_path_to_pointer ()
 *
 * sql_path (in)
 * json_pointer_out (out): the result
 * An sql_path is converted to rapidjson standard path
 * Example: $[0]."name1".name2[2] -> /0/name1/name2/2
 */
int
db_json_convert_sql_path_to_pointer (const char *sql_path, std::string &json_pointer_out, bool allow_wildcards)
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

  if (!db_json_sql_path_is_valid (sql_path_string, allow_wildcards))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  std::unordered_map<std::string, std::string> special_chars;

  db_json_build_path_special_chars (json_path_type, special_chars);

  // first we need to split into tokens
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (sql_path_string, db_Json_sql_path_delimiters,
				    false);

  // build json pointer
  json_pointer_out = "";
  for (unsigned int i = 0; i < tokens.size (); ++i)
    {
      db_json_replace_token_special_chars (tokens[i], special_chars);
      json_pointer_out += "/" + tokens[i];
    }

  return NO_ERROR;
}

/*
 * db_json_path_unquote_object_keys () - Unquote, when possible, object_keys of the json_path
 *
 * sql_path (in/out)       : path
 */
void
db_json_path_unquote_object_keys (std::string &sql_path)
{
  // note: sql_path should not have wildcards, it comes as output of json_search function
  auto tokens = db_json_split_path_by_delimiters (sql_path, ".[", false);
  std::size_t crt_idx = 0;
  std::string res = "$";

  assert (!tokens.empty () && tokens[0] == "$");
  for (std::size_t i = 1; i < tokens.size (); ++i)
    {
      if (tokens[i][0] == '"')
	{
	  res += ".";
	  std::string unquoted = tokens[i].substr (1, tokens[i].length () - 2);
	  std::size_t start = 0;

	  if (db_json_path_is_token_valid_unquoted_object_key (unquoted, start) && start >= unquoted.length ())
	    {
	      res.append (unquoted);
	    }
	  else
	    {
	      res += tokens[i];
	    }
	}
      else
	{
	  res += "[";
	  res += tokens[i];
	}
    }

  sql_path = std::move (res);
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
  JSON_POINTER p ("");
  const JSON_VALUE *head = p.Get (doc);
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

void db_json_set_bigint_to_doc (JSON_DOC *doc, std::int64_t i)
{
  doc->SetInt64 (i);
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

//
// db_value_to_json_path () - get path string from value; if value is not a string, an error is returned
//
// return          : error code
// path_value (in) : path value
// fcode (in)      : JSON function (for verbose error)
// path_str (out)  : path string
//
int
db_value_to_json_path (const DB_VALUE *path_value, FUNC_TYPE fcode, const char **path_str)
{
  if (!TP_IS_CHAR_TYPE (db_value_domain_type (path_value)))
    {
      int error_code = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 2, fcode_get_uppercase_name (fcode), "STRING");
      return error_code;
    }
  *path_str = db_get_string (path_value);
  return NO_ERROR;
}

/* db_value_to_json_doc - create a JSON_DOC from db_value.
 *
 * return     : error code
 * db_val(in) : input db_value
 * json_doc(out) : output JSON_DOC pointer
 *
 * TODO: sometimes copying a JSON document might not be necessary.
 */
int
db_value_to_json_doc (const DB_VALUE &db_val, REFPTR (JSON_DOC, json_doc))
{
  int error_code = NO_ERROR;

  if (db_value_is_null (&db_val))
    {
      json_doc = db_json_allocate_doc ();
      db_json_make_document_null (json_doc);
      return NO_ERROR;
    }

  json_doc = NULL;
  switch (db_value_domain_type (&db_val))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      error_code = db_json_get_json_from_str (db_get_string (&db_val), json_doc, db_get_string_size (&db_val));
      if (error_code != NO_ERROR)
	{
	  assert (json_doc == NULL);
	  ASSERT_ERROR ();
	}
      return error_code;

    case DB_TYPE_JSON:
      json_doc = db_json_get_copy_of_doc (db_val.data.json.document);
      return NO_ERROR;

    case DB_TYPE_NULL:
      json_doc = db_json_allocate_doc ();
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
db_value_to_json_value (const DB_VALUE &db_val, REFPTR (JSON_DOC, json_val))
{
  json_val = NULL;

  if (db_value_is_null (&db_val))
    {
      json_val = db_json_allocate_doc ();
      db_json_make_document_null (json_val);
      return NO_ERROR;
    }

  switch (DB_VALUE_DOMAIN_TYPE (&db_val))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      json_val = db_json_allocate_doc ();
      db_json_set_string_to_doc (json_val, db_get_string (&db_val));
      break;

    case DB_TYPE_ENUMERATION:
      json_val = db_json_allocate_doc ();
      {
	std::string enum_str;
	enum_str.append (db_get_enum_string (&db_val), (size_t) db_get_enum_string_size (&db_val));
	db_json_set_string_to_doc (json_val, enum_str.c_str ());
      }
      break;

    default:
      DB_VALUE dest;
      TP_DOMAIN_STATUS status = tp_value_cast (&db_val, &dest, &tp_Json_domain, false);
      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
	  return ER_QSTR_INVALID_DATA_TYPE;
	}

      json_val = db_get_json_document (&dest);
    }

  return NO_ERROR;
}

/*
 * db_json_path_is_token_valid_array_index () - verify if token is a valid array index. token can be a substring of
 *                                              first argument (by default the entire argument).
 *
 * return          : true if all token characters are digits followed by spaces (valid index)
 * str (in)        : token or the string that token belong to
 * allow_wildcards : whether json_path wildcards are allowed
 * start (in)      : start of token; default is start of string
 * end (in)        : end of token; default is end of string; 0 is considered default value
 */
static bool
db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards, std::size_t start,
    std::size_t end)
{
  // json pointer will corespond the symbol '-' to JSON_ARRAY length
  // so if we have the json {"A":[1,2,3]} and the path /A/-
  // this will point to the 4th element of the array (zero indexed)
  if (str == "-")
    {
      return true;
    }

  if (end == 0)
    {
      // default is end of string
      end = str.length ();
    }

  if (start == end)
    {
      return false;
    }

  std::size_t last_non_space = end - 1;
  for (; last_non_space > start && str[last_non_space] == ' '; --last_non_space);
  if (allow_wildcards && start == last_non_space && str[start] == '*')
    {
      return true;
    }

  ++last_non_space;
  for (auto it = str.cbegin () + start; it < str.cbegin () + last_non_space; ++it)
    {
      if (!std::isdigit (static_cast<unsigned char> (*it)))
	{
	  return false;
	}
    }

  // this is a valid array index
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
  m_stop = false;
  return WalkValue (db_json_doc_to_value (document));
}

int
JSON_WALKER::WalkValue (JSON_VALUE &value)
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
      for (JSON_VALUE *it = value.Begin (); it != value.End (); ++it)
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

bool JSON_SERIALIZER::PackType (const DB_JSON_TYPE &type)
{
  m_error = or_put_int (m_buffer, static_cast<int> (type));
  return !HasError ();
}

bool JSON_SERIALIZER::PackString (const char *str)
{
  m_error = or_put_string_aligned_with_length (m_buffer, str);
  return !HasError ();
}

bool JSON_SERIALIZER_LENGTH::Null ()
{
  m_length += GetTypePackedSize ();
  return true;
}

bool JSON_SERIALIZER::Null ()
{
  return PackType (DB_JSON_NULL);
}

bool JSON_SERIALIZER_LENGTH::Bool (bool b)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int (0 or 1)
  m_length += GetTypePackedSize () + OR_INT_SIZE;
  return true;
}

bool JSON_SERIALIZER::Bool (bool b)
{
  if (!PackType (DB_JSON_BOOL))
    {
      return false;
    }

  m_error = or_put_int (m_buffer, b ? 1 : 0);
  return !HasError ();
}

bool JSON_SERIALIZER_LENGTH::Int (int i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_INT_SIZE;
  return true;
}

bool JSON_SERIALIZER::Int (int i)
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

bool JSON_SERIALIZER_LENGTH::Uint (unsigned i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_INT_SIZE;
  return true;
}

bool JSON_SERIALIZER::Uint (unsigned i)
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

bool JSON_SERIALIZER_LENGTH::Int64 (std::int64_t i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int64
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_BIGINT_SIZE;
  return true;
}

bool JSON_SERIALIZER::Int64 (std::int64_t i)
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

bool JSON_SERIALIZER_LENGTH::Uint64 (std::uint64_t i)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is int64
  m_length += GetTypePackedSize () + OR_INT_SIZE + OR_BIGINT_SIZE;
  return true;
}

bool JSON_SERIALIZER::Uint64 (std::uint64_t i)
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

bool JSON_SERIALIZER_LENGTH::Double (double d)
{
  // the encode will be TYPE|VALUE, where TYPE is int and value is double
  m_length += GetTypePackedSize () + OR_DOUBLE_SIZE;
  return true;
}

bool JSON_SERIALIZER::Double (double d)
{
  if (!PackType (DB_JSON_DOUBLE))
    {
      return false;
    }

  m_error = or_put_double (m_buffer, d);
  return !HasError ();
}

bool JSON_SERIALIZER_LENGTH::String (const Ch *str, SizeType length, bool copy)
{
  m_length += GetTypePackedSize () + GetStringPackedSize (str);
  return true;
}

bool JSON_SERIALIZER::String (const Ch *str, SizeType length, bool copy)
{
  return PackType (DB_JSON_STRING) && PackString (str);
}

bool JSON_SERIALIZER_LENGTH::Key (const Ch *str, SizeType length, bool copy)
{
  // we encode directly the key because we know we are dealing with object
  m_length += GetStringPackedSize (str);
  return true;
}

bool JSON_SERIALIZER::Key (const Ch *str, SizeType length, bool copy)
{
  return PackString (str);
}

bool JSON_SERIALIZER_LENGTH::StartObject ()
{
  m_length += GetTypePackedSize ();
  m_length += OR_INT_SIZE;
  return true;
}

bool JSON_SERIALIZER::StartObject ()
{
  if (!PackType (DB_JSON_OBJECT))
    {
      return false;
    }

  // add pointer to stack, because we need to come back to overwrite this pointer with the correct size
  // we will know that in EndObject function
  return SaveSizePointers (m_buffer->ptr);
}

bool JSON_SERIALIZER_LENGTH::StartArray ()
{
  m_length += GetTypePackedSize ();
  m_length += OR_INT_SIZE;
  return true;
}

bool JSON_SERIALIZER::StartArray ()
{
  if (!PackType (DB_JSON_ARRAY))
    {
      return false;
    }

  // add pointer to stack, because we need to come back to overwrite this pointer with the correct size
  // we will know that in EndObject function
  return SaveSizePointers (m_buffer->ptr);
}

bool JSON_SERIALIZER_LENGTH::EndObject (SizeType memberCount)
{
  return true;
}

bool JSON_SERIALIZER::EndObject (SizeType memberCount)
{
  // overwrite the count
  SetSizePointers (memberCount);
  return true;
}

bool JSON_SERIALIZER_LENGTH::EndArray (SizeType elementCount)
{
  return true;
}

bool JSON_SERIALIZER::EndArray (SizeType elementCount)
{
  // overwrite the count
  SetSizePointers (elementCount);
  return true;
}

void JSON_PRETTY_WRITER::WriteDelimiters (bool is_key)
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

void JSON_PRETTY_WRITER::PushLevel (const DB_JSON_TYPE &type)
{
  // advance one level
  m_current_indent += LEVEL_INDENT_UNIT;

  // push the new context
  m_level_stack.push (level_context (type, true));
}

void JSON_PRETTY_WRITER::PopLevel ()
{
  // reestablish the old context
  m_current_indent -= LEVEL_INDENT_UNIT;
  m_level_stack.pop ();
}

void JSON_PRETTY_WRITER::SetIndentOnNewLine ()
{
  m_buffer.append ("\n").append (m_current_indent, ' ');
}

bool JSON_PRETTY_WRITER::Null ()
{
  WriteDelimiters ();

  m_buffer.append ("NULL");

  return true;
}

bool JSON_PRETTY_WRITER::Bool (bool b)
{
  WriteDelimiters ();

  m_buffer.append (b ? "true" : "false");

  return true;
}

bool JSON_PRETTY_WRITER::Int (int i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool JSON_PRETTY_WRITER::Uint (unsigned i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool JSON_PRETTY_WRITER::Int64 (std::int64_t i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool JSON_PRETTY_WRITER::Uint64 (std::uint64_t i)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (i));

  return true;
}

bool JSON_PRETTY_WRITER::Double (double d)
{
  WriteDelimiters ();

  m_buffer.append (std::to_string (d));

  return true;
}

bool JSON_PRETTY_WRITER::String (const Ch *str, SizeType length, bool copy)
{
  WriteDelimiters ();

  m_buffer.append ("\"").append (str).append ("\"");

  return true;
}

bool JSON_PRETTY_WRITER::StartObject ()
{
  WriteDelimiters ();

  m_buffer.append ("{");

  PushLevel (DB_JSON_TYPE::DB_JSON_OBJECT);

  return true;
}

bool JSON_PRETTY_WRITER::Key (const Ch *str, SizeType length, bool copy)
{
  WriteDelimiters (true);

  m_buffer.append ("\"").append (str).append ("\"").append (":");

  return true;
}

bool JSON_PRETTY_WRITER::StartArray ()
{
  WriteDelimiters ();

  m_buffer.append ("[");

  PushLevel (DB_JSON_TYPE::DB_JSON_ARRAY);

  return true;
}

bool JSON_PRETTY_WRITER::EndObject (SizeType memberCount)
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

bool JSON_PRETTY_WRITER::EndArray (SizeType elementCount)
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
db_json_serialize (const JSON_DOC &doc, OR_BUF &buffer)
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
db_json_or_buf_underflow (OR_BUF *buf, size_t length)
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
