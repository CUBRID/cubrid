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
    void note_executed (INT64 token);
    int consume (THREAD_ENTRY *thread_p, INT64 token, const OID *class_oid, DB_TYPE expected_type,
		 INTERNAL_LOB_LOCATOR &locator);

  private:
    struct payload
    {
      DB_TYPE type = DB_TYPE_NULL;
      DB_BIGINT data_length = 0;
      DB_BIGINT logical_length = 0;
      DB_BIGINT received = 0;
      DB_BIGINT base = 0;	/* offset of this payload in m_spool */
      bool complete = false;
      bool consumed = false;	/* stored at least once, or bound by a statement that ran; kept alive for the
				 * rest of the statement */
      int use_count = 0;	/* consume () calls currently reading this payload (a reused token may be
				 * consumed by more than one row at once) */
      bool pending_erase = false;	/* purge_consumed () or abort () arrived while use_count > 0; the
					 * erase is deferred to the moment the last consume () finishes */
    };
    using payload_map = std::unordered_map<INT64, payload>;

    /* Drops one payload; once none is left the spool is emptied so its space is given back.  Caller holds
     * m_mutex. */
    payload_map::iterator erase (payload_map::iterator it);
    /* Releases payloads already stored by an earlier statement.  Called when a new upload starts, which is the
     * first moment we know the previous one can no longer be bound again. */
    void purge_consumed ();

    std::mutex m_mutex;
    INT64 m_next_token;
    payload_map m_payloads;
    /* All payloads of the session share this one staging file (one per payload could exhaust the descriptor
     * limit), each in its own [base, base + received) range.  Uploads of one session are serialized, so a new
     * payload always starts at m_spool_end. */
    FILE *m_spool;
    DB_BIGINT m_spool_end;
};

#endif /* _INTERNAL_LOB_UPLOAD_HPP_ */
