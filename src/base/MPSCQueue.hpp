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
 * MPSCQueue.hpp
 */

#ifndef _MPSCQueue_HPP_
#define _MPSCQueue_HPP_

#ident "$Id$"

#include <atomic>
#include <cassert>
#include <optional>

namespace cubbase
{
  template <typename T>
  class MPSCQueue
  {
    private:
      struct Node
	{
	  Node (const T &data);

	  T m_data;
	  std::atomic<Node *> m_next;
	};

    public:
      MPSCQueue ();
      ~MPSCQueue ();

      void enqueue (const T &item);
      std::optional<T> dequeue ();
      bool empty () const;

    private:
      std::atomic<Node *> m_head;
      Node* m_tail;
    };

  template <typename T>
  MPSCQueue<T>::Node::Node (const T &data) : 
      m_data (data),
      m_next (nullptr)
    {
    }

  template <typename T>
  MPSCQueue<T>::MPSCQueue ()
    {
      Node *dummy;

      dummy = new Node (T { });
      m_head.store (dummy);
      m_tail = dummy;
    }

  template <typename T>
  MPSCQueue<T>::~MPSCQueue ()
    {
      while (dequeue ().has_value ());
      delete m_tail;
    }
      
  template <typename T>
  void MPSCQueue<T>::enqueue (const T &item)
    {
      Node* prev, node;

      node = new Node (item);
      prev = m_head.exchange (node, std::memory_order_acq_rel);
      prev->m_next.store (node, std::memory_order_release);
    }
      
  template <typename T>
  std::optional<T> MPSCQueue<T>::dequeue ()
    {
      T result;
      Node* next;

      next = m_tail->m_next.load (std::memory_order_acquire);
      if (next == nullptr)
	{
	  return std::nullopt;
	}
      
      result = std::move (next->m_data);
      delete m_tail;
      m_tail = next;
      return result;
    }
      
  template <typename T>
  bool MPSCQueue<T>::empty () const
    {
      return m_tail->m_next.load (std::memory_order_acquire) == nullptr;
    }
}

#endif
