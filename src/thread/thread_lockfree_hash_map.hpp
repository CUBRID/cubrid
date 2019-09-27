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
#include "lockfree_transaction_def.hpp"
#include "thread_entry.hpp"

namespace cubthread
{
  template <class Key, class T>
  class lockfree_hashmap
  {
    public:
      lockfree_hashmap ();

      class iterator;

      void init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
			lf_entry_descriptor &edesc, int entry_idx);
      void init_as_new ();
      void destroy ();

      T *find (cubthread::entry *thread_p, Key &key);
      bool find_or_insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert_given (cubthread::entry *thread_p, Key &key, T *&t);
      bool erase (cubthread::entry *thread_p, Key &key);
      bool erase_locked (cubthread::entry *thread_p, Key &key, T *&t);

      void unlock (cubthread::entry *thread_p, T *&t);

      void clear (cubthread::entry *thread_p);

    private:
      class new_hashmap;

      bool is_old_type () const;
      lf_tran_entry *get_tran_entry (cubthread::entry *thread_p);

      enum type
      {
	OLD,
	NEW,
	UNKNOWN
      };

      lf_hash_table_cpp m_old_hash;
      new_hashmap m_new_hash;
      type m_type;
      int m_entry_idx;
  };

  template <class Key, class T>
  class lockfree_hashmap<Key, T>::new_hashmap
  {
    public:
      new_hashmap () = default;

      class iterator
      {
	public:
	  iterator ();
	  iterator (lockfree::tran::index tran_index, lockfree_hashmap &map) {}
	  ~iterator ();

	  T *
	  iterate ()
	  {
	    return NULL;
	  }
      };

      void init () {}
      void destroy () {}

      T *
      find (lockfree::tran::index tran_index, Key &key)
      {
	return NULL;
      }
      bool
      find_or_insert (lockfree::tran::index tran_index, Key &key, T *&t)
      {
	return false;
      }
      bool
      insert (lockfree::tran::index tran_index, Key &key, T *&t)
      {
	return false;
      }
      bool
      insert_given (lockfree::tran::index tran_index, Key &key, T *&t)
      {
	return false;
      }
      bool
      erase (lockfree::tran::index tran_index, Key &key)
      {
	return false;
      }
      bool
      erase_locked (lockfree::tran::index tran_index, Key &key, T *&t)
      {
	return false;
      }

      void unlock (lockfree::tran::index tran_index, T *&t) {}
      void clear (lockfree::tran::index tran_index) {}

    private:
  };

  template <class Key, class T>
  class lockfree_hashmap<Key, T>::iterator
  {
    public:
      iterator (cubthread::entry *thread_p, lockfree_hashmap &map);
      ~iterator ();

      T *iterate ();

    private:
      // old stuff
      lockfree_hashmap &m_map;
      lf_hash_table_cpp::iterator m_old_iter;
      typename lockfree_hashmap::new_hashmap::iterator m_new_iter;
      T *m_crt_val;
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
  template <class Key, class T>
  lockfree_hashmap<Key, T>::lockfree_hashmap ()
    : m_old_hash {}
    , m_new_hash {}
    , m_type (UNKNOWN)
  {
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count,
					 int freelist_block_size, lf_entry_descriptor &edesc, int entry_idx)
  {
    m_type = OLD;
    m_old_hash.init (transys, hash_size, freelist_block_count, freelist_block_size);
    m_entry_idx = entry_idx;
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init_as_new ()
  {
    // not implemented
    assert (false);
  }

#define lockfree_hashmap_forward_func(f_, tp_, ...) \
  is_old_type () ? \
  m_old_hash.(f_) (get_tran_entry (tp_), __VA_ARGS__) : \
  m_new_hash.(f_) (tp_.get_lf_tran_index (), __VA_ARGS__)
#define lockfree_hashmap_forward_func_noarg(f_) \
  is_old_type () ? \
  m_old_hash.(f_) (get_tran_entry (tp_)) : \
  m_new_hash.(f_) (tp_.get_lf_tran_index ())

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::destroy ()
  {
    lockfree_hashmap_forward_func_noarg (destroy);
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::find (cubthread::entry *thread_p, Key &key)
  {
    return lockfree_hashmap_forward_func (find, thread_p, key);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::find_or_insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (find_or_insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::insert_given (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (insert_given, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::erase (cubthread::entry *thread_p, Key &key)
  {
    return lockfree_hashmap_forward_func (erase, thread_p, key);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::erase_locked (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (erase_locked, thread_p, key, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::unlock (cubthread::entry *thread_p, T *&t)
  {
    lockfree_hashmap_forward_func (unlock, thread_p, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::clear (cubthread::entry *thread_p)
  {
    lockfree_hashmap_forward_func (clear, thread_p);
  }

#undef lockfree_hashmap_forward_func
#undef lockfree_hashmap_forward_func_noarg

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::is_old_type () const
  {
    // for now, always true
    return true;
  }

  template <class Key, class T>
  lf_tran_entry *
  lockfree_hashmap<Key, T>::get_tran_entry (cubthread::entry *thread_p)
  {
    return thread_get_tran_entry (thread_p, m_entry_idx);
  }

  //
  // lockfree_hashmap::iterator
  //
  template <class Key, class T>
  lockfree_hashmap<Key, T>::iterator::iterator (cubthread::entry *thread_p, lockfree_hashmap &map)
    : m_map (map)
    , m_old_iter ()
    , m_new_iter ()
    , m_crt_val (NULL)
  {
    if (m_map.is_old_type ())
      {
	m_old_iter = { m_map.get_tran_entry (thread_p), m_map.m_old_hash };
      }
    else
      {
	m_new_iter = { thread_p->get_lf_tran_index (), m_map.m_new_hash };
      }
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::iterator::iterate ()
  {
    if (m_map.is_old_type ())
      {
	return m_old_iter.iterate ();
      }
    else
      {
	return m_new_iter.iterate ();
      }
  }
}

#endif // !_THREAD_LOCKFREE_HASH_MAP_
