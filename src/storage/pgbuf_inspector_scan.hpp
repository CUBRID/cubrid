/*
 * Copyright 2008 Search Solution Corporation
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

#ifndef _PGBUF_INSPECTOR_SCAN_HPP_
#define _PGBUF_INSPECTOR_SCAN_HPP_

#include "pgbuf_inspector_wire.hpp"
#include <functional>

namespace cubpgbuf
{
  namespace inspector
  {
    constexpr std::size_t SCAN_SLOT_LIMIT = 65536;
    constexpr std::size_t SCAN_RECORD_LIMIT = 65536;
    constexpr std::size_t SCAN_BYTE_LIMIT = 64 * 1024 * 1024;
    constexpr std::uint64_t SCAN_ELAPSED_US = 100000;
    constexpr std::size_t OUTPUT_BUFFER_BYTES = 65536;

// Private sampling boundary: owned scalars only, no engine pointers or page bytes.
    struct sampled_lsa
    {
      std::int64_t pageid = -1;
      int offset = -1;
    };
    struct page_sample
    {
      int volid = -1, pageid = -1;
      int latch_mode = -1, fix_count = -1;
      bool waiter_present = false;
      bool dirty = false, flushing = false, async_flush_requested = false, to_vacuum = false;
      // zone: 1..3 LRU, 0 void, otherwise invalid; index is private to this boundary.
      int zone = -1, list_index = -1;
      std::optional<sampled_lsa> page_lsa, oldest_unflush_lsa;
      const char *page_kind = "unknown";
    };
    enum class sample_status { EMPTY, RESIDENT, UNAVAILABLE };
    struct scan_source
    {
      std::size_t slots = 0;
      int shared = 0, private_count = 0;
      std::function<sample_status (std::size_t, page_sample &)> sample;
    };
    struct scan_limits
    {
      std::size_t slots = SCAN_SLOT_LIMIT, records = SCAN_RECORD_LIMIT, bytes = SCAN_BYTE_LIMIT;
      std::uint64_t elapsed_us = SCAN_ELAPSED_US;
    };
// Lower budgets are useful for deterministic tests; no caller may raise the shipped caps.
    class resident_scan
    {
      public:
	bool begin (const scan_source &source, const std::string &incarnation, std::uint64_t sequence,
		    std::size_t start, std::uint64_t monotonic_us, std::uint64_t wall_us,
		    std::string &output, scan_limits limits = {});
	// Caller admits a step only with at least one maximum frame of output capacity.
	bool step (std::uint64_t monotonic_us, std::uint64_t wall_us, std::string &output);
	bool done () const
	{
	  return m_done;
	}
	std::size_t next_start () const;
      private:
	scan_source m_source;
	scan_limits m_limits;
	std::string m_binding;
	std::size_t m_start = 0, m_visited = 0, m_records = 0, m_bytes = 0;
	std::uint64_t m_started = 0;
	bool m_done = true, m_partial = false;
	bool footer (std::uint64_t wall_us, std::string &output);
    };
    bool encode_page_sample (const page_sample &page, int shared, int private_count,
			     const std::string &binding, std::string &output);
  }
}
#endif
