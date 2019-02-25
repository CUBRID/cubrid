#ifndef _DB_JSON_TYPES_INTERNAL_HPP
#define _DB_JSON_TYPES_INTERNAL_HPP

#include "error_manager.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "db_json.hpp"

#include "rapidjson/allocators.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <string>
#include <vector>

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
typedef rapidjson::GenericPointer <JSON_VALUE>::Token TOKEN;
typedef rapidjson::GenericStringBuffer<JSON_ENCODING, JSON_PRIVATE_ALLOCATOR> JSON_STRING_BUFFER;
typedef rapidjson::GenericMemberIterator<true, JSON_ENCODING, JSON_PRIVATE_MEMPOOL>::Iterator JSON_MEMBER_ITERATOR;
typedef rapidjson::GenericArray<true, JSON_VALUE>::ConstValueIterator JSON_VALUE_ITERATOR;

static std::vector<std::pair<std::string, std::string>> uri_fragment_conversions =
{
  std::make_pair ("~", "~0"),
  std::make_pair ("/", "~1")
};

static const char *db_Json_sql_path_delimiters = "$.[]\"";

static const rapidjson::SizeType kPointerInvalidIndex = rapidjson::kPointerInvalidIndex;

class JSON_DOC : public rapidjson::GenericDocument <JSON_ENCODING, JSON_PRIVATE_MEMPOOL>
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

DB_JSON_TYPE
db_json_get_type_of_value (const JSON_VALUE *val);

#endif // !_DB_JSON_TYPES_INTERNAL_HPP
