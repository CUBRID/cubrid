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
 * scope_exit.hpp
 */

#ifndef _SCOPE_EXIT_HPP_
#define _SCOPE_EXIT_HPP_

// Scope Guard: execute something when it goes out of scope (no extra cost when optimization is enabled)
#include <functional>
#include <type_traits>
#include <utility>

template<typename Callable>//Callable=function, function address, function pointer, functor, lambda, std::function
class scope_exit final
{
  public:
    explicit scope_exit (Callable &&callable) noexcept
      : m_valid (true)
      , m_callable (std::is_lvalue_reference<decltype (callable)>::value ? callable : std::forward<Callable> (callable))
    {}

    scope_exit (scope_exit &&sg) noexcept
      : m_valid (sg.m_valid)
      , m_callable (sg.m_callable)
    {
      sg.m_valid = false;
    }

    ~scope_exit() noexcept
    {
      if (m_valid)
	{
	  m_callable();
	}
    }

    void release() noexcept
    {
      m_valid = false;
    }
  private:
    using func_t = std::function<void (void)>;

    bool m_valid;
    func_t m_callable;

    scope_exit() = delete;
    scope_exit (scope_exit &) = delete;
    scope_exit &operator= (const scope_exit &) = delete;
    scope_exit &operator= (scope_exit &&) = delete;
};

#endif /* _SCOPE_EXIT_HPP_ */
