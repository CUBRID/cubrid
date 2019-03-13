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

#ifndef _DB_JSON_TYPES_INTERNAL_HPP
#define _DB_JSON_TYPES_INTERNAL_HPP

#include "db_json_allocator.hpp"

#include "pragma_push.h"
#include "pragma_supress.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "pragma_pop.h"

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
