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

#include "fileline_location.hpp"

#include <map>
#include <forward_list>
#include <string>
#include <iostream>

#include <cassert>

namespace cubbase
{
  // resource_tracker_item -
  //
  struct resource_tracker_item
  {
    public:

      resource_tracker_item (const char *fn_arg, int l_arg, int amount);

      fileline_location m_first_location;
      unsigned m_amount;
  };

  std::ostream &operator<< (std::ostream &os, const resource_tracker_item &item);

  template <typename Res>
  class resource_tracker
  {
    public:
      using res_type = Res;
      using map_type = std::map<res_type, resource_tracker_item>;

      resource_tracker (const char *name, bool enabled, std::size_t max_size, const char *res_name,
			unsigned max_amount = 1);
      ~resource_tracker (void);

      void increment (const char *filename, const int line, const res_type &res, unsigned amount = 1);
      void decrement (const res_type &res, unsigned amount = 1);
      void push_track (void);
      void pop_track (void);
      void clear_all (void);
      unsigned get_total_amount (void) const;
      void check_total_amount (unsigned expected_amount) const;

    private:

      map_type &get_current_map (void);

      void abort (void);
      void dump (void) const;
      void dump_map (const map_type &map, std::ostream &out) const;

      const char *m_name;
      const char *m_res_name;

      bool m_enabled;
      bool m_is_aborted;
      size_t m_max_size;
      size_t m_crt_size;
      unsigned m_max_amout;

      std::forward_list<map_type> m_tracked_stack;
  };

  //////////////////////////////////////////////////////////////////////////
  // other functions & stuff
  //////////////////////////////////////////////////////////////////////////

  bool restrack_pop_error (void);
  void restrack_set_error (bool set_error);
  void restrack_set_suppress_assert (bool suppress);
  bool restrack_is_assert_suppressed (void);
  inline void restrack_assert (bool cond);

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Res>
  resource_tracker<Res>::resource_tracker (const char *name, bool enabled, std::size_t max_size, const char *res_name,
      unsigned max_amount /* = 1 */)
    : m_name (name)
    , m_res_name (res_name)
    , m_enabled (enabled)
    , m_is_aborted (false)
    , m_max_size (max_size)
    , m_crt_size (0)
    , m_max_amout (max_amount)
    , m_tracked_stack ()
  {
    //
  }

  template <typename Res>
  resource_tracker<Res>::~resource_tracker (void)
  {
    restrack_assert (m_tracked_stack.empty ());
    restrack_assert (m_crt_size == 0);

    clear_all ();
  }

  template <typename Res>
  void
  resource_tracker<Res>::increment (const char *filename, const int line, const res_type &res,
				    unsigned amount /* = 1 */)
  {
    if (!m_enabled || m_is_aborted)
      {
	return;
      }

    if (m_tracked_stack.empty ())
      {
	// not active
	return;
      }

    restrack_assert (amount <= m_max_amout);

    map_type &map = get_current_map ();

    //
    // std::map::try_emplace returns a pair <it_insert_or_found, did_insert>
    // it_insert_or_found = did_insert ? iterator to inserted : iterator to existing
    // it_insert_or_found is an iterator to pair <res_type, resource_tracker_item>
    //
    // as of c++17 we could just do:
    // auto inserted = map.try_emplace (res, filename, line, amount);
    //
    // however, for now we have to use next piece of code I got from
    // https://en.cppreference.com/w/cpp/container/map/emplace
    auto inserted = map.emplace (std::piecewise_construct, std::forward_as_tuple (res),
				 std::forward_as_tuple (filename, line, amount));
    if (inserted.second)
      {
	// did insert
	if (++m_crt_size > m_max_size)
	  {
	    // max size reached. abort the tracker
	    abort ();
	    return;
	  }
      }
    else
      {
	// already exists
	// increment amount
	inserted.first->second.m_amount += amount;

	restrack_assert (inserted.first->second.m_amount <= m_max_amout);
      }
  }

