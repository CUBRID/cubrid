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

#include "config.h"
#include "pgbuf_inspector_scan.hpp"
#include <algorithm>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpgbuf
{
  namespace inspector
  {
    bool resident_scan::begin (const scan_source &source, const std::string &incarnation, std::uint64_t sequence,
			       std::size_t start, std::uint64_t monotonic_us, std::uint64_t wall_us,
			       std::string &output, scan_limits limits)
    {
      *this = resident_scan {};
      m_source = source;
      m_limits = limits;
      m_limits.slots = std::min<std::size_t> (limits.slots, SCAN_SLOT_LIMIT);
      m_limits.records = std::min<std::size_t> (limits.records, SCAN_RECORD_LIMIT);
      m_limits.bytes = std::min<std::size_t> (limits.bytes, SCAN_BYTE_LIMIT);
      m_limits.elapsed_us = std::min<std::uint64_t> (limits.elapsed_us, SCAN_ELAPSED_US);
      m_start = source.slots ? start % source.slots : 0;
      m_started = monotonic_us;
      m_binding = "\"incarnation\":\"" + incarnation + "\",\"scan_seq\":\"" + std::to_string (sequence) + "\"";
      std::string header;
      if (encode_frame ("{\"type\":\"scan_header\"," + m_binding + ",\"start_time_us\":\""
			+ std::to_string (wall_us) + "\"}", header) != encode_status::OK
	  || header.size () + WIRE_CONTROL_FRAME_MAX_BYTES > m_limits.bytes)
	{
	  return false;
	}
      m_bytes = header.size ();
      output += header;
      m_done = false;
      return true;
    }

    bool resident_scan::footer (std::uint64_t wall_us, std::string &output)
    {
      std::string frame;
      if (encode_frame ("{\"type\":\"scan_footer\"," + m_binding + ",\"end_time_us\":\""
			+ std::to_string (wall_us) + "\",\"record_count\":" + std::to_string (m_records)
			+ ",\"visited_slots\":" + std::to_string (m_visited) + ",\"truncated\":"
			+ (m_partial || m_visited < m_source.slots ? "true}" : "false}"), frame) != encode_status::OK
	  || frame.size () > m_limits.bytes - m_bytes)
	{
	  return false;
	}
      output += frame;
      m_bytes += frame.size ();
      m_done = true;
      return true;
    }

    bool encode_page_sample (const page_sample &page, int shared, int private_count,
			     const std::string &binding, std::string &output)
    {
      const char *latches[] = {"none", "read", "write", "flush"};
      const char *zones[] = {"void", "lru1", "lru2", "lru3"};
      const char *kind = "none";
      int index = -1;
      if (page.zone >= 1 && page.zone <= 3)
	{
	  kind = "invalid";
	  if (page.list_index >= 0 && shared >= 0 && private_count >= 0)
	    {
	      if (page.list_index < shared)
		{
		  kind = "shared";
		  index = page.list_index;
		}
	      else if (static_cast<std::int64_t> (page.list_index) < static_cast<std::int64_t> (shared) + private_count)
		{
		  kind = "private";
		  index = page.list_index - shared;
		}
	    }
	}
      std::string frame = "{\"type\":\"page\"," + binding + ",\"volid\":" + std::to_string (page.volid)
			  + ",\"pageid\":" + std::to_string (page.pageid) + ",\"latch_mode\":\""
			  + (page.latch_mode >= 0 && page.latch_mode < 4 ? latches[page.latch_mode] : "unknown")
			  + "\",\"waiter_present\":" + (page.waiter_present ? "true" : "false");
      if (page.fix_count >= 0)
	{
	  frame += ",\"fix_count\":" + std::to_string (page.fix_count);
	}
      frame += std::string (",\"dirty\":") + (page.dirty ? "true" : "false")
	       + ",\"flushing\":" + (page.flushing ? "true" : "false")
	       + ",\"async_flush_requested\":" + (page.async_flush_requested ? "true" : "false")
	       + ",\"to_vacuum\":" + (page.to_vacuum ? "true" : "false")
	       + ",\"lru_zone\":\"" + (page.zone >= 0 && page.zone < 4 ? zones[page.zone] : "invalid")
	       + "\",\"lru_list_kind\":\"" + kind + "\",\"lru_list_index\":"
	       + (index >= 0 ? std::to_string (index) : "null");
      auto append_lsa = [&frame] (const char *name, const std::optional<sampled_lsa> &lsa)
      {
	// Absent means unknown. Only the engine null pageid is a known null LSA.
	if (!lsa || lsa->pageid < -1 || (lsa->pageid != -1 && (lsa->offset < 0 || lsa->offset > 32767)))
	  {
	    return;
	  }
	frame += std::string (",\"") + name + "\":";
	frame += lsa->pageid == -1 ? "null" : "{\"pageid\":\"" + std::to_string (lsa->pageid)
		 + "\",\"offset\":" + std::to_string (lsa->offset) + "}";
      };
      append_lsa ("page_lsa", page.page_lsa);
      append_lsa ("oldest_unflush_lsa", page.oldest_unflush_lsa);
      if (page.page_kind)
	{
	  frame += std::string (",\"page_kind\":\"") + page.page_kind + "\"";
	}
      frame += "}";
      return encode_frame (frame, output) == encode_status::OK;
    }

    std::size_t resident_scan::next_start () const
    {
      return m_source.slots ? (m_start + std::max<std::size_t> (m_visited, 1)) % m_source.slots : 0;
    }

    bool resident_scan::step (std::uint64_t monotonic_us, std::uint64_t wall_us, std::string &output)
    {
      if (m_done)
	{
	  return true;
	}
      if (m_visited == m_source.slots || m_visited >= m_limits.slots || m_records >= m_limits.records
	  || monotonic_us - m_started >= m_limits.elapsed_us)
	{
	  return footer (wall_us, output);
	}
      if (!m_source.sample)
	{
	  return false;
	}
      page_sample page;
      auto status = m_source.sample ((m_start + m_visited) % m_source.slots, page);
      ++m_visited;
      if (status == sample_status::UNAVAILABLE)
	{
	  m_partial = true;
	}
      else if (status == sample_status::RESIDENT)
	{
	  std::string frame;
	  if (!encode_page_sample (page, m_source.shared, m_source.private_count, m_binding, frame))
	    {
	      return false;
	    }
	  if (frame.size () + WIRE_CONTROL_FRAME_MAX_BYTES > m_limits.bytes - m_bytes)
	    {
	      m_partial = true;
	      return footer (wall_us, output);
	    }
	  ++m_records;
	  m_bytes += frame.size ();
	  output += frame;
	}
      return true;
    }
  }
}
