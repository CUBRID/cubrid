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

#include "config.h"

#include "internal_lob_dml_session.hpp"

#include "error_manager.h"
#include "heap_file.h"
#include "log_comm.h"
#include "log_impl.h"
#include "object_representation.h"
#include "xserver_interface.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int
internal_lob_dml_session_set_error (const char *reason)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
  return ER_STREAM_SESSION_ERROR;
}

/*
 * internal_lob_dml_session_out_of_memory () - Report a std::bad_alloc escaping the std containers below.
 *   The session objects are driven from C request handlers; an exception crossing that boundary would
 *   terminate the server, so every public entry point converts it to ER_OUT_OF_VIRTUAL_MEMORY here.
 */
static int
internal_lob_dml_session_out_of_memory (std::size_t size)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
  return ER_OUT_OF_VIRTUAL_MEMORY;
}

internal_lob_dml_staging_context::internal_lob_dml_staging_context (
	const std::vector<internal_lob_dml_slot_config> &slot_configs)
  : m_initialized (false)
  , m_finished (false)
  , m_has_direct_slots (false)
  , m_savepoint_started (false)
{
  LSA_SET_NULL (&m_savepoint_lsa);
  m_savepoint_name[0] = '\0';
  m_slots.reserve (slot_configs.size ());
  for (const internal_lob_dml_slot_config &config : slot_configs)
    {
      slot_state slot;
      slot.config = config;
      m_slots.push_back (slot);
    }
}

internal_lob_dml_staging_context::~internal_lob_dml_staging_context ()
{
  clear ();
}

int
internal_lob_dml_staging_context::init (THREAD_ENTRY *thread_p)
{
  if (m_initialized || m_slots.empty ())
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML slot configuration");
    }

  for (slot_state &slot : m_slots)
    {
      if ((slot.config.type != DB_TYPE_BLOB && slot.config.type != DB_TYPE_CLOB)
	  || slot.config.data_length < -1 || slot.config.data_length > DB_MAX_INTERNAL_LOB_LENGTH
	  || slot.config.logical_length < -1
	  || (slot.config.type == DB_TYPE_CLOB && slot.config.logical_length > DB_MAX_INTERNAL_LOB_LENGTH)
	  || (slot.config.type == DB_TYPE_BLOB
	      && slot.config.logical_length > (DB_BIGINT) DB_MAX_INTERNAL_LOB_LENGTH * 8)
	  || (slot.config.flags & ~INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE) != 0
	  || ((slot.config.flags & INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE) != 0
	      && (slot.config.data_length < 0 || slot.config.logical_length < 0
		  || OID_ISNULL (&slot.config.class_oid))))
	{
	  return internal_lob_dml_session_set_error ("invalid internal LOB DML slot metadata");
	}

      slot.direct_enabled = (slot.config.flags & INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE) != 0;
      m_has_direct_slots |= slot.direct_enabled;
    }

  if (m_has_direct_slots)
    {
      int tran_index;

      if (thread_p == NULL)
	{
	  return internal_lob_dml_session_set_error ("direct internal LOB DML requires a server transaction");
	}

      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      snprintf (m_savepoint_name, sizeof (m_savepoint_name), "ilob_stream_%d_%p", tran_index, (void *) this);
      if (xtran_server_savepoint (thread_p, m_savepoint_name, &m_savepoint_lsa) != NO_ERROR)
	{
	  return er_errid () == NO_ERROR ? ER_FAILED : er_errid ();
	}
      m_savepoint_started = true;

      for (slot_state &slot : m_slots)
	{
	  int error;

	  if (!slot.direct_enabled)
	    {
	      continue;
	    }
	  error = initialize_direct_slot (thread_p, slot);
	  if (error != NO_ERROR)
	    {
	      rollback_direct_writes (thread_p);
	      return error;
	    }
	}
    }

  m_initialized = true;
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::initialize_direct_slot (THREAD_ENTRY *thread_p, slot_state &slot)
{
  HFID hfid;
  VFID lob_vfid;
  int error;

  if (heap_get_class_info (thread_p, &slot.config.class_oid, &hfid, NULL, NULL) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  VFID_SET_NULL (&lob_vfid);
  if (!heap_internal_lob_find_vfid (thread_p, &hfid, &lob_vfid, true))
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  return internal_lob_reverse_insert_begin (thread_p, lob_vfid, slot.config.type, slot.config.data_length,
	 slot.config.logical_length, slot.direct_writer);
}

int
internal_lob_dml_staging_context::capture_direct_tracking (THREAD_ENTRY *thread_p, slot_state &slot,
    std::size_t oid_start, std::size_t lsa_start)
{
  LOG_TDES *tdes;

  if (thread_p == NULL)
    {
      return internal_lob_dml_session_set_error ("missing server thread for direct internal LOB tracking");
    }

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes == NULL || oid_start > thread_p->oos_oids.size ()
      || lsa_start > tdes->oos_insert_lsa_queue.m_queue.size ())
    {
      return internal_lob_dml_session_set_error ("invalid direct internal LOB replication tracking state");
    }

  slot.direct_oids.insert (slot.direct_oids.end (), thread_p->oos_oids.begin () + oid_start,
			   thread_p->oos_oids.end ());
  slot.direct_lsas.insert (slot.direct_lsas.end (), tdes->oos_insert_lsa_queue.m_queue.begin () + lsa_start,
			   tdes->oos_insert_lsa_queue.m_queue.end ());

  thread_p->oos_oids.resize (oid_start);
  tdes->oos_insert_lsa_queue.m_queue.resize (lsa_start);
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::finalize_direct_slot (THREAD_ENTRY *thread_p, slot_state &slot)
{
  LOG_TDES *tdes;
  std::size_t oid_start;
  std::size_t lsa_start;
  int error;
  int tracking_error;

  if (!slot.direct_enabled)
    {
      return NO_ERROR;
    }

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes == NULL)
    {
      return internal_lob_dml_session_set_error ("missing transaction for direct internal LOB DML");
    }

  oid_start = thread_p->oos_oids.size ();
  lsa_start = tdes->oos_insert_lsa_queue.m_queue.size ();
  error = internal_lob_reverse_insert_end (thread_p, slot.direct_writer, slot.direct_writer.locator);
  tracking_error = capture_direct_tracking (thread_p, slot, oid_start, lsa_start);
  return error != NO_ERROR ? error : tracking_error;
}

