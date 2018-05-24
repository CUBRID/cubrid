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
 * critical_section_tracker.hpp - interface to track and debug critical sections usage
 */

#ifndef _CRITICAL_SECTION_TRACKER_HPP_
#define _CRITICAL_SECTION_TRACKER_HPP_

#include "critical_section.h"

#include <cstdint>

namespace cubsync
{
  class critical_section_tracker
  {
    public:

      critical_section_tracker (bool enable = false);

      static const std::size_t MAX_REENTERS = 8;      // how many re-enters do we allow?

      void start (void);
      void stop (void);
      void clear_all (void);

      void on_enter_as_reader (int cs_index);
      void on_enter_as_writer (int cs_index);
      void on_promote (int cs_index);
      void on_demote (int cs_index);
      void on_exit (int cs_index);

    private:

      struct cstrack_entry
      {
	std::uint8_t m_enter_count;
	bool m_is_writer;
	bool m_is_demoted;

	cstrack_entry ();
      };

      bool is_started (void);
      void check (void);
      void check_csect_interdependencies (int cs_index);

      cstrack_entry m_cstrack_array[CRITICAL_SECTION_COUNT];
      bool m_enabled;
      std::uint8_t m_start_count;
  };

} // namespace cubsync

#endif // _CRITICAL_SECTION_TRACKER_HPP_
