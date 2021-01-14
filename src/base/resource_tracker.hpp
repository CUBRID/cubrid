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

//
// resource_tracker.hpp - interface to track and debug resource usage (allocations, page fixes)
//
// the purpose of this interface is to track resource usage and detect possible issues:
//
//    1. resource leaks (at the end of the scope, used resources are not freed/retired)
//    2. resource over-usage (a resource exceeds a maximum allowed of usage).
//    3. invalid free/retire.
//
// how to use:
//
//    1. specialize your tracker based on resource type:
//
//        using my_tracker_type = cubbase::resource_tracker<my_resource_type>;
//
//        note - my_resource_type must be comparable; resource_tracker uses internally a std::map specialized by
//               provided type and std::less of same type.
//        todo - template for compare function
//
//    2. create an instance of your tracker.
//
//        my_tracker_type my_tracker ("My Tracker", true, MAX_RESOURCES, "my res", MAX_REUSE);
//
//    3. start tracking:
//
//        my_tracker.push_track ();
//
//    4. track resource; resource over-usage may be detected:
//
//        my_tracker.increment (__FILE__, __LINE__, my_res_to_track, use_count /* default 1 */)
//
//    5. un-track resource; resource invalid free/retire may be detected:
//
//        my_tracker.decrement (my_res_to_track, use_count /* default 1 */);
//
//    6. stop tracking; resource leaks may be detected:
//
//        my_tracker.pop_track ();
//

#ifndef _RESOURCE_TRACKER_HPP_
#define _RESOURCE_TRACKER_HPP_

#include "fileline_location.hpp"

#include <map>
#include <forward_list>
#include <sstream>

#include <cassert>

namespace cubbase
{
  //
  // resource_tracker_item - helper structure used by resource tracker to store information one resource.
  //
  struct resource_tracker_item
  {
    public:

      // ctor - file, line, initial reuse count
      resource_tracker_item (const char *fn_arg, int l_arg, unsigned reuse);

      fileline_location m_first_location;   // first increment location
      unsigned m_reuse_count;               // current reuse count
  };
  // printing a resource_tracker_item to output stream
  std::ostream &operator<< (std::ostream &os, const resource_tracker_item &item);

  //
  // resource tracker - see more details on file description
  //
  // Res template is used to allow any resource specialization. It is restricted to comparable types.
  //
  template <typename Res>
  class resource_tracker
  {
    public:

      // internal types
      using res_type = Res;
      using map_type = std::map<res_type, resource_tracker_item>;

      // ctor/dtor
      resource_tracker (const char *name, bool enabled, std::size_t max_resources, const char *res_name,
			unsigned max_reuse = 1);
      ~resource_tracker (void);

      // using resources
      // increment "uses" for given resource.
      // if it is first use, file & line are saved.
      // detects if maximum uses is exceeded.
      void increment (const char *filename, const int line, const res_type &res, unsigned use_count = 1);
      // decrement "uses" for given resource.
      // if use count becomes 0, resource is removed from tracker
      // detects invalid free/retire (existing use count is less than given count)
      void decrement (const res_type &res, unsigned use_count = 1);

      // tracking; is stackable (allows nested tracking).
      // push tracking; a new level of tracking is started
      void push_track (void);
      // pop tracking; finish last level of tacking. resource leaks are checked for last level
      void pop_track (void);
      // clear all tracks.
      void clear_all (void);

    private:

      map_type &get_current_map (void);     // get map for current level of tracking

      void abort (void);                    // abort tracking when maximum number of resources is exceeded
      void dump (void) const;               // dump all tracked resources on all levels
      void dump_map (const map_type &map, std::ostream &out) const;   // dump one tracker level
      unsigned get_total_use_count (void) const;        // compute and return total use count

      const char *m_name;                   // tracker name; used for dumping
      const char *m_res_alias;              // resource alias; used for dumping

      bool m_enabled;                       // if disabled, all tracker functions do nothing.
      // it is used to avoid branching when calling tracker functions.
      bool m_is_aborted;                    // set to true when tracking had to be aborted;
      std::size_t m_max_resources;          // maximum number of resources allowed to track; if exceeded, the tracker
      // is aborted
      std::size_t m_resource_count;         // current resource count
      unsigned m_max_reuse;                 // maximum reuse for a resource. by default is one.

      // a list of maps of tracked resources.
      // each map represents one level in tracker
      // list holds all levels
      std::forward_list<map_type> m_tracked_stack;
  };

  //////////////////////////////////////////////////////////////////////////
  // other functions & stuff
  //////////////////////////////////////////////////////////////////////////

  // functions used mainly to disable debug crashing and convert into an error; for unit testing
  // todo - convert from global to instance

  // pop existing error
  // returns true if error exists and false otherwise. error is reset.
  bool restrack_pop_error (void);
  // if set_error is true, error is set. if set_error is false, error remains as is.
  void restrack_set_error (bool set_error);
  // set assert mode
  // if suppress is true, assert is suppressed and error is set instead; if false, assert is allowed.
  void restrack_set_suppress_assert (bool suppress);
  // get current assert mode
  bool restrack_is_assert_suppressed (void);
  // check condition to be true; based on assert mode, assert is hit or error is set
  inline void restrack_assert (bool cond);

