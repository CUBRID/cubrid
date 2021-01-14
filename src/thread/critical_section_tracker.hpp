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
