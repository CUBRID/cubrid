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
 * memory_alloc.h - Memory allocation module
 */

#ifndef _DB_PRIVATE_ALLOCATOR_HPP_
#define _DB_PRIVATE_ALLOCATOR_HPP_

#include "memory_alloc.h"

/* db_private_allocator -
 *
 * note:
 *
 *    1. Cannot be used with typenames T that overload operator &.
 */
template <typename T>
class db_private_allocator
{
public:
  /* typedefs */
  typedef T value_type;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef value_type& reference;
  typedef const value_type& const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  /* convert an allocator<T> to allocator<U> */
  template <typename U>
  struct rebind
  {
    typedef db_private_allocator<U> other;
  };

  inline explicit db_private_allocator (THREAD_ENTRY * thread_p) :
    m_thread_p(thread_p)
  {
#if defined (SERVER_MODE)
    if (m_thread_p == NULL)
      {
        /* try to provide the thread entry context to avoid this lookup */
        assert (false);
        m_thread_p = thread_get_thread_entry_info ();
      }
    m_heapid = m_thread_p->private_heap_id;
    /* also register the allocator in thread entry */
    m_thread_p->count_private_allocators++;
#endif /* SERVER_MODE */
  }
  inline ~db_private_allocator ()
  {
    /* deregister allocator from thread entry */
    m_thread_p->count_private_allocators--;
  }
  inline explicit db_private_allocator (const db_private_allocator & other)
  {
    m_thread_p = other.thread_p;
    /* also register the allocator in thread entry */
    m_thread_p->count_private_allocators++;
  }
  template <typename U>
  inline explicit db_private_allocator (const db_private_allocator<U> &other)
  {
    m_thread_p = other.thread_p;
    /* also register the allocator in thread entry */
    m_thread_p->count_private_allocators++;
  }

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
  inline pointer allocate (size_type count)
  {
#if defined (SERVER_MODE)
    /* todo: this check takes a few operations. what is better? to allow this overhead or to just add assert and assume
     *       we catch all issues? */
    if (m_heapid != m_thread_p->private_heap_id)
      {
        /* this is not something we should do! */
        assert (false);

        HL_HEAPID save_heapid = css_set_private_heap (m_thread_p, m_heapid);
        pointer p = reinterpret_cast<pointer> (db_private_alloc (m_thread_p, count * sizeof (T)));
        (void) css_set_private_heap (m_thread_p, save_heapid);
        return p;
      }
    else
#endif /* !SERVER_MODE */
      {
        return reinterpret_cast<pointer> (db_private_alloc (m_thread_p, count * sizeof (T)));
      }
  }
  inline void deallocate (pointer p, size_type ingored = 0)
  {
#if defined (SERVER_MODE)
    if (m_heapid != m_thread_p->private_heap_id)
      {
        assert (false);
        /* what am I gonna do on release mode? this is memory leak! */
        HL_HEAPID save_heapid = css_set_private_heap (m_thread_p, m_heapid);
        db_private_free (m_thread_p, p);
        (void) css_set_private_heap (m_thread_p, save_heapid);
      }
    else
#endif /* !SERVER_MODE */
      {
        db_private_free (m_thread_p, p);
      }
  }

  /* maximum number of allocations */
  size_type max_size () const
  {
    static const size_type DB_PRIVATE_ALLOCATOR_MAX_SIZE = 0x7FFFFFFF;
    return DB_PRIVATE_ALLOCATOR_MAX_SIZE / sizeof(T);
  }

private:
  /* todo: construction/destruction */
  inline void construct (pointer p, const_reference t);
  inline void destroy (pointer p);

  THREAD_ENTRY *m_thread_p;
  HL_HEAPID m_heapid;
};

/* interchangeable specializations */
template <typename T, typename U>
bool operator== (const db_private_allocator<T> &, const db_private_allocator<U> &)
{
  return true;
}

template <typename T, typename U>
bool operator!= (const db_private_allocator<T> &, const db_private_allocator<U> &)
{
  return false;
}

#endif /* _DB_PRIVATE_ALLOCATOR_HPP_ */
