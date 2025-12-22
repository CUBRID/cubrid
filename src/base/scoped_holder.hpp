/*
 *
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
 * scoped_holder.hpp
 */

#ifndef _SCOPED_HOLDER_HPP_
#define _SCOPED_HOLDER_HPP_

#include "scope_exit.hpp"

struct scoped_noop
{
  template <typename P>
  void operator() (P &) const noexcept {}
};

template <typename Data, typename Cleanup>
class scoped_holder
{
  public:

    static_assert (std::is_invocable_v<Cleanup, Data &>,
		   "Cleanup must be callable with Data&");

    using data_t = Data;
    using cleanup_t = std::decay_t<Cleanup>;

  private:
    struct cleanup_state_t
    {
      data_t *data;
      cleanup_t cleanup;

      void operator()() noexcept (noexcept (std::declval<cleanup_t &>() (*std::declval<data_t *&>())))
      {
	if (data)
	  {
	    cleanup (*data);
	  }
      }
    };
    using guard_t = scope_exit<cleanup_state_t>;

  public:
    scoped_holder () = delete;

    scoped_holder (data_t data, Cleanup &&cleanup)
      : m_data (std::move (data))
      , m_guard (cleanup_state_t {std::addressof (m_data), cleanup_t (std::forward<Cleanup> (cleanup))})
    {
    }

    ~scoped_holder() = default;

    // non-copyable
    scoped_holder (const scoped_holder &) = delete;
    scoped_holder &operator= (const scoped_holder &) = delete;

    // move-constructible
    scoped_holder (scoped_holder &&other) noexcept (
	    std::is_nothrow_move_constructible_v<data_t> &&std::is_nothrow_move_constructible_v<cleanup_t>)
      : m_data (std::move (other.m_data))
      , m_guard (
		cleanup_state_t {std::addressof (m_data), std::move (other.m_guard.functor ().cleanup)})
    {
      other.m_guard.functor ().data = nullptr;
      other.m_guard.release();
    }

    // remove move-assignment
    scoped_holder &operator= (scoped_holder &&) = delete;

    data_t &get() noexcept
    {
      return m_data;
    }

    const data_t &get() const noexcept
    {
      return m_data;
    }

    data_t *operator->() noexcept
    {
      return std::addressof (m_data);
    }

    const data_t *operator->() const noexcept
    {
      return std::addressof (m_data);
    }

    data_t &operator*() noexcept
    {
      return m_data;
    }

    const data_t &operator*() const noexcept
    {
      return m_data;
    }

    explicit operator data_t &() noexcept
    {
      return m_data;
    }

    explicit operator const data_t &() const noexcept
    {
      return m_data;
    }

    void release() noexcept
    {
      m_guard.functor ().data = nullptr;
      m_guard.release();
    }

  private:
    data_t m_data;
    guard_t m_guard;
};

#if 0
// do we need this helper function?
// for future use.
template <typename Data, typename Cleanup>
auto make_scoped_holder (Data data, Cleanup &&cleanup)
-> scoped_holder<Data, std::decay_t<Cleanup>>
{
  return scoped_holder<Data, std::decay_t<Cleanup>> (std::move (data),
	 std::forward<Cleanup> (cleanup));
}
#endif

#endif
