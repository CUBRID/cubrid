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

/*
 * copy_session.hpp - Server-side COPY FROM STDIN session
 */

#ifndef _COPY_SESSION_HPP_
#define _COPY_SESSION_HPP_

#include "copy_decode_common.hpp"
#include "dbtype_def.h"
#include "heap_file.h"
#include "oid.h"
#include "record_descriptor.hpp"
#include "thread_compat.hpp"
#include "copy_stream_kind.h"
#include "stream_session.hpp"

#include <cstdint>
#include <string>
#include <vector>

typedef struct UUID_STATE UUID_STATE;

class copy_session : public stream_session
{
  public:
    copy_session ();
    ~copy_session () override;

    /* binding-specific open (not part of the stream_session seam) */
    int init (THREAD_ENTRY *thread_p, const OID *class_oid, const COPY_CONFIG &config);

    /* stream_session seam */
    int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override;
    int finish (THREAD_ENTRY *thread_p, stream_result *result) override;
    void abort (THREAD_ENTRY *thread_p) override;

  private:
    int flush_batch (THREAD_ENTRY *thread_p);	/* flush m_recdes_collected to the heap */
    int reject_row (DB_VALUE *vals, std::int64_t row, int col, const char *reason);	/* ON_ERROR IGNORE */
    int format_error (const char *reason, std::int64_t row = -1, int col = -1) const;
    /* the defaults of m_omitted_pos that hold for the whole COPY, once a chunk */
    int eval_statement_defaults (THREAD_ENTRY *thread_p, HEAP_CACHE_ATTRINFO &attrinfo,
				 std::vector<DB_VALUE> &defaults);
    /* sets every default of m_omitted_pos into a row, making the per-row ones */
    int set_defaults (HEAP_CACHE_ATTRINFO &attrinfo, std::vector<DB_VALUE> &defaults, UUID_STATE *uuid_state);
    int carry (const char *from, std::size_t len);

    OID m_class_oid;
    HFID m_hfid;

    std::vector<DB_TYPE> m_col_types;
    /* per-column target domain, captured at open (see COPY_COL_DOMAIN) */
    std::vector<COPY_COL_DOMAIN> m_col_domains;
    std::vector<ATTR_ID> m_attr_ids;	/* attribute repr IDs in column order */
    std::vector<char> m_col_notnull;	/* column is NOT NULL, in column order */
    int m_auto_incr_pos;		/* the AUTO_INCREMENT attribute in the representation, or -1 */
    int m_auto_incr_col;		/* its column in this COPY, or -1 when the column list leaves it out */
    char m_auto_incr_serial_name[DB_MAX_IDENTIFIER_LENGTH];	/* heap_set_autoincrement_value () fills it once */
    std::vector<int> m_omitted_pos;	/* attributes the column list leaves out, but the AUTO_INCREMENT one */
    DB_DATETIME m_sys_datetime;		/* the statement's time, which those defaults take */
    DB_TIMESTAMP m_sys_epochtime;
    UINT64 m_uuid_last_ms;		/* UUID (7)'s state across the chunks, which other threads may serve */
    UINT8 m_uuid_seq;
    std::vector<char> m_leftover;		/* bytes at the tail of a receive_data
					   call that form a partial row and need
					   to be combined with the next chunk */
    int m_num_cols;
    int m_format;			/* COPY_FORMAT_BINARY | COPY_FORMAT_CSV */
    char m_delimiter;			/* CSV field delimiter (default ',') */
    char m_quote;			/* CSV quote char (default '"') */
    std::string m_null_str;		/* CSV: the unquoted field that reads as NULL (default empty) */
    bool m_on_error_ignore;		/* CSV: skip a row in error instead of failing (ON_ERROR IGNORE) */
    std::int64_t m_reject_limit;	/* the most rows ON_ERROR IGNORE may skip; 0 for no limit */
    std::int64_t m_rows_rejected;	/* rows skipped so far; COPY_REJECTED_ROWS () reports it */
    bool m_skip_header;			/* CSV: a leading header line is still to be skipped */
    bool m_bulk;			/* bulk-load mode requested (BU_LOCK acquired at open) */
    bool m_footer_seen;			/* BINARY: the end-of-data marker has arrived */
    std::int64_t m_rows_loaded;	/* what finish () reports; a stream is not bounded by int */
    std::int64_t m_row_number;		/* rows seen, decoded or not: what an error reports */

    /* rows packed and queued for batch insert (flushed every COPY_FLUSH_BATCH_ROWS) */
    std::vector<record_descriptor> m_recdes_collected;

    /* reused per-row scratch for CSV field strings; keeps VARCHAR bytes alive
     * through the row insert (out_vals point into it) */
    std::vector<std::string> m_csv_fields;
    std::vector<char> m_csv_quoted;
};

#endif /* _COPY_SESSION_HPP_ */
