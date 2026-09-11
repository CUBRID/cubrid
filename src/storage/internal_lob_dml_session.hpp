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

#ifndef _INTERNAL_LOB_DML_SESSION_HPP_
#define _INTERNAL_LOB_DML_SESSION_HPP_

#include "internal_lob_file.hpp"
#include "internal_lob_dml_protocol.hpp"
#include "log_lsa.hpp"
#include "stream_session.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <vector>

/* SEND_DATA frame: fixed slot id + absolute byte offset followed by opaque LOB bytes. */
#define INTERNAL_LOB_DML_FRAME_HEADER_SIZE (OR_INT_SIZE + OR_INT64_SIZE)

class internal_lob_dml_context
{
  public:
    virtual ~internal_lob_dml_context () {}

    virtual int receive_lob_chunk (THREAD_ENTRY *thread_p, int slot, DB_BIGINT offset, const char *data,
				   int data_len) = 0;
    virtual int consume_lob_slot (THREAD_ENTRY *thread_p, int slot, const OID *class_oid, DB_TYPE expected_type,
				  INTERNAL_LOB_LOCATOR &locator) = 0;
    virtual int finish_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows) = 0;
    virtual void abort_dml (THREAD_ENTRY *thread_p) = 0;
};

/*
 * Reusable payload owner for DML execution contexts.
 *
 * Payloads stay private to the active stream session. finish_dml() validates
 * every slot and invokes execute_dml() while the session is still installed in
 * SESSION_STATE. Heap insertion can therefore resolve a DML-slot marker by
 * rewinding this context directly; no token registry or second DML is needed.
 *
 * A slot whose target class is not known up front (a multi-table UPDATE, where the
 * value could land in any of several classes and therefore in any of their internal
 * LOB files) cannot be written straight to storage as it arrives.  Such a slot is
 * spooled to a temp file instead, so server memory stays bounded by one chunk no
 * matter how large the value is; the file is read back once the target class is known.
 */
class internal_lob_dml_staging_context : public internal_lob_dml_context
{
  public:
    explicit internal_lob_dml_staging_context (const std::vector<internal_lob_dml_slot_config> &slot_configs);
    ~internal_lob_dml_staging_context () override;

    int init (THREAD_ENTRY *thread_p);
    int receive_lob_chunk (THREAD_ENTRY *thread_p, int slot, DB_BIGINT offset, const char *data,
			   int data_len) override;
    int consume_lob_slot (THREAD_ENTRY *thread_p, int slot, const OID *class_oid, DB_TYPE expected_type,
			  INTERNAL_LOB_LOCATOR &locator) override;
    int finish_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows) override;
    void abort_dml (THREAD_ENTRY *thread_p) override;

  protected:
    virtual int execute_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows) = 0;

  private:
    struct slot_state
    {
      internal_lob_dml_slot_config config;
      FILE *stage = NULL;		/* non-direct staging: the payload is spooled to a temp file */
      DB_BIGINT received = 0;
      DB_BIGINT read_offset = 0;
      INTERNAL_LOB_REVERSE_WRITER direct_writer;
      std::vector<OID> direct_oids;
      std::vector<LOG_LSA> direct_lsas;
      bool direct_enabled = false;
      bool direct_adopted = false;
    };

    static int read_slot (void *ctx, DB_BIGINT offset, char *buffer, int size);
    int validate_slot (const slot_state &slot) const;
    int initialize_direct_slot (THREAD_ENTRY *thread_p, slot_state &slot);
    int finalize_direct_slot (THREAD_ENTRY *thread_p, slot_state &slot);
    int capture_direct_tracking (THREAD_ENTRY *thread_p, slot_state &slot, std::size_t oid_start,
				 std::size_t lsa_start);
    int restore_direct_tracking (THREAD_ENTRY *thread_p, slot_state &slot);
    int delete_unadopted_direct_slots (THREAD_ENTRY *thread_p);
    void rollback_direct_writes (THREAD_ENTRY *thread_p);
    void clear ();

    std::vector<slot_state> m_slots;
    bool m_initialized;
    bool m_finished;
    bool m_has_direct_slots;
    bool m_savepoint_started;
    LOG_LSA m_savepoint_lsa;
    char m_savepoint_name[64];
};

/*
 * Server-owned COPY-style execution session for Internal LOB DML.
 *
 * The context owns the prepared INSERT/UPDATE execution state and all LOB
 * writers. The stream wrapper only validates chunk framing and guarantees that
 * END completes the DML instead of returning an upload token.
 */
class internal_lob_dml_session : public stream_session
{
  public:
    internal_lob_dml_session ();
    ~internal_lob_dml_session () override;

    int init (internal_lob_dml_context *context, int slot_count);
    int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override;
    int consume_lob_slot (THREAD_ENTRY *thread_p, int slot, const OID *class_oid, DB_TYPE expected_type,
			  INTERNAL_LOB_LOCATOR &locator);
    int finish (THREAD_ENTRY *thread_p, stream_result *result) override;
    void abort (THREAD_ENTRY *thread_p) override;

  private:
    internal_lob_dml_context *m_context;
    int m_slot_count;
    bool m_active;
};

#endif /* _INTERNAL_LOB_DML_SESSION_HPP_ */