  template <typename Res>
  void
  resource_tracker<Res>::decrement (const res_type &res, unsigned amount /* = 1 */)
  {
    if (!m_enabled || m_is_aborted)
      {
	return;
      }

    if (m_tracked_stack.empty ())
      {
	// not active
	return;
      }

    map_type &map = get_current_map ();

    // note - currently resources from different levels cannot be mixed. one level cannot free resources from another
    //        level... if we want to do that, I don't think the stacking has any use anymore. we can just keep only
    //        one level and that's that.

    // std::map::find returns a pair to <res_type, resource_tracker_item>
    auto tracked = map.find (res);
    if (tracked == map.end ())
      {
	// not found
	restrack_assert (false);
      }
    else
      {
	// decrement
	if (amount > tracked->second.m_amount)
	  {
	    // more than expected
	    restrack_assert (false);
	    map.erase (tracked);
	    m_crt_size--;
	  }
	else
	  {
	    tracked->second.m_amount -= amount;
	    if (tracked->second.m_amount == 0)
	      {
		// remove from map
		map.erase (tracked);
		m_crt_size--;
	      }
	  }
      }
  }

  template <typename Res>
  unsigned
  resource_tracker<Res>::get_total_amount (void) const
  {
    unsigned total = 0;
    for (auto stack_it : m_tracked_stack)
      {
	for (auto map_it : stack_it)
	  {
	    total += map_it.second.m_amount;
	  }
      }
    return total;
  }

  template <typename Res>
  void
  resource_tracker<Res>::check_total_amount (unsigned expected_amount) const
  {
    if (m_enabled)
      {
	restrack_assert (get_total_amount () == expected_amount);
      }
  }

  template <typename Res>
  typename resource_tracker<Res>::map_type &
  resource_tracker<Res>::get_current_map (void)
  {
    return m_tracked_stack.front ();
  }

  template <typename Res>
  void
  resource_tracker<Res>::clear_all (void)
  {
    // clear stack
    while (!m_tracked_stack.empty ())
      {
	pop_track ();
      }
    restrack_assert (m_crt_size == 0);
  }

  template <typename Res>
  void
  resource_tracker<Res>::abort (void)
  {
    m_is_aborted = true;
  }

  template <typename Res>
  void
  resource_tracker<Res>::dump (void) const
  {
    std::ostream &out = std::cerr;

    out << std::endl;
    out << "   +--- " << m_name << std::endl;
    out << "         +--- amount = " << get_total_amount () << " (threshold = " << m_max_size << ")" << std::endl;

    std::size_t level = 0;
    for (auto stack_it : m_tracked_stack)
      {
	out << "         +--- stack_level = " << level << std::endl;
	dump_map (stack_it, out);
	level++;
      }
  }

  template <typename Res>
  void
  resource_tracker<Res>::dump_map (const map_type &map, std::ostream &out) const
  {
    for (auto map_it : map)
      {
	out << "            +--- tracked " << m_res_name << "=" << map_it.first;
	out << " " << map_it.second;
	out << std::endl;
      }
    out << "            +--- tracked " << m_res_name << " count = " << map.size () << std::endl;
  }

  template <typename Res>
  void
  resource_tracker<Res>::push_track (void)
  {
    if (!m_enabled)
      {
	return;
      }

    if (m_tracked_stack.empty ())
      {
	// fresh start. set abort as false
	m_crt_size = 0;
	m_is_aborted = false;
      }

    m_tracked_stack.emplace_front ();
  }

  template <typename Res>
  void
  resource_tracker<Res>::pop_track (void)
  {
    if (!m_enabled)
      {
	return;
      }

    if (m_tracked_stack.empty ())
      {
	restrack_assert (false);
	return;
      }

    map_type &map = m_tracked_stack.front ();
    if (!map.empty ())
      {
	if (!m_is_aborted)
	  {
	    dump ();
	    restrack_assert (false);
	  }
	m_crt_size -= map.size ();
      }
    m_tracked_stack.pop_front ();

    restrack_assert (!m_tracked_stack.empty () || m_crt_size == 0);
  }

  //////////////////////////////////////////////////////////////////////////
  // other
  //////////////////////////////////////////////////////////////////////////
  void
  restrack_assert (bool cond)
  {
#if !defined (NDEBUG)
    if (restrack_is_assert_suppressed ())
      {
	restrack_set_error (!cond);
      }
    else
      {
	assert (cond);
      }
#endif // NDEBUG
  }

} // namespace cubbase

#endif // _CUBRID_RESOURCE_TRACKER_HPP_
