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
#include <iostream>

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

    static const char *print_format (void)
    {
      return "%s:%d";
    }

    char m_file[MAX_FILENAME_SIZE];
    int m_line;
  };
#define FILELINE_LOCATION_AS_ARGS(fileline) (fileline).m_file, (fileline).m_line

  std::ostream &operator<< (std::ostream &os, const fileline_location &fileline);

  // resource_tracker_item -
  //
  struct resource_tracker_item
  {
    public:

      resource_tracker_item (const char *fn_arg, int l_arg);

      fileline_location m_first_location;
      int m_amount;
  };

  std::ostream &operator<< (std::ostream &os, const resource_tracker_item &item);

  template <typename Res>
  class resource_tracker
  {
    public:
      using res_type = Res;
      using map_type = std::map<res_type, resource_tracker_item>;

      resource_tracker (const char *name, bool enabled, bool stackable, std::size_t max_size,
			const char *res_name = "res_ptr");
      ~resource_tracker (void);

      void increment (const char *filename, const int line, const res_type &res);
      void decrement (const res_type &res);
      void check_leaks (void);

    private:
      int get_total_amount ();
      void dump (void);

      std::string m_name;
      std::string m_res_name;

      bool m_enabled;
      bool m_stackable;
      bool m_aborted;
      size_t m_max_size;
      size_t m_crt_size;

      map_type m_tracked;
  };

  //////////////////////////////////////////////////////////////////////////
  // other functions
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Res>
  resource_tracker<Res>::resource_tracker (const char *name, bool enabled, bool stackable, std::size_t max_size,
      const char *res_name /* = "res_ptr" */)
    : m_name (name)
    , m_res_name (res_name)
    , m_enabled (enabled)
    , m_stackable (stackable)
    , m_aborted (false)
    , m_max_size (max_size)
    , m_crt_size (0)
  {
    //
  }

  template <typename Res>
  resource_tracker<Res>::~resource_tracker (void)
  {
    assert (m_crt_size == 0);
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
	    // max size reached. abort the tracker
	    m_aborted = true;
	    m_tracked.clear ();
	    m_crt_size = 0;
	  }
      }
    else
      {
	// already exists
	assert (m_stackable);
	// increment amount
	inserted.first->second.m_amount++;
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
	tracked->second.m_amount--;
	if (tracked->second.m_amount == 0)
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

  template <typename Res>
  int
  resource_tracker<Res>::get_total_amount (void)
  {
    int total = 0;
    for (auto it : m_tracked)
      {
	total += it.second.m_amount;
      }
    return total;
  }

  template <typename Res>
  void
  resource_tracker<Res>::dump (void)
  {
    std::ostream &out = std::cerr;

    out << "   +--- " << m_name << std::endl;
    out << "         +--- amount = " << get_total_amount () << " (threshold = %d)" << m_max_size << std::endl;

    for (auto it : m_tracked)
      {
	out << "            +--- tracked res = " << m_res_name << "=" << it.first;
	out << " " << it.second;
	out << std::endl;
      }

    out << "            +--- tracked res count = " << m_tracked.size () << std::endl;
  }

  template <typename Res>
  void
  resource_tracker<Res>::check_leaks (void)
  {
    if (!m_tracked.empty ())
      {
	dump ();
	assert (false);

	// remove all entries
	m_tracked.clear ();
      }
    else
      {
	assert (m_crt_size == 0);
      }
    m_crt_size = 0;
    m_aborted = false;
  }

} // namespace cubbase

#endif // _CUBRID_RESOURCE_TRACKER_HPP_