int
internal_lob_dml_staging_context::restore_direct_tracking (THREAD_ENTRY *thread_p, slot_state &slot)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (tdes == NULL)
    {
      return internal_lob_dml_session_set_error ("missing transaction for direct internal LOB adoption");
    }

  for (const LOG_LSA &lsa : slot.direct_lsas)
    {
      tdes->oos_insert_lsa_queue.push (lsa);
    }
  thread_p->oos_oids.insert (thread_p->oos_oids.end (), slot.direct_oids.begin (), slot.direct_oids.end ());
  slot.direct_lsas.clear ();
  slot.direct_oids.clear ();
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::delete_unadopted_direct_slots (THREAD_ENTRY *thread_p)
{
  for (slot_state &slot : m_slots)
    {
      if (!slot.direct_enabled || slot.direct_adopted)
	{
	  continue;
	}

      int error = internal_lob_delete (thread_p, slot.direct_writer.lob_vfid, slot.direct_writer.locator);
      if (error != NO_ERROR)
	{
	  return error;
	}
      slot.direct_oids.clear ();
      slot.direct_lsas.clear ();
    }
  return NO_ERROR;
}

void
internal_lob_dml_staging_context::rollback_direct_writes (THREAD_ENTRY *thread_p)
{
  if (!m_savepoint_started || thread_p == NULL)
    {
      return;
    }

  TRAN_STATE state = xtran_server_partial_abort (thread_p, m_savepoint_name, &m_savepoint_lsa);
  if (state != TRAN_UNACTIVE_ABORTED)
    {
      er_log_debug (ARG_FILE_LINE, "failed to roll back direct internal LOB stream savepoint (state=%d)\n",
		    (int) state);
    }
  m_savepoint_started = false;
}

