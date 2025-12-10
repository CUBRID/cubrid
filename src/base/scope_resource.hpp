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
 * scoped_resource.hpp
 */

#ifndef _SCOPED_RESOURCE_HPP_
#define _SCOPED_RESOURCE_HPP_

#include "scope_exit.hpp"

template <typename Res, typename Cleanup>
class scoped_resource
{
  public:
    scoped_resource (Res res, Cleanup&& cleanup)
      : m_res (res)
      , m_guard (std::forward<Cleanup>(cleanup))
    {}

    scoped_resource (const scoped_resource &) = delete;
    scoped_resource &operator= (const scoped_resource &) = delete;

    scoped_resource (scoped_resource &&other) noexcept
      : m_res (other.m_res)
      , m_guard (std::move (other.m_guard))
    {
      other.m_res = Res{};
    }

    ~scoped_resource() = default;

    Res get() const noexcept
    {
      return m_res;
    }
    explicit operator Res() const noexcept
    {
      return m_res;
    }

    void release() noexcept
    {
      m_guard.release();
    }

  private:
    Res m_res{};
    scope_exit<std::decay_t<Cleanup>> m_guard;
};

#endif