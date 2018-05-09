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
 * resource_tracker.hpp - interface to track resource usage (allocations, page fixes) and detect leaks
 */

#ifndef _CUBRID_RESOURCE_TRACKER_HPP_
#define _CUBRID_RESOURCE_TRACKER_HPP_

#include <map>

#include <cstring>

namespace cubbase
{
  template <typename Res>
  class resource_tracker
  {
    public:

      using res_type = Res;

      void increment (const char *filename, const int line, const res_type &res);
      void decrement (const char *filename, const int line, const res_type &res);
      void check_leaks (void);

    private:
      void track (const char *filename, const int line, res_type &res, int amount);

      static const std::size_t MAX_FILENAME_SIZE = 32;

      struct location
      {
	const char filename[MAX_FILENAME_SIZE];
	int line;

	location (const char *fn_arg, int l_arg)
	  : filename {}
	  , line (l_arg)
	{
	  std::strncpy (filename, fn_arg, MAX_FILENAME_SIZE);
	}
      };

      struct res_entry
      {
	location first_location;
	int current_amount;

	res_entry (const char *fn_arg, int l_arg)
	  : first_location (fn_arg, l_arg)
	  , current_amount (1)
	{
	  //
	}
      };

      bool m_enabled;
      bool m_stackable;
      bool m_aborted;
      size_t m_max_size;
      size_t m_crt_size;

      std::map<res_type, res_entry> m_tracked;
  };

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Res>
  void
  resource_tracker<Res>::increment (const char *filename, const int line, const res_type &res)
  {
    if (!m_enabled || m_aborted)
      {
	return;
      }
    auto inserted = m_tracked.try_emplace (res, filename, line);
    if (inserted.second)
      {
	// inserted
	if (++m_crt_size == m_max_size)
	  {
	    // max size reached. abort the check
	    m_aborted = true;
	    m_tracked.clear ();
	  }
      }
    else
      {
	// already exists
	assert (m_stackable);
	// increment amount
	inserted.first->current_amount++;
      }
  }

  template <typename Res>
  void
  resource_tracker<Res>::decrement (const char *filename, const int line, const res_type &res)
  {
    if (!m_enabled || m_aborted)
      {
	return;
      }
    auto tracked = m_tracked.find (res);
    if (tracked == m_tracked.end ())
      {
	// not found
	assert (false);
      }
    else
      {
	tracked->second->current_amount--;
	if (tracked->second->current_amount)
	  {
	    m_tracked.erase (tracked);
	  }
      }
  }

} // namespace cubbase

#endif // _CUBRID_RESOURCE_TRACKER_HPP_
