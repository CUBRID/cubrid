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
// memory_reference_store.hpp - extension to handle permanent & temporary ownership
//

#ifndef _MEMORY_REFERENCE_STORE_HPP_
#define _MEMORY_REFERENCE_STORE_HPP_

#include <algorithm>

namespace cubmem
{
  template <typename T>
  class reference_store
  {
    public:
      reference_store ();
      reference_store &operator= (reference_store &&);

      reference_store (reference_store &) = delete;
      reference_store &operator= (reference_store &) = delete;

      const T *get_immutable () const;
      bool is_null () const;
      bool is_mutable () const;
      T *get_mutable () const;
      T *release_mutable_reference ();

      void create_mutable_reference ();
      void set_immutable_reference (T *ptr);
      void set_mutable_reference (T *ptr);

      void clear ();
      ~reference_store ();
    private:
      void delete_mutable ();

      const T *m_immutable_reference;
      T *m_mutable_reference;
  };
}

namespace cubmem
{
  template <class T>
  reference_store<T>::reference_store ()
    : m_immutable_reference (nullptr)
    , m_mutable_reference (nullptr)
  {

  }

  template <class T>
  reference_store<T> &
  reference_store<T>::operator= (reference_store<T> &&other)
  {
    if (this != &other)
      {
	std::swap (m_mutable_reference, other.m_mutable_reference);
	std::swap (m_immutable_reference, other.m_immutable_reference);
      }
    return *this;
  }

  template <class T>
  const T *
  reference_store<T>::get_immutable () const
  {
    return m_immutable_reference;
  }

  template <class T>
  bool
  reference_store<T>::is_null () const
  {
    return m_immutable_reference == nullptr;
  }

  template <class T>
  bool
  reference_store<T>::is_mutable () const
  {
    return m_mutable_reference != nullptr;
  }

  template <class T>
  T *
  reference_store<T>::get_mutable () const
  {
    assert (is_mutable ());
    return m_mutable_reference;
  }

  template <class T>
  T *
  reference_store<T>::release_mutable_reference ()
  {
    assert (is_mutable ());
    T *ret_ref = m_mutable_reference;
    m_immutable_reference = m_mutable_reference = nullptr;
    return ret_ref;
  }

  template <class T>
  void
  reference_store<T>::set_immutable_reference (T *ptr)
  {
    if (ptr != m_mutable_reference)
      {
	// we should not clear the doc we assign to ourselves
	clear ();
      }

    m_mutable_reference = nullptr;
    m_immutable_reference = ptr;
  }

  template <class T>
  void
  reference_store<T>::set_mutable_reference (T *ptr)
  {
    if (ptr != m_mutable_reference)
      {
	clear ();
	m_immutable_reference = m_mutable_reference = ptr;
      }
  }

  template <class T>
  void
  reference_store<T>::clear ()
  {
    if (is_mutable ())
      {
	delete_mutable ();
	m_mutable_reference = nullptr;
      }
    m_immutable_reference = nullptr;
  }

  template <class T>
  reference_store<T>::~reference_store ()
  {
    clear ();
  }
}

#endif // _MEMORY_REFERENCE_STORE_HPP_
