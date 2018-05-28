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

//
// monitor_transcation.hpp - interface for managing transaction separate sheets of statistics
//

#if !defined _MONITOR_TRANSACTION_HPP_
#define _MONITOR_TRANSACTION_HPP_

#include "monitor_collect_forward.hpp"

#include <mutex>

#include <cstring>

namespace cubmonitor
{
  extern std::size_t Transaction_watcher_count;
  bool start_transaction_watcher (int tran_index);
  void end_transaction_watcher (int tran_index);
  bool is_transaction_watching (int tran_index, bool &is_static, std::size_t &dynamic_index);

  class transaction_watcher
  {
    public:
      transaction_watcher (int tran_index);
      transaction_watcher (void) = delete;
      ~transaction_watcher (void);

    private:
      // private members
      int m_tran_index;
      bool m_watching;
  };

  // forward definition; implementation is entirely in monitor_transaction.cpp
  template <typename Col>
  class transaction_dynamic_collectors
  {
    public:
      using collector_type = Col;

      transaction_dynamic_collectors (void);
      ~transaction_dynamic_collectors (void);

      collector_type &get_collector (std::size_t index);

    private:
      void extend (std::size_t index);

      std::size_t m_size;
      collector_type *m_collectors;
      std::mutex m_extend_mutex;
  };

  template <class Col>
  class transaction_collector
  {
    public:

      transaction_collector (void)
	: m_static_collector ()
	, m_dynamic_collectors ()
      {
	//
      }

      using collector_type = Col;

      inline void collect (int tran_index, const typename collector_type::rep &change);
      statistic_value fetch (int tran_index);

    private:

      void collect_internal (int tran_index, const typename collector_type::rep &change);


      collector_type m_static_collector;
      transaction_dynamic_collectors<collector_type> m_dynamic_collectors;
  };

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // transaction_collector
  //////////////////////////////////////////////////////////////////////////

  template <class Col>
  void
  transaction_collector<Col>::collect (int tran_index, const typename collector_type::rep &change)
  {
    if (Transaction_watcher_count == 0)
      {
	return;
      }

    collect_internal (tran_index, change);
  }

  template <class Col>
  void
  transaction_collector<Col>::collect_internal (int tran_index, const typename collector_type::rep &change)
  {
    bool is_static = false;
    std::size_t index = 0;

    if (!is_transaction_watching (tran_index, is_static, index))
      {
	// not watching
	return;
      }

    if (is_static)
      {
	m_static_collector.collect (change);
      }
    else
      {
	m_dynamic_collectors.get_collector (index).collect (change);
      }
  }

  template <typename Col>
  statistic_value
  transaction_collector<Col>::fetch (int tran_index)
  {
    bool is_static = false;
    std::size_t index = 0;

    if (!is_transaction_watching (tran_index, is_static, index))
      {
	// not watching
	return 0;
      }

    if (is_static)
      {
	return m_static_collector.fetch ();
      }
    else
      {
	return m_dynamic_collectors.get_collector (index).fetch ();
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // transaction_dynamic_collectors
  //////////////////////////////////////////////////////////////////////////

  template <typename Col>
  transaction_dynamic_collectors<Col>::transaction_dynamic_collectors (void)
    : m_size (0)
    , m_collectors (NULL)
    , m_extend_mutex ()
  {
    //
  }

  template <typename Col>
  transaction_dynamic_collectors<Col>::~transaction_dynamic_collectors (void)
  {
    delete [] m_collectors;
  }

  template <typename Col>
  typename transaction_dynamic_collectors<Col>::collector_type &
  transaction_dynamic_collectors<Col>::get_collector (std::size_t index)
  {
    if (index >= m_size)
      {
	extend (index);
      }

    return m_collectors[index];
  }

  template <typename Col>
  void
  transaction_dynamic_collectors<Col>::extend (std::size_t index)
  {
    std::unique_lock<std::mutex> ulock (m_extend_mutex);
    if (index < m_size)
      {
	// already extended
	return;
      }

    collector_type *new_collectors = new collector_type[index + 1];

    if (m_collectors != NULL)
      {
	std::memcpy (new_collectors, m_collectors, m_size * sizeof (collector_type));
	delete [] m_collectors;
      }

    m_collectors = new_collectors;
    m_size = index + 1;
  }

} // namespace cubmonitor

#endif // _MONITOR_TRANSACTION_HPP_
