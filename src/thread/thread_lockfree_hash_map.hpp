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
//  specialize lock-free hash map to thread manager and its entries
//

#ifndef _THREAD_LOCKFREE_HASH_MAP_
#define _THREAD_LOCKFREE_HASH_MAP_

#include "lock_free.h"  // old implementation

namespace cubthread
{
  template <class T>
  class lockfree_hashmap
  {
    public:
      lockfree_hashmap ();

      void init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
			lf_entry_descriptor &edesc);
      void init_as_new ();

      void destroy ();

    private:
      class old_hashmap;
      class new_hashmap;

      bool is_old_type () const;

      enum type
      {
	OLD,
	NEW,
	UNKNOWN
      };

      old_hashmap m_old_hash;
      new_hashmap m_new_hash;
      type m_type;
  };

  template <class T>
  class lockfree_hashmap<T>::old_hashmap
  {
    public:
      old_hashmap ();

      void init (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
		 lf_entry_descriptor &edes);
      void destroy ();

    private:
      lf_freelist m_freelist;
      lf_hash_table m_hash;
  };

  template <class T>
  class lockfree_hashmap<T>::new_hashmap
  {
    public:
      new_hashmap () = default;

      void init () {}
      void destroy () {}

    private:
  };
} // namespace cubthread

//
// implementation
//

namespace cubthread
{
  //
  //  lockfree_hashmap
  //
  template <class T>
  lockfree_hashmap<T>::lockfree_hashmap ()
    : m_old_hash {}
    , m_new_hash {}
    , m_type (UNKNOWN)
  {
  }

  template <class T>
  void
  lockfree_hashmap<T>::init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count,
				    int freelist_block_size, lf_entry_descriptor &edesc)
  {
    m_type = OLD;
    m_old_hash.init (transys, hash_size, freelist_block_count, freelist_block_size);
  }

  template <class T>
  void
  lockfree_hashmap<T>::init_as_new ()
  {
    // not implemented
    assert (false);
  }

  template <class T>
  void
  lockfree_hashmap<T>::destroy ()
  {
    if (is_old_type ())
      {
	m_old_hash.destroy ();
      }
    else
      {
	m_new_hash.destroy ();
      }
  }

  template <class T>
  bool
  lockfree_hashmap<T>::is_old_type () const
  {
    // for now, always true
    return true;
  }

  //
  // lockfree_hashmap::old_hashmap
  //
  template <class T>
  lockfree_hashmap<T>::old_hashmap::old_hashmap ()
    : m_freelist LF_FREELIST_INITIALIZER
    , m_hash LF_HASH_TABLE_INITIALIZER
  {
  }

  template <class T>
  void
  lockfree_hashmap<T>::old_hashmap::init (lf_tran_system &transys, int hash_size, int freelist_block_count,
					  int freelist_block_size, lf_entry_descriptor &edesc)
  {
    if (lf_freelist_init (&m_freelist, freelist_block_count, freelist_block_size, &edesc, &transys) != NO_ERROR)
      {
	assert (false);
	return;
      }
    if (lf_hash_init (&m_hash, &m_freelist, hash_size, &edesc) != NO_ERROR)
      {
	assert (false);
	return;
      }
  }

  template <class T>
  void
  lockfree_hashmap<T>::old_hashmap::destroy ()
  {
    lf_hash_destroy (&m_hash);
    lf_freelist_destroy (&m_freelist);
  }
}

#endif // !_THREAD_LOCKFREE_HASH_MAP_
