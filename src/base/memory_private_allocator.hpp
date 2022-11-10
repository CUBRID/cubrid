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

//
// memory_private_allocator.hpp - extension to memory_alloc.h private allocation
//

#ifndef _MEMORY_PRIVATE_ALLOCATOR_HPP_
#define _MEMORY_PRIVATE_ALLOCATOR_HPP_

#include "mem_block.hpp"
#include "memory_alloc.h"

#include <memory>
#include <functional>

// forward definitions
namespace cubthread
{
  class entry;
};

namespace cubmem
{
  /* private_allocator -
   *
   *  Implementation of C++ allocator concept using CUBRID private allocator.
   *
   *
   *
   *  Templates:
   *
   *      T: base allocation type.
   *
   *
   *  How to use:
   *
   *      Specialize template classes/functions that require dynamic memory allocation.
   *
   *
   *  note:
   *
   *    1. Cannot be used with typenames T that overload operator &
   */
  template <typename T>
  class private_allocator
  {
    public:
      /* standard allocator type definitions */
      typedef T value_type;
      typedef value_type *pointer;
      typedef const value_type *const_pointer;
      typedef value_type &reference;
      typedef const value_type &const_reference;
      typedef size_t size_type;
      typedef ptrdiff_t difference_type;

      /* convert an allocator<T> to allocator<U> */
      template <typename U>
      struct rebind
      {
	typedef private_allocator<U> other;
      };

      inline explicit private_allocator (cubthread::entry *thread_p = NULL);
      inline ~private_allocator ();
      inline explicit private_allocator (const private_allocator &other);
      template <typename U>
      inline explicit private_allocator (const private_allocator<U> &other);

      /* address */
      inline pointer address (reference r)
      {
	return &r;
      }
      inline const_pointer address (const_reference r)
      {
	return &r;
      }

      /* memory allocation */
      inline pointer allocate (size_type count);
      inline void deallocate (pointer p, size_type ignored = 0);

      /* maximum number of allocations */
      size_type max_size () const;

      /* construction/destruction */
      inline void construct (pointer p, const_reference t);
      inline void destroy (pointer p);

      /* db_private_alloc accessors */
      cubthread::entry *get_thread_entry () const;
      HL_HEAPID get_heapid () const;

    private:

      cubthread::entry *m_thread_p;
      HL_HEAPID m_heapid;
  };

  /* interchangeable specializations */
  template <typename T, typename U>
  bool
  operator== (const private_allocator<T> &, const private_allocator<U> &)
  {
    return true;
  }

  template <typename T, typename U>
  bool
  operator!= (const private_allocator<T> &, const private_allocator<U> &)
  {
    return false;
  }

  // private allocator helper functions
  HL_HEAPID get_private_heapid (cubthread::entry *&thread_p);
  void *private_heap_allocate (cubthread::entry *thread_p, HL_HEAPID heapid, size_t size);
  void private_heap_deallocate (cubthread::entry *thread_p, HL_HEAPID heapid, void *ptr);
  void register_private_allocator (cubthread::entry *thread_p);
  void deregister_private_allocator (cubthread::entry *thread_p);

  // private_pointer_deleter - deleter for pointer allocated with private allocator
  //
  template <class T>
  class private_pointer_deleter
  {
    public:
      private_pointer_deleter ();
      private_pointer_deleter (cubthread::entry *thread_p);

      inline void operator () (T *ptr) const;

    private:
      cubthread::entry *thread_p;
  };

  // private_unique_ptr - unique pointer allocated with private allocator
  //
  template <class T>
  class private_unique_ptr
  {
    public:
      private_unique_ptr (T *ptr, cubthread::entry *thread_p);

      inline T *get () const;   // get and keep ownership
      inline T *release ();     // get and release ownership

      inline void swap (private_unique_ptr <T> &other);

      inline void reset (T *ptr);

      inline T *operator-> () const;
      inline T &operator* () const;

    private:
      std::unique_ptr<T, private_pointer_deleter<T>> m_smart_ptr;
  };

  extern const block_allocator PRIVATE_BLOCK_ALLOCATOR;

  // Calls func with the global heap set for allocating memory
  template <typename Func, typename ...Args>
  inline void switch_to_global_allocator_and_call (Func &&func, Args &&... args);

} // namespace cubmem

//////////////////////////////////////////////////////////////////////////
//
// inline/template implementation
//
//////////////////////////////////////////////////////////////////////////

