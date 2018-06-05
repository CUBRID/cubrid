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
// monitor_collect.hpp - interface for collecting statistics
//

#if !defined _MONITOR_REGISTRATION_HPP_
#define _MONITOR_REGISTRATION_HPP_

#include "monitor_collect.hpp"
#include "monitor_definition.hpp"
#include "monitor_transaction.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cubmonitor
{
  class monitor
  {
    public:
      using single_fetch_function = std::function<statistic_value (void)>;
      using multistat_fetch_function = std::function<statistic_value (std::size_t)>;

      template <typename Fetchable>
      void register_single_statistic (const char *name, const Fetchable &fetchable);

      // with transactions sheets
      template <typename Fetchable>
      void register_single_transaction_statistic (const char *name, const Fetchable &fetchable);

      // todo - add multi-statistics

    private:

      struct registered_statistics_holder
      {
	std::size_t m_offset;
	std::size_t m_statistics_count;

	multistat_fetch_function m_fetch_func;
	multistat_fetch_function m_tran_fetch_func;

	registered_statistics_holder (void);

	// todo: add here more meta-information on each statistic
      };

      void register_single_function (const char *name, const single_fetch_function &fetch_f);
      void register_single_function_with_transaction (const char *name, const single_fetch_function &fetch_func,
	  const single_fetch_function &tran_fetch_func);
      void register_statistics (std::size_t count, const multistat_fetch_function &fetch_func,
				const multistat_fetch_function &tran_fetch_func);
      void check_name_count (void);

      std::size_t m_all_count;
      std::vector<std::string> m_all_names;
      std::vector<registered_statistics_holder> m_registered;
  };

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Fetchable>
  void
  monitor::register_single_statistic (const char *name, const Fetchable &fetchable)
  {
    single_fetch_function fetch_func = std::bind (&Fetchable::fetch, fetchable);
    register_single_function (name, fetch_func);
  }

  template <typename Fetchable>
  void
  monitor::register_single_transaction_statistic (const char *name, const Fetchable &fetchable)
  {
    single_fetch_function fetch_func = std::bind (&Fetchable::fetch, fetchable);
    single_fetch_function tran_fetch_func = std::bind (&Fetchable::fetch_sheet, fetchable);

    register_single_function_with_transaction (name, fetch_func, tran_fetch_func);
  }

} // namespace cubmonitor

#endif // _MONITOR_REGISTRATION_HPP_
