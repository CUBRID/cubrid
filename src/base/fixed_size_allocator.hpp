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

#ifndef _FIXED_SIZE_ALLOCATOR_HPP_
#define _FIXED_SIZE_ALLOCATOR_HPP_

#include "thread_compat.hpp"
#include "memory_private_allocator.hpp"

namespace cubmem
{
  namespace fixed_size_alloc
  {
    template <typename T>
    using private_vector = std::vector<T, private_allocator<T>>;
    constexpr size_t align_size = 16;
    constexpr size_t allocate_node_unit = 256;
    template <typename T, bool is_private>
    class allocator
    {
    };
    template <typename T>
    struct node
    {
      node<T> *m_next;

      alignas (align_size) char m_data[sizeof (T)];

      static constexpr size_t get_size()
      {
	constexpr size_t size = sizeof (node<T> *) + sizeof (T);
	return (size + align_size-1) & ~static_cast<size_t> (align_size-1);
      }

      static node<T> *from_data (void *data_ptr)
      {
	return reinterpret_cast<node<T> *> (
		       reinterpret_cast<char *> (data_ptr) - offsetof (node<T>, m_data)
	       );
      }
    };
    template <typename T>
    struct block
    {
      node<T> nodes[allocate_node_unit];
    };
    template <typename T>
    class allocator<T, false>
    {
      public:
	allocator();
	~allocator();
	void *allocate ();
	void deallocate (void *ptr);
	void expand();

      private:
	std::vector<std::unique_ptr<block<T>>> m_blocks;
	node<T> *m_free_head;
    };
    template <typename T>
    class allocator<T, true>
    {
      public:
	allocator (THREAD_ENTRY *thread_p);
	~allocator();
	void *allocate ();
	void deallocate (void *ptr);
	void expand();
      private:
	THREAD_ENTRY *m_thread_p;
	private_allocator<block<T>> m_allocator;
	std::vector<std::shared_ptr<block<T>>> m_blocks;
	node<T> *m_free_head;
    };

    template <typename T>
    allocator<T, false>::allocator()
      : m_blocks ()
      , m_free_head (nullptr)
    {
    }

    template <typename T>
    allocator<T, false>::~allocator()
    {
      m_blocks.clear();
      m_free_head = nullptr;
    }

    template <typename T>
    void allocator<T, false>::expand()
    {
      size_t old_size = m_blocks.size(), new_size = 0;
      if (old_size == 0)
	{
	  new_size = 1;
	}
      else
	{
	  new_size = old_size * 2;
	}
      size_t blocks_to_add = new_size - old_size;
      m_blocks.reserve (new_size);
      node<T> *prev_node = nullptr;
      node<T> *new_free_head = nullptr;
      for (size_t i = 0; i < blocks_to_add; i++)
	{
	  m_blocks.push_back (std::make_unique<block<T>>());
	  for (node<T> &node : m_blocks.back()->nodes)
	    {
	      if (new_free_head == nullptr)
		{
		  new_free_head = &node;
		}
	      if (prev_node == nullptr)
		{
		  prev_node = &node;
		}
	      else
		{
		  prev_node->m_next = &node;
		  prev_node = &node;
		}
	    }
	}
      /* last one */
      if (prev_node != nullptr)
	{
	  prev_node->m_next = nullptr;
	}
      assert (m_free_head == nullptr);
      m_free_head = new_free_head;
    }

    template <typename T>
    void *allocator<T, false>::allocate()
    {
      if (m_free_head == nullptr)
	{
	  expand();
	}
      assert (m_free_head != nullptr);
      node<T> *node = m_free_head;
      m_free_head = m_free_head->m_next;

      return static_cast<void *> (node->m_data);
    }

    template <typename T>
    void allocator<T, false>::deallocate (void *ptr)
    {
      assert (ptr != nullptr);
      node<T> *freed_node = node<T>::from_data (ptr);
      freed_node->m_next = m_free_head;
      m_free_head = freed_node;
    }

    template <typename T>
    allocator<T, true>::allocator (THREAD_ENTRY *thread_p)
      : m_thread_p (thread_p)
      , m_allocator (thread_p)
      , m_blocks ()
      , m_free_head (nullptr)
    {
    }

    template <typename T>
    allocator<T, true>::~allocator()
    {
      m_blocks.clear();
      m_free_head = nullptr;
    }

    template <typename T>
    void allocator<T, true>::expand()
    {
      size_t old_size = m_blocks.size(), new_size = 0;
      if (old_size == 0)
	{
	  new_size = 1;
	}
      else
	{
	  new_size = old_size * 2;
	}
      size_t blocks_to_add = new_size - old_size;
      m_blocks.reserve (new_size);
      node<T> *prev_node = nullptr;
      node<T> *new_free_head = nullptr;
      for (size_t i = 0; i < blocks_to_add; i++)
	{
	  void *raw_mem = m_allocator.allocate (1);
	  auto deleter = [alloc = &m_allocator] (block<T> *ptr)
	  {
	    ptr->~block();
	    alloc->deallocate (ptr);
	  };
	  m_blocks.push_back (std::shared_ptr<block<T>> (new (raw_mem) block<T>(), deleter));
	  for (node<T> &node : m_blocks.back()->nodes)
	    {
	      if (new_free_head == nullptr)
		{
		  new_free_head = &node;
		}
	      if (prev_node == nullptr)
		{
		  prev_node = &node;
		}
	      else
		{
		  prev_node->m_next = &node;
		  prev_node = &node;
		}
	    }
	}
      /* last one */
      if (prev_node != nullptr)
	{
	  prev_node->m_next = m_free_head;
	}
      m_free_head = new_free_head;
    }

    template <typename T>
    void *allocator<T, true>::allocate()
    {
      if (m_free_head == nullptr)
	{
	  expand();
	}
      assert (m_free_head != nullptr);
      node<T> *node = m_free_head;
      m_free_head = m_free_head->m_next;

      return static_cast<void *> (node->m_data);
    }

    template <typename T>
    void allocator<T, true>::deallocate (void *ptr)
    {
      assert (ptr != nullptr);
      node<T> *freed_node = node<T>::from_data (ptr);
      freed_node->m_next = m_free_head;
      m_free_head = freed_node;
    }
  }
}

#endif
