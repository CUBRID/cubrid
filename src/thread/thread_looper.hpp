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
 *
 */

#ifndef _THREAD_LOOPER_HPP_
#define _THREAD_LOOPER_HPP_

#include <chrono>
#include <cstdint>

namespace thread
{

// forward def
class waiter;

class looper
{
public:

  // constructors
  looper ();
  template<class Rep, class Period>
  looper (std::chrono::duration<Rep, Period>& fixed_period);
  template<class Rep, class Period, size_t Count>
  looper (std::array<std::chrono::duration<Rep, Period>, Count> periods);
  looper (looper & other);

  void put_to_sleep (waiter & waiter_arg);
  void reset (void);
  void stop (void);
  bool is_stopped (void) const;

private:
  
  // definitions
  typedef std::chrono::duration<std::uint64_t, std::nano> delta_time;
  enum class wait_pattern
  {
    FIXED_PERIODS,                // fixed periods
    INCREASING_PERIODS,           // increasing periods with each timeout
    INFINITE_WAITS,               // always infinite waits
  };

  static const size_t MAX_PERIODS = 3;

  wait_pattern m_wait_pattern;
  std::size_t m_periods_count;
  delta_time m_periods[MAX_PERIODS];
  
  std::size_t m_period_index;
  bool m_stop;
};

} // namespace thread

#endif // _THREAD_LOOPER_HPP_
