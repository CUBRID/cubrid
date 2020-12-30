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

#ifndef _DB_JSON_TYPES_INTERNAL_HPP
#define _DB_JSON_TYPES_INTERNAL_HPP

#include "db_json_allocator.hpp"
#include "db_rapidjson.hpp"

#if defined GetObject
/* stupid windows and their definitions; GetObject is defined as GetObjectW or GetObjectA */
#undef GetObject
#endif /* defined GetObject */

typedef rapidjson::UTF8<> JSON_ENCODING;
typedef rapidjson::GenericValue<JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_VALUE;

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

JSON_VALUE &db_json_doc_to_value (JSON_DOC &doc);
const JSON_VALUE &db_json_doc_to_value (const JSON_DOC &doc);

#endif // !_DB_JSON_TYPES_INTERNAL_HPP
