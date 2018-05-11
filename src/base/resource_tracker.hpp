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
#include <string>

#include <cstring>

namespace cubbase
{
  // file_line - holder of file/line location
  //
  // probably should be moved elsewhere
  //
  struct fileline_location
  {
    fileline_location (const char *fn_arg, int l_arg);

    static const std::size_t MAX_FILENAME_SIZE = 32;

    char file[MAX_FILENAME_SIZE];
    int line;
  };

  struct resource_tracker_item
  {
    public:

      resource_tracker_item (const char *fn_arg, int l_arg);

      fileline_location m_first_location;
      int m_current_amount;
  };

  template <typename Res>
  class resource_tracker
  {
    public:

      resource_tracker (const char *name, bool enabled, bool stackable, std::size_t max_size);

      using res_type = Res;

      void increment (const char *filename, const int line, const res_type &res);
      void decrement (const res_type &res);
      void check_leaks (void);

    private:
      void track (const char *filename, const int line, res_type &res, int amount);

      std::string m_name;

      bool m_enabled;
      bool m_stackable;
      bool m_aborted;
      size_t m_max_size;
      size_t m_crt_size;

      std::map<res_type, resource_tracker_item> m_tracked;
  };

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Res>
  resource_tracker<Res>::resource_tracker (const char *name, bool enabled, bool stackable, std::size_t max_size)
    : m_name (name)
    , m_enabled (enabled)
    , m_stackable (stackable)
    , m_aborted (false)
    , m_max_size (max_size)
    , m_crt_size (0)
  {
    //
  }

  template <typename Res>
  void
  resource_tracker<Res>::increment (const char *filename, const int line, const res_type &res)
  {
    if (!m_enabled || m_aborted)
      {
	return;
      }

    //
    // std::map::try_emplace returns a pair <it_insert_or_found, did_insert>
    // it_insert_or_found = did_insert ? iterator to inserted : iterator to existing
    // it_insert_or_found is an iterator to pair <res_type, resource_tracker_item>
    //
    auto inserted = m_tracked.try_emplace (res, filename, line);
    if (inserted.second)
      {
	// did insert
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
	inserted.first->second.m_current_amount++;
      }
  }

  template <typename Res>
  void
  resource_tracker<Res>::decrement (const res_type &res)
  {
    if (!m_enabled || m_aborted)
      {
	return;
      }

    // std::map::find returns a pair to <res_type, resource_tracker_item>
    auto tracked = m_tracked.find (res);
    if (tracked == m_tracked.end ())
      {
	// not found
	assert (false);
      }
    else
      {
	// decrement
	tracked->second.m_current_amount--;
	if (tracked->second.m_current_amount == 0)
	  {
	    // remove from map
	    m_tracked.erase (tracked);
	  }
	else
	  {
	    assert (m_stackable);
	  }
      }
  }

} // namespace cubbase

#endif // _CUBRID_RESOURCE_TRACKER_HPP_
