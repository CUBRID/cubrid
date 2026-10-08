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
 * heap_oos_value_ref.hpp - Decoded disk or retained-memory OOS value reference
 */

#ifndef _HEAP_OOS_VALUE_REF_HPP_
#define _HEAP_OOS_VALUE_REF_HPP_

#include "oos_file.hpp"

#include <cstddef>
#include <vector>

class heap_pending_record;
struct heap_cache_attrinfo;

/* Decoded reference, not the packed record format. Both alternatives copy into
 * caller-owned storage; callers never borrow a pending payload or a page. */
class heap_oos_value_ref
{
  public:
    heap_oos_value_ref () : m_kind (kind::disk), m_length (0), m_value () {}
    static int decode (const RECDES &record, int location, heap_oos_value_ref &ref,
		       const heap_pending_record *pending = nullptr);
    static void encode_pending (char *stub, DB_BIGINT length, int index);
    std::size_t length () const
    {
      return m_length;
    }
    int read_into (THREAD_ENTRY *thread_p, oos_buffer destination) const;

  private:
    /* The caller locates and bounds-checks the field before decoding its bytes. */
    static int decode_stub (const RECDES &record, char *stub, heap_oos_value_ref &ref,
			    const heap_pending_record *pending);
    enum class kind { memory, disk };
    kind m_kind;
    std::size_t m_length;
    union value
    {
      const char *memory;
      oos_chain_ref disk;
      value () : disk {} {}
    } m_value;
    friend int heap_oos_read_grouped_payloads (THREAD_ENTRY *, RECDES *, heap_cache_attrinfo *,
	std::vector<RECDES> &, bool *, const heap_pending_record *);
    friend int heap_oos_finalize_record (THREAD_ENTRY *, const OID *, RECDES *, heap_pending_record *);
};

#endif /* _HEAP_OOS_VALUE_REF_HPP_ */