  void restrack_log (const std::string &str);

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Res>
  resource_tracker<Res>::resource_tracker (const char *name, bool enabled, std::size_t max_resources,
      const char *res_name, unsigned max_reuse /* = 1 */)
    : m_name (name)
    , m_res_alias (res_name)
    , m_enabled (enabled)
    , m_is_aborted (false)
    , m_max_resources (max_resources)
    , m_resource_count (0)
    , m_max_reuse (max_reuse)
    , m_tracked_stack ()
  {
    //
  }

  template <typename Res>
  resource_tracker<Res>::~resource_tracker (void)
  {
    // check no leaks
    restrack_assert (m_tracked_stack.empty ());
    restrack_assert (m_resource_count == 0);

    clear_all ();
  }

  template <typename Res>
  void
  resource_tracker<Res>::increment (const char *filename, const int line, const res_type &res,
				    unsigned use_count /* = 1 */)
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

    // given use_count cannot exceed max reuse
    restrack_assert (use_count <= m_max_reuse);

    // get current level map
    map_type &map = get_current_map ();

    // add resource to map or increment the use count of existing entry
    //
    // std::map::try_emplace returns a pair <it_insert_or_found, did_insert>
    // it_insert_or_found = did_insert ? iterator to inserted : iterator to existing
    // it_insert_or_found is an iterator to pair <res_type, resource_tracker_item>
    //
    // as of c++17 we could just do:
    // auto inserted = map.try_emplace (res, filename, line, use_count);
    //
    // however, for now we have to use next piece of code I got from
    // https://en.cppreference.com/w/cpp/container/map/emplace
    auto inserted = map.emplace (std::piecewise_construct, std::forward_as_tuple (res),
				 std::forward_as_tuple (filename, line, use_count));
    if (inserted.second)
      {
	// did insert
	if (++m_resource_count > m_max_resources)
	  {
	    // max size reached. abort the tracker
	    abort ();
	    return;
	  }
      }
    else
      {
	// already exists
	// increment use_count
	inserted.first->second.m_reuse_count += use_count;

	restrack_assert (inserted.first->second.m_reuse_count <= m_max_reuse);
      }
  }

  template <typename Res>
  void
  resource_tracker<Res>::decrement (const res_type &res, unsigned use_count /* = 1 */)
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
	// not found; invalid free
	restrack_assert (false);
      }
    else
      {
	// decrement
	if (use_count > tracked->second.m_reuse_count)
	  {
	    // more than expected; invalid free
	    restrack_assert (false);
	    map.erase (tracked);
	    m_resource_count--;
	  }
	else
	  {
	    tracked->second.m_reuse_count -= use_count;
	    if (tracked->second.m_reuse_count == 0)
	      {
		// remove from map
		map.erase (tracked);
		m_resource_count--;
	      }
	  }
      }
  }

  template <typename Res>
  unsigned
  resource_tracker<Res>::get_total_use_count (void) const
  {
    // iterate through all resources and collect use count
    unsigned total = 0;
    for (auto stack_it : m_tracked_stack)
      {
	for (auto map_it : stack_it)
	  {
	    total += map_it.second.m_reuse_count;
	  }
      }
    return total;
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
    restrack_assert (m_resource_count == 0);
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
    std::stringstream out;

    out << std::endl;
    out << "   +--- " << m_name << std::endl;
    out << "         +--- amount = " << get_total_use_count () << " (threshold = " << m_max_resources << ")" << std::endl;

    std::size_t level = 0;
    for (auto stack_it : m_tracked_stack)
      {
	out << "         +--- stack_level = " << level << std::endl;
	dump_map (stack_it, out);
	level++;
      }

    restrack_log (out.str ());
  }

  template <typename Res>
  void
  resource_tracker<Res>::dump_map (const map_type &map, std::ostream &out) const
  {
    for (auto map_it : map)
      {
	out << "            +--- tracked " << m_res_alias << "=" << map_it.first;
	out << " " << map_it.second;
	out << std::endl;
      }
    out << "            +--- tracked " << m_res_alias << " count = " << map.size () << std::endl;
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
	m_resource_count = 0;
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

    // get current level map
    map_type &map = m_tracked_stack.front ();
    if (!map.empty ())
      {
	if (!m_is_aborted)
	  {
	    // resources are leaked
	    dump ();
	    restrack_assert (false);
	  }
	else
	  {
	    // tracker was aborted; we don't know if there are really leaks
	  }
	// remove resources
	m_resource_count -= map.size ();
      }
    // remove level from stack
    m_tracked_stack.pop_front ();

    // if no levels, resource count should be zero
    restrack_assert (!m_tracked_stack.empty () || m_resource_count == 0);
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

#endif // _RESOURCE_TRACKER_HPP_
