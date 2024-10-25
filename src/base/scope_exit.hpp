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
 * scope_exit.hpp
 */

#ifndef _SCOPE_EXIT_HPP_
#define _SCOPE_EXIT_HPP_

// Scope Guard: execute something when it goes out of scope (no extra cost when optimization is enabled)
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
