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

#ifndef _INTERNAL_LOB_STREAM_SESSION_HPP_
#define _INTERNAL_LOB_STREAM_SESSION_HPP_

#include "stream_session.hpp"
#include "dbtype_def.h"

class internal_lob_stream_session : public stream_session
{
  public:
    internal_lob_stream_session ();
    ~internal_lob_stream_session () override;

    int init (THREAD_ENTRY *thread_p, DB_TYPE type, DB_BIGINT data_length, DB_BIGINT logical_length);
    int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override;
    int finish (THREAD_ENTRY *thread_p, stream_result *result) override;
    void abort (THREAD_ENTRY *thread_p) override;

  private:
    INT64 m_token;
    bool m_active;
};

#endif /* _INTERNAL_LOB_STREAM_SESSION_HPP_ */
