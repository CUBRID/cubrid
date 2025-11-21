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

#pragma once

#include <type_traits>
#include <utility>

template<class F>
class scope_exit
{
  public:
    using fun_t = std::decay_t<F>;

    // constructors
    explicit constexpr scope_exit (F &&f) noexcept (std::is_nothrow_constructible_v<fun_t, F &&>)
      : active_ (true), f_ (std::forward<F> (f)) {}

    scope_exit (const scope_exit &) = delete;
    scope_exit &operator= (const scope_exit &) = delete;
    scope_exit &operator= (scope_exit &&) = delete; // avoid double-run on assign

    constexpr scope_exit (scope_exit &&other) noexcept (std::is_nothrow_move_constructible_v<fun_t>)
      : active_ (other.active_), f_ (std::move (other.f_))
    {
      other.release();
    }

    // destructor calls the functor if engaged
    ~scope_exit() noexcept (noexcept (std::declval<fun_t &>()()))
    {
      if (active_)
	{
	  f_();
	}
    }

    // control
    constexpr void release() noexcept
    {
      active_ = false;
    }
    [[nodiscard]] constexpr bool engaged() const noexcept
    {
      return active_;
    }

  private:
    bool active_{false};
    // [[no_unique_address]] fun_t f_; // EBO when possible <- use this line when C++20 is available later.
    fun_t f_;
};

// CTAD: scope_exit se{[]{}};
template<class F>
scope_exit (F) -> scope_exit<std::decay_t<F>>;

template<class F>
[[nodiscard]] constexpr auto make_scope_exit (F &&f) -> scope_exit<std::decay_t<F>>
{
  return scope_exit<std::decay_t<F>> (std::forward<F> (f));
}

