/*
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
 * copy_session.hpp - Server-side COPY FROM STDIN session
 */

#ifndef _COPY_SESSION_HPP_
#define _COPY_SESSION_HPP_

#include "dbtype_def.h"
#include "heap_file.h"
#include "oid.h"
#include "thread_compat.hpp"
#include "stream_session.hpp"

#include <string>
#include <vector>

/* COPY row formats (mirror PT_COPY_INFO.format: 0 = BINARY, 1 = CSV) */
#define COPY_FORMAT_BINARY 0
#define COPY_FORMAT_CSV    1

class copy_session : public stream_session
{
  public:
    copy_session ();
    ~copy_session () override;

    /* binding-specific open (not part of the stream_session seam) */
    int init (THREAD_ENTRY *thread_p, const OID *class_oid, const DB_TYPE *col_types, int num_cols, int format);

    /* stream_session seam */
    int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override;
    int finish (THREAD_ENTRY *thread_p, stream_result *result) override;
    void abort (THREAD_ENTRY *thread_p) override;

  private:
    OID m_class_oid;
    HFID m_hfid;

    std::vector<DB_TYPE> m_col_types;
    std::vector<ATTR_ID> m_attr_ids;	/* attribute repr IDs in column order */
    std::vector<char> m_leftover;		/* bytes at the tail of a receive_data
					   call that form a partial row and need
					   to be combined with the next chunk */
    int m_num_cols;
    int m_format;			/* COPY_FORMAT_BINARY | COPY_FORMAT_CSV */
    int m_rows_loaded;

    /* reused per-row scratch for CSV field strings; keeps VARCHAR bytes alive
     * through the row insert (out_vals point into it) */
    std::vector<std::string> m_csv_fields;
    std::vector<char> m_csv_quoted;
};

#endif /* _COPY_SESSION_HPP_ */
