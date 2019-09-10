/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * log_postpone_cache.hpp - log postpone cache module
 */

#ifndef _LOG_POSTPONE_CACHE_HPP_
#define _LOG_POSTPONE_CACHE_HPP_

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "mem_block.hpp"
#include "storage_common.h"

#include <array>

// forward declarations
struct log_tdes;
struct log_prior_node;

namespace cubthread
{
  class entry;
}

/**
 * Caches postpones to avoid reading them from log after commit top operation with postpone.
 * Otherwise, log critical section may be required which will slow the access on merged index nodes
 */
class log_postpone_cache
{
  public:
    log_postpone_cache ()
      : m_redo_data_buf ()
      , m_redo_data_offset (0)
      , m_is_redo_data_buf_full (false)
      , m_cursor (0)
      , m_cache_entries ()
    {
    }

    log_postpone_cache (log_postpone_cache &&other) = delete;
    log_postpone_cache (const log_postpone_cache &other) = delete;

    log_postpone_cache &operator= (log_postpone_cache &&other) = delete;
    log_postpone_cache &operator= (const log_postpone_cache &other) = delete;

    ~log_postpone_cache () = default;

    void reset ();

    void add_redo_data (const log_prior_node &node);
    void add_lsa (const log_lsa &lsa);
    bool do_postpone (cubthread::entry &thread_ref, const log_lsa &start_postpone_lsa);

  private:
    static const std::size_t MAX_CACHE_ENTRIES = 512;
    // on average redo data size for an entry is 48 bytes
    static const std::size_t REDO_DATA_MAX_SIZE = 48 * MAX_CACHE_ENTRIES;

    class cache_entry
    {
      public:
	cache_entry ()
	  : m_lsa ()
	  , m_offset (0)
	{
	  m_lsa.set_null ();
	}

	log_lsa m_lsa;
	std::size_t m_offset;
    };

    cubmem::extensible_block m_redo_data_buf;
    std::size_t m_redo_data_offset;
    bool m_is_redo_data_buf_full;

    std::size_t m_cursor;
    std::array<cache_entry, MAX_CACHE_ENTRIES> m_cache_entries;

    bool is_full () const;
};

#endif /* _LOG_POSTPONE_CACHE_HPP_ */
