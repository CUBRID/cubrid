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
#include "storage_common.h"
#include "thread_entry.hpp"

#include <array>

/**
 * Caches postpones to avoid reading them from log after commit top operation with postpone.
 * Otherwise, log critical section may be required which will slow the access on merged index nodes
 */
class log_postpone_cache
{
  public:
    log_postpone_cache ()
      : m_redo_data_buf (NULL)
      , m_redo_data_ptr (NULL)
      , m_cache_status (LOG_POSTPONE_CACHE_NO)
      , m_cache_entries_cursor (0)
      , m_cache_entries ()
    {
      m_redo_data_buf = new char[REDO_DATA_SIZE];
    }

    log_postpone_cache (log_postpone_cache &&other) = delete;
    log_postpone_cache (const log_postpone_cache &other) = delete;

    log_postpone_cache &operator= (log_postpone_cache &&other) = delete;
    log_postpone_cache &operator= (const log_postpone_cache &other) = delete;

    ~log_postpone_cache ()
    {
      delete [] m_redo_data_buf;
    }

    void clear ();

    void insert (log_lsa &lsa);
    void redo_data (char *data_header, char *rcv_data, int rcv_data_length);
    bool do_postpone (cubthread::entry &thread_ref, log_lsa *start_postpone_lsa);

  private:
    static const std::size_t REDO_DATA_SIZE;
    static const std::size_t MAX_CACHE_ENTRIES = 10;

    class cache_entry
    {
      public:
	cache_entry ()
	  : m_lsa ()
	  , m_redo_data (NULL)
	{
	  m_lsa.set_null ();
	}

	log_lsa m_lsa;
	char *m_redo_data;
    };

    enum cache_status
    {
      LOG_POSTPONE_CACHE_NO,
      LOG_POSTPONE_CACHE_YES,
      LOG_POSTPONE_CACHE_OVERFLOW
    };

    char *m_redo_data_buf;
    char *m_redo_data_ptr;

    cache_status m_cache_status;

    std::size_t m_cache_entries_cursor;
    std::array<cache_entry, MAX_CACHE_ENTRIES> m_cache_entries;
};

#endif /* _LOG_POSTPONE_CACHE_HPP_ */
