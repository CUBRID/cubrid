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
 * timers.hpp - time stuff maybe to compare performance
 */

#ifndef _TEST_TIMERS_HPP_
#define _TEST_TIMERS_HPP_

#include <chrono>

namespace test_common
{

/* timer - time durations between checkpoints.
 *
 *  Templates:
 *
 *      Units - desired duration unit.
 */
template <typename Units>
class timer
{
  public:
    inline timer ()
    {
      reset ();
    }

    inline Units time ()
    {
      return (std::chrono::duration_cast<Units> (get_now () - m_saved_time));
    }

    inline void reset ()
    {
      m_saved_time = get_now ();
    }

    inline Units time_and_reset ()
    {
      Units diff = time ();
      reset ();
      return diff;
    }

  private:

    static inline std::chrono::system_clock::time_point get_now (void)
    {
      return std::chrono::system_clock::now ();
    }

    std::chrono::system_clock::time_point m_saved_time;
};

/* Specialization for microseconds and milliseconds */
typedef class timer<std::chrono::milliseconds> ms_timer;
typedef class timer<std::chrono::microseconds> us_timer;

} // namespace test_common

#endif // _TEST_TIMERS_HPP_