int
internal_lob_dml_staging_context::receive_lob_chunk (THREAD_ENTRY *thread_p, int slot_index, DB_BIGINT offset,
    const char *data, int data_len)
{
  slot_state *slot;
  DB_BIGINT next_size;
  int error;

  if (!m_initialized || m_finished || slot_index < 0 || slot_index >= (int) m_slots.size () || offset < 0
      || data_len < 0 || (data_len > 0 && data == NULL))
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML payload chunk");
    }

  slot = &m_slots[slot_index];
  if (slot->received > DB_MAX_INTERNAL_LOB_LENGTH - data_len
      || (slot->config.data_length >= 0 && slot->received + data_len > slot->config.data_length))
    {
      return internal_lob_dml_session_set_error ("internal LOB DML payload exceeds declared length");
    }

  next_size = slot->received + data_len;
  if (slot->direct_enabled)
    {
      LOG_TDES *tdes;
      std::size_t oid_start;
      std::size_t lsa_start;
      int tracking_error;

      if (thread_p == NULL || data_len == 0)
	{
	  return internal_lob_dml_session_set_error ("invalid direct internal LOB DML payload chunk");
	}
      tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      if (tdes == NULL)
	{
	  return internal_lob_dml_session_set_error ("missing transaction for direct internal LOB receive");
	}

      oid_start = thread_p->oos_oids.size ();
      lsa_start = tdes->oos_insert_lsa_queue.m_queue.size ();
      error = internal_lob_reverse_insert_append (thread_p, slot->direct_writer, offset,
	      oos_buffer (const_cast<char *> (data), (std::size_t) data_len));
      tracking_error = capture_direct_tracking (thread_p, *slot, oid_start, lsa_start);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (tracking_error != NO_ERROR)
	{
	  return tracking_error;
	}
      slot->received = next_size;
      return NO_ERROR;
    }

  if (offset != slot->received)
    {
      return internal_lob_dml_session_set_error ("non-contiguous internal LOB DML payload");
    }

  if (data_len > 0)
    {
      /* Spool to a temp file rather than a growing memory buffer: this slot's target class is not
       * known yet, so the value cannot go straight to storage, and it may be up to 4GB. */
      if (slot->stage == NULL)
	{
	  slot->stage = tmpfile ();
	  if (slot->stage == NULL)
	    {
	      return internal_lob_dml_session_set_error ("failed to create internal LOB DML staging file");
	    }
	}
      if (fseeko (slot->stage, (off_t) slot->received, SEEK_SET) != 0
	  || fwrite (data, 1, (std::size_t) data_len, slot->stage) != (std::size_t) data_len)
	{
	  return internal_lob_dml_session_set_error ("failed to write internal LOB DML staging file");
	}
    }
  slot->received = next_size;
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::read_slot (void *ctx, DB_BIGINT offset, char *buffer, int size)
{
  slot_state *slot = (slot_state *) ctx;

  if (slot == NULL || buffer == NULL || size <= 0 || offset < 0 || offset > slot->received - size
      || slot->stage == NULL)
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML slot reader");
    }

  /* The writer reads the value back in ranges (from the end toward offset 0), so the staging file is
   * seeked rather than streamed sequentially. */
  if (fseeko (slot->stage, (off_t) offset, SEEK_SET) != 0
      || fread (buffer, 1, (std::size_t) size, slot->stage) != (std::size_t) size)
    {
      return internal_lob_dml_session_set_error ("failed to read internal LOB DML staging file");
    }
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::validate_slot (const slot_state &slot) const
{
  if (slot.config.data_length >= 0 && slot.received != slot.config.data_length)
    {
      return internal_lob_dml_session_set_error ("incomplete internal LOB DML payload");
    }
  if (slot.config.type == DB_TYPE_BLOB && slot.config.logical_length >= 0
      && !internal_lob_is_valid_blob_bit_length (slot.received, slot.config.logical_length))
    {
      return internal_lob_dml_session_set_error ("invalid internal BLOB logical length");
    }
  if (slot.config.type == DB_TYPE_CLOB && slot.config.logical_length >= 0
      && slot.config.logical_length != slot.received)
    {
      return internal_lob_dml_session_set_error ("invalid internal CLOB logical length");
    }
  return NO_ERROR;
}

int
internal_lob_dml_staging_context::consume_lob_slot (THREAD_ENTRY *thread_p, int slot_index, const OID *class_oid,
    DB_TYPE expected_type, INTERNAL_LOB_LOCATOR &locator)
{
  slot_state *slot;
  DB_BIGINT bit_length;
  int error;

  if (!m_initialized || m_finished || slot_index < 0 || slot_index >= (int) m_slots.size ())
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML slot consumption");
    }

  slot = &m_slots[slot_index];
  if (slot->config.type != expected_type)
    {
      return internal_lob_dml_session_set_error ("internal LOB DML slot type mismatch");
    }
  error = validate_slot (*slot);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (slot->direct_enabled)
    {
      error = finalize_direct_slot (thread_p, *slot);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (!slot->direct_adopted && OID_EQ (class_oid, &slot->config.class_oid))
	{
	  error = restore_direct_tracking (thread_p, *slot);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  locator = slot->direct_writer.locator;
	  locator.adopted = false;
	  slot->direct_adopted = true;
	  return NO_ERROR;
	}

      return heap_internal_lob_clone_locator (thread_p, class_oid, slot->config.type,
					      &slot->direct_writer.locator, &locator);
    }

  slot->read_offset = 0;

  bit_length = slot->config.type == DB_TYPE_BLOB
	       ? (slot->config.logical_length >= 0 ? slot->config.logical_length : slot->received * 8) : -1;
  return heap_internal_lob_insert_stream (thread_p, class_oid, read_slot, slot, slot->received, bit_length, &locator);
}

