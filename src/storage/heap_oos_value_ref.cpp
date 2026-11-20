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
 * heap_oos_value_ref.cpp - Decode OOS references and copy their values into caller-owned storage
 */

#include "heap_oos_value_ref.hpp"

#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "heap_file.h"
#include "heap_pending_record.hpp"
#include "object_representation.h"

#include <climits>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

void
heap_oos_value_ref::encode_pending (char *stub, DB_BIGINT length, int index)
{
  OR_BUF buf;
  or_init (&buf, stub, OR_OOS_INLINE_SIZE);
  or_put_oid (&buf, &oid_Null_oid);
  or_put_bigint (&buf, length);
  or_put_bigint (&buf, index);
}

int
heap_oos_value_ref::decode (const RECDES &record, int location, heap_oos_value_ref &ref,
			    const heap_pending_record *pending)
{
  char *stub = nullptr;
  (void) heap_recdes_get_oos_inline_stub (&record, location, &stub);
  return decode_stub (record, stub, ref, pending);
}

int
heap_oos_value_ref::decode_stub (const RECDES &record, char *stub, heap_oos_value_ref &ref,
				 const heap_pending_record *pending)
{
  DB_BIGINT length = 0;
  OID head = OID_INITIALIZER;
  if (stub == nullptr)
    {
      goto invalid;
    }
  OR_GET_OID (stub, &head);
  OR_GET_BIGINT (stub + OR_OID_SIZE, &length);
  if (length <= 0 || length > DB_MAX_STRING_LENGTH)
    {
      goto invalid;
    }
  ref.m_length = (std::size_t) length;
  if (OID_ISNULL (&head))
    {
      /* Bytes never authorize memory access: require this allocation's prepared
       * owner and a matching retained value. Copies and incoming rows stay disk-only. */
      DB_BIGINT index = 0;
      if (pending == nullptr)
	{
	  goto invalid;
	}
      OR_GET_BIGINT (stub + OR_OID_SIZE + OR_BIGINT_SIZE, &index);
      if (index < 0 || index > INT_MAX)
	{
	  goto invalid;
	}
      oos_buffer payload = pending->resolve (record, (std::size_t) index, ref.m_length);
      if (payload.data () == nullptr)
	{
	  goto invalid;
	}
      ref.m_kind = kind::memory;
      ref.m_value.memory = payload.data ();
      return NO_ERROR;
    }
  DB_BIGINT packed_identity_stamp;
  OR_GET_BIGINT (stub + OR_OID_SIZE + OR_BIGINT_SIZE, &packed_identity_stamp);
  ref.m_value.disk = { head, oos_unpack_identity_stamp (packed_identity_stamp) };
  ref.m_kind = kind::disk;
  return NO_ERROR;

invalid:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_BAD_INLINE_HEADER, 3, OID_AS_ARGS (&head));
  return ER_HEAP_OOS_BAD_INLINE_HEADER;
}

int
heap_oos_value_ref::read_into (THREAD_ENTRY *thread_p, oos_buffer destination) const
{
  if (destination.size () != m_length || destination.data () == nullptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  switch (m_kind)
    {
    case kind::memory:
      std::memcpy (destination.data (), m_value.memory, m_length);
      return NO_ERROR;
    case kind::disk:
      return oos_read (thread_p, m_value.disk, destination);
    }
  return ER_FAILED;
}
