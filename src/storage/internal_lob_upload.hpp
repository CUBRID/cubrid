/*
 *
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

#ifndef _INTERNAL_LOB_UPLOAD_HPP_
#define _INTERNAL_LOB_UPLOAD_HPP_

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <cstdio>
#include <mutex>
#include <unordered_map>

struct internal_lob_locator;
using INTERNAL_LOB_LOCATOR = struct internal_lob_locator;

class internal_lob_upload_store
{
  public:
    internal_lob_upload_store ();
    ~internal_lob_upload_store ();

    int begin (DB_TYPE type, DB_BIGINT data_length, DB_BIGINT logical_length, INT64 &token);
    int append (INT64 token, const char *data, int data_size);
    int end (INT64 token);
    int abort (INT64 token);
    int consume (THREAD_ENTRY *thread_p, INT64 token, const OID *class_oid, DB_TYPE expected_type,
		 INTERNAL_LOB_LOCATOR &locator);

  private:
    struct payload
    {
      DB_TYPE type = DB_TYPE_NULL;
      DB_BIGINT data_length = 0;
      DB_BIGINT logical_length = 0;
      DB_BIGINT received = 0;
      bool complete = false;
      FILE *file = NULL;
    };

    void clear (payload &entry);

    std::mutex m_mutex;
    INT64 m_next_token;
    std::unordered_map<INT64, payload> m_payloads;
};

#endif /* _INTERNAL_LOB_UPLOAD_HPP_ */