namespace cubmem
{
  //
  // private_allocator
  //
  template <typename T>
  private_allocator<T>::private_allocator (cubthread::entry *thread_p /* = NULL */)
    : m_thread_p (thread_p)
  {
    m_heapid = get_private_heapid (m_thread_p);
    register_private_allocator (m_thread_p);
  }

  template <typename T>
  private_allocator<T>::private_allocator (const private_allocator &other)
  {
    m_thread_p = other.m_thread_p;
    m_heapid = other.m_heapid;
    register_private_allocator (m_thread_p);
  }

  template <typename T>
  template <typename U>
  private_allocator<T>::private_allocator (const private_allocator<U> &other)
  {
    m_thread_p = other.get_thread_entry ();
    m_heapid = other.get_heapid ();
    register_private_allocator (m_thread_p);
  }

  template <typename T>
  private_allocator<T>::~private_allocator ()
  {
    deregister_private_allocator (m_thread_p);
  }

  template <typename T>
  typename private_allocator<T>::pointer
  private_allocator<T>::allocate (size_type count)
  {
    return reinterpret_cast<T *> (private_heap_allocate (m_thread_p, m_heapid, count * sizeof (T)));
  }

  template <typename T>
  void
  private_allocator<T>::deallocate (pointer p, size_type ignored)
  {
    (void) ignored; // unused
    private_heap_deallocate (m_thread_p, m_heapid, p);
  }

  template <typename T>
  typename private_allocator<T>::size_type
  private_allocator<T>::max_size () const
  {
    const size_type DB_PRIVATE_ALLOCATOR_MAX_SIZE = 0x7FFFFFFF;
    return DB_PRIVATE_ALLOCATOR_MAX_SIZE / sizeof (T);
  }

  template <typename T>
  void
  private_allocator<T>::construct (pointer p, const_reference t)
  {
    new (p) value_type (t);
  }

  template <typename T>
  void
  private_allocator<T>::destroy (pointer p)
  {
    p->~value_type ();
  }

  template <typename T>
  cubthread::entry *
  private_allocator<T>::get_thread_entry () const
  {
    return m_thread_p;
  }

  template <typename T>
  HL_HEAPID
  private_allocator<T>::get_heapid () const
  {
    return m_heapid;
  }

  //
  // private_pointer_deleter
  //
  template <class T>
  private_pointer_deleter<T>::private_pointer_deleter ()
    : thread_p (NULL)
  {
  }

  template <class T>
  private_pointer_deleter<T>::private_pointer_deleter (cubthread::entry *thread_p)
    : thread_p (thread_p)
  {
  }

  template <class T>
  void
  private_pointer_deleter<T>::operator () (T *ptr) const
  {
    if (ptr != NULL)
      {
	db_private_free (thread_p, ptr);
      }
  }

  //
  // private_unique_ptr
  //
  template <class T>
  private_unique_ptr<T>::private_unique_ptr (T *ptr, cubthread::entry *thread_p)
  {
    m_smart_ptr = std::unique_ptr<T, private_pointer_deleter<T>> (ptr, private_pointer_deleter<T> (thread_p));
  }

  template <class T>
  T *
  private_unique_ptr<T>::get () const
  {
    return m_smart_ptr.get ();
  }
  template <class T>
  T *
  private_unique_ptr<T>::release ()
  {
    return m_smart_ptr.release ();
  }

  template <class T>
  void
  private_unique_ptr<T>::swap (private_unique_ptr<T> &other)
  {
    m_smart_ptr.swap (other.m_smart_ptr);
  }

  template <class T>
  void
  private_unique_ptr<T>::reset (T *ptr)
  {
    m_smart_ptr.reset (ptr);
  }

  template <class T>
  T *
  private_unique_ptr<T>::operator-> () const
  {
    return m_smart_ptr.get ();
  }

  template <class T>
  T &
  private_unique_ptr<T>::operator* () const
  {
    return *m_smart_ptr.get ();
  }

  template < typename Func, typename...Args >
  void
  switch_to_global_allocator_and_call (Func &&func, Args &&... args)
  {
    HL_HEAPID save_id;

    // Switch to global
    save_id = db_change_private_heap (NULL, 0);

    func (std::forward < Args > (args)...);

    // Switch back
    (void) db_change_private_heap (NULL, save_id);
  }
} // namespace cubmem



#endif // _MEMORY_PRIVATE_ALLOCATOR_HPP_
