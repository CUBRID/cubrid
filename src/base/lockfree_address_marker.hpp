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

//
// lock-free address marker
//

#ifndef _LOCKFREE_ADDRESS_MARKER_HPP_
#define _LOCKFREE_ADDRESS_MARKER_HPP_

#include <atomic>

namespace lockfree
{
  template <class T>
  class address_marker
  {
    public:
      address_marker ();
      address_marker (T *addr);

      bool is_marked () const;
      T *get_address () const;
      T *get_address_no_strip () const;

      static T *set_adress_mark (T *addr);
      static T *strip_address_mark (T *addr);
      bool is_address_marked (const T *addr);

      static T *atomic_strip_address_mark (T *addr);

    private:
      static const T *MARK = static_cast<T *> (0x1);

      std::atomic<T *> m_addr;
  };
} // namespace lockfree

//
// implementation
//

namespace lockfree
{
  //
  // address marker
  //
  template <class T>
  address_marker<T>::address_marker ()
    : m_addr { NULL }
  {
  }

  template <class T>
  address_marker<T>::address_marker (T *addr)
    : m_addr (addr)
  {
  }

  template <class T>
  bool
  address_marker<T>::is_marked () const
  {
    return is_address_marked (m_addr.load ());
  }

  template <class T>
  T *
  address_marker<T>::get_address () const
  {
    return strip_address_mark (m_addr.load ());
  }

  template <class T>
  T *
  address_marker<T>::get_address_no_strip () const
  {
    return m_addr.load ();
  }

  template <class T>
  T *
  address_marker<T>::set_adress_mark (T *addr)
  {
    return addr | MARK;
  }

  template <class T>
  T *
  address_marker<T>::strip_address_mark (T *addr)
  {
    return addr & (~MARK);
  }

  template <class T>
  bool
  address_marker<T>::is_address_marked (const T *addr)
  {
    return (addr & MARK) != 0;
  }

  template <class T>
  T *
  address_marker<T>::atomic_strip_address_mark (T *addr)
  {
    return address_marker (addr).get_address ();
  }
} // namespace lockfree
#endif // !_LOCKFREE_ADDRESS_MARKER_HPP_
