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
// lock-free address marker
//

#ifndef _LOCKFREE_ADDRESS_MARKER_HPP_
#define _LOCKFREE_ADDRESS_MARKER_HPP_

#include <atomic>
#include <cstdint>

namespace lockfree
{
  template <class T>
  class address_marker
  {
    private:
      using convert_type = std::uint64_t;
      static const convert_type MARK = 0x1;

      static convert_type to_cnv_type (T *addr);
      static T *to_addr (convert_type ct);

    public:
      address_marker ();
      address_marker (T *addr);

      bool is_marked () const;
      T *get_address () const;
      T *get_address_no_strip () const;

      static T *set_adress_mark (T *addr);
      static T *strip_address_mark (T *addr);
      static bool is_address_marked (T *addr);

      static T *atomic_strip_address_mark (T *addr);

    private:
      static bool is_ct_marked (convert_type ct);
      static convert_type set_ct_mark (convert_type ct);
      static convert_type strip_ct_mark (convert_type ct);

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
  typename address_marker<T>::convert_type
  address_marker<T>::to_cnv_type (T *addr)
  {
    return (convert_type) addr;
  }

  template <class T>
  T *
  address_marker<T>::to_addr (convert_type ct)
  {
    return (T *) ct;
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
  bool
  address_marker<T>::is_ct_marked (convert_type ct)
  {
    return (ct & MARK) != 0;
  }

  template <class T>
  typename address_marker<T>::convert_type
  address_marker<T>::set_ct_mark (convert_type ct)
  {
    return (ct | MARK);
  }

  template <class T>
  typename address_marker<T>::convert_type
  address_marker<T>::strip_ct_mark (convert_type ct)
  {
    return (ct & (~MARK));
  }

  template <class T>
  T *
  address_marker<T>::set_adress_mark (T *addr)
  {
    return to_addr (set_ct_mark (to_cnv_type (addr)));
  }

  template <class T>
  T *
  address_marker<T>::strip_address_mark (T *addr)
  {
    return to_addr (strip_ct_mark (to_cnv_type (addr)));
  }

  template <class T>
  bool
  address_marker<T>::is_address_marked (T *addr)
  {
    return is_ct_marked (to_cnv_type (addr));
  }

  template <class T>
  T *
  address_marker<T>::atomic_strip_address_mark (T *addr)
  {
    return address_marker (addr).get_address ();
  }
} // namespace lockfree
#endif // !_LOCKFREE_ADDRESS_MARKER_HPP_
