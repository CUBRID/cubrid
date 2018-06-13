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
 * critical_section_tracker.cpp - implementation for tracking and debugging critical sections usage
 */

#include "critical_section_tracker.hpp"

#include "resource_tracker.hpp"

#include <iostream>

namespace cubsync
{
  //////////////////////////////////////////////////////////////////////////
  //
  //////////////////////////////////////////////////////////////////////////

  static void
  cstrack_assert (bool cond)
  {
    cubbase::restrack_assert (cond);
  }

  critical_section_tracker::cstrack_entry::cstrack_entry (void)
    : m_enter_count (0)
    , m_is_writer (false)
    , m_is_demoted (false)
  {
    //
  }

  critical_section_tracker::critical_section_tracker (bool enable /* = false */)
    : m_cstrack_array {}
    , m_enabled (enable)
    , m_start_count (0)
  {
    //
  }

  void
  critical_section_tracker::on_enter_as_reader (int cs_index)
  {
    if (!m_enabled || !is_started ())
      {
	return;
      }

    check_csect_interdependencies (cs_index);

    cstrack_entry &cs_entry = m_cstrack_array[cs_index];

    // is first enter?
    if (cs_entry.m_enter_count == 0)
      {
	cstrack_assert (!cs_entry.m_is_writer && !cs_entry.m_is_demoted);
	cs_entry.m_enter_count++;
	return;
      }
    // is not first enter.
    assert (cs_entry.m_enter_count < MAX_REENTERS);

    if (cs_entry.m_is_writer)
      {
	// already entered as writer
	// this case is allowed. if I am a writer, I will not be blocked as readers
	cs_entry.m_enter_count++;
      }
    else if (cs_entry.m_is_demoted)
      {
	// I am not entirely sure why we allow when lock is demoted.
	cs_entry.m_enter_count++;
      }
    else
      {
	// re-enter is not accepted, since it may cause deadlocks.
	cs_entry.m_enter_count++;
	cstrack_assert (false);
      }
  }

  void
  critical_section_tracker::on_enter_as_writer (int cs_index)
  {
    if (!m_enabled || !is_started ())
      {
	return;
      }

    cstrack_entry &cs_entry = m_cstrack_array[cs_index];

    check_csect_interdependencies (cs_index);

    // is first enter?
    if (cs_entry.m_enter_count == 0)
      {
	cstrack_assert (!cs_entry.m_is_writer && !cs_entry.m_is_demoted);
	cs_entry.m_is_writer = true;
	cs_entry.m_enter_count++;
	return;
      }
    // is not first enter.

    // re-enter is only accepted if it was already writer
    cstrack_assert (cs_entry.m_is_writer);
    cs_entry.m_enter_count++;
  }

  void
  critical_section_tracker::on_promote (int cs_index)
  {
    if (!m_enabled || !is_started ())
      {
	return;
      }

    cstrack_entry &cs_entry = m_cstrack_array[cs_index];

    // I must be reader
    cstrack_assert (!cs_entry.m_is_writer);
    cstrack_assert (cs_entry.m_enter_count == 1);

    cs_entry.m_is_writer = true;
    cs_entry.m_is_demoted = false;
  }

  void
  critical_section_tracker::on_demote (int cs_index)
  {
    if (!m_enabled || !is_started ())
      {
	return;
      }

    cstrack_entry &cs_entry = m_cstrack_array[cs_index];

    // I must be writer
    cstrack_assert (cs_entry.m_is_writer);
    cstrack_assert (cs_entry.m_enter_count == 1);

    cs_entry.m_is_writer = false;
    cs_entry.m_is_demoted = true;
  }

  void
  critical_section_tracker::on_exit (int cs_index)
  {
    if (!m_enabled || !is_started ())
      {
	return;
      }

    cstrack_entry &cs_entry = m_cstrack_array[cs_index];

    if (cs_entry.m_enter_count == 0)
      {
	cstrack_assert (false);
	return;
      }

    --cs_entry.m_enter_count;
    if (cs_entry.m_enter_count == 0)
      {
	cs_entry.m_is_demoted = false;
	cs_entry.m_is_writer = false;
      }
  }

  void
  critical_section_tracker::check_csect_interdependencies (int cs_index)
  {
    if (cs_index == CSECT_LOCATOR_SR_CLASSNAME_TABLE)
      {
	cstrack_assert (m_cstrack_array[CSECT_CT_OID_TABLE].m_enter_count == 0);
      }
  }

  bool
  critical_section_tracker::is_started (void)
  {
    return m_start_count > 0;
  }

  void
  critical_section_tracker::start (void)
  {
    if (!m_enabled)
      {
	return;
      }
    m_start_count++;
  }

  void
  critical_section_tracker::stop (void)
  {
    if (!m_enabled)
      {
	return;
      }

    if (m_start_count == 0)
      {
	cstrack_assert (false);
      }
    else
      {
	m_start_count--;
      }

    if (m_start_count == 0)
      {
	// check no critical section is entered
	clear_all ();
      }
  }

  void
  critical_section_tracker::check (void)
  {
    bool m_printed_header = false;
    std::ostream &os = std::cerr;

    for (int cs_index = 0; cs_index < CRITICAL_SECTION_COUNT; cs_index++)
      {
	if (m_cstrack_array[cs_index].m_enter_count > 0)
	  {
	    if (!m_printed_header)
	      {
		os << "   +--- Critical Sections" << std::endl;
		m_printed_header = true;
	      }
	    os << "     +--- " << csect_name_at (cs_index) << std::endl;
	    os << "       +--- enter count = " << m_cstrack_array[cs_index].m_enter_count << std::endl;
	    os << "       +--- is writer = " << m_cstrack_array[cs_index].m_is_writer << std::endl;
	    os << "       +--- is demoted = " << m_cstrack_array[cs_index].m_is_demoted << std::endl;

	    cstrack_assert (false);
	  }
      }
  }

  void
  critical_section_tracker::clear_all (void)
  {
    check ();
    for (int cs_index = 0; cs_index < CRITICAL_SECTION_COUNT; cs_index++)
      {
	// reset
	m_cstrack_array[cs_index] = {};
      }
    m_start_count = 0;
  }
} // namespace cubsync