int
internal_lob_dml_staging_context::finish_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows)
{
  int error;

  if (!m_initialized || m_finished)
    {
      return internal_lob_dml_session_set_error ("internal LOB DML context is not active");
    }
  for (slot_state &slot : m_slots)
    {
      error = validate_slot (slot);
      if (error != NO_ERROR)
	{
	  return error;
	}
      error = finalize_direct_slot (thread_p, slot);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  error = execute_dml (thread_p, affected_rows);
  if (error == NO_ERROR)
    {
      error = delete_unadopted_direct_slots (thread_p);
    }
  if (error == NO_ERROR)
    {
      m_savepoint_started = false;
      m_finished = true;
      clear ();
    }
  return error;
}

void
internal_lob_dml_staging_context::abort_dml (THREAD_ENTRY *thread_p)
{
  rollback_direct_writes (thread_p);
  m_finished = true;
  clear ();
}

void
internal_lob_dml_staging_context::clear ()
{
  for (slot_state &slot : m_slots)
    {
      if (slot.stage != NULL)
	{
	  fclose (slot.stage);
	  slot.stage = NULL;
	}
      slot.received = 0;
      slot.read_offset = 0;
      internal_lob_reverse_insert_abort (slot.direct_writer);
      slot.direct_oids.clear ();
      slot.direct_lsas.clear ();
      slot.direct_enabled = false;
      slot.direct_adopted = false;
    }
  m_initialized = false;
  m_has_direct_slots = false;
  m_savepoint_started = false;
  LSA_SET_NULL (&m_savepoint_lsa);
  m_savepoint_name[0] = '\0';
}

internal_lob_dml_session::internal_lob_dml_session ()
  : m_context (NULL)
  , m_slot_count (0)
  , m_active (false)
{
}

internal_lob_dml_session::~internal_lob_dml_session ()
{
  delete m_context;
  m_context = NULL;
}

int
internal_lob_dml_session::init (internal_lob_dml_context *context, int slot_count)
{
  if (context == NULL || slot_count <= 0 || m_context != NULL)
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML stream configuration");
    }

  m_context = context;
  m_slot_count = slot_count;
  m_active = true;
  return NO_ERROR;
}

int
internal_lob_dml_session::receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len)
{
  int slot;
  INT64 offset;

  if (!m_active || m_context == NULL)
    {
      return internal_lob_dml_session_set_error ("internal LOB DML stream is not active");
    }
  if (data == NULL || data_len < INTERNAL_LOB_DML_FRAME_HEADER_SIZE)
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML stream frame");
    }

  slot = OR_GET_INT (data);
  OR_GET_INT64 (data + OR_INT_SIZE, &offset);
  if (slot < 0 || slot >= m_slot_count)
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML stream slot");
    }

  try
    {
      return m_context->receive_lob_chunk (thread_p, slot, (DB_BIGINT) offset,
					   data + INTERNAL_LOB_DML_FRAME_HEADER_SIZE,
					   data_len - INTERNAL_LOB_DML_FRAME_HEADER_SIZE);
    }
  catch (std::bad_alloc &)
    {
      return internal_lob_dml_session_out_of_memory ((std::size_t) data_len);
    }
}

int
internal_lob_dml_session::consume_lob_slot (THREAD_ENTRY *thread_p, int slot, const OID *class_oid,
    DB_TYPE expected_type, INTERNAL_LOB_LOCATOR &locator)
{
  if (!m_active || m_context == NULL)
    {
      return internal_lob_dml_session_set_error ("internal LOB DML stream is not active");
    }
  if (slot < 0 || slot >= m_slot_count)
    {
      return internal_lob_dml_session_set_error ("invalid internal LOB DML stream slot");
    }

  try
    {
      return m_context->consume_lob_slot (thread_p, slot, class_oid, expected_type, locator);
    }
  catch (std::bad_alloc &)
    {
      return internal_lob_dml_session_out_of_memory (sizeof (OID));
    }
}

int
internal_lob_dml_session::finish (THREAD_ENTRY *thread_p, stream_result *result)
{
  std::int64_t affected_rows = 0;
  int error;

  if (!m_active || m_context == NULL || result == NULL)
    {
      return internal_lob_dml_session_set_error ("internal LOB DML stream is not active");
    }

  try
    {
      error = m_context->finish_dml (thread_p, affected_rows);
    }
  catch (std::bad_alloc &)
    {
      error = internal_lob_dml_session_out_of_memory (sizeof (LOG_LSA));
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  result->count = affected_rows;
  m_active = false;
  return NO_ERROR;
}

void
internal_lob_dml_session::abort (THREAD_ENTRY *thread_p)
{
  if (m_active && m_context != NULL)
    {
      m_context->abort_dml (thread_p);
      m_active = false;
    }
}
