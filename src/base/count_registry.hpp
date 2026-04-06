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
 * count_registry.hpp
 */

#ifndef _COUNT_REGISTRY_HPP_
#define _COUNT_REGISTRY_HPP_

#include <string>
#include <variant>
#include <functional>

namespace cubbase
{
  template<typename... Ts> struct overloaded : Ts...
  {
    using Ts::operator ()...;
  };
  template<typename... Ts> overloaded (Ts...) -> overloaded<Ts...>;

  template <typename Tag>
  class count_registry
  {
    private:
      inline static count_registry *m_head = nullptr;

    public:
      inline static int total ()
      {
	count_registry *p;
	int sum = 0;

	for (p = m_head; p; p = p->m_next)
	  {
	    // type: %s, name: %s, entries: %d\n
	    // typeid (*p).name (), p->m_name.c_str (), p->get ()
	    sum += p->get ();
	  }
	return sum;
      }

      inline static std::size_t count ()
      {
	count_registry *p;
	std::size_t sum = 0;

	for (p = m_head; p; p = p->m_next)
	  {
	    sum++;
	  }
	return sum;
      }

    public:
      count_registry (std::string name, std::function<int ()> getter) :
	m_name (std::move (name)),
	m_getter (std::move (getter)),
	m_next (m_head)
      {
	m_head = this;
      }

      count_registry (std::string name, int count) :
	m_name (std::move (name)),
	m_getter (count),
	m_next (m_head)
      {
	m_head = this;
      }

      count_registry() = delete;
      ~count_registry () = default;

      count_registry (const count_registry &) = delete;
      count_registry &operator= (const count_registry &) = delete;

      count_registry (count_registry &&) = delete;
      count_registry &operator= (count_registry &&) = delete;

      void *operator new (size_t) = delete;

      int get () const
      {
	return std::visit (overloaded
	{
	  [] (int val)
	  {
	    return val;
	  },
	  [] (const std::function<int ()> &func)
	  {
	    return func ? func () : 0;
	  }
	}, m_getter);
      }

    private:
      std::string m_name;
      std::variant<std::function<int ()>, int> m_getter;
      count_registry *m_next;
  };
}

#endif /* _COUNT_REGISTRY_HPP_ */
