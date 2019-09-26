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
#include "thread_entry.hpp"

namespace cubthread
{
  template <class Key, class T>
  class lockfree_hashmap
  {
    public:
      lockfree_hashmap ();

      void init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
			lf_entry_descriptor &edesc, int entry_idx);
      void init_as_new ();
      void destroy ();

      T *find (cubthread::entry *thread_p, Key &key);
      bool find_or_insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert_given (cubthread::entry *thread_p, Key &key, T *&t);
      void unlock (cubthread::entry *thread_p, T *&t);

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

  template <class Key, class T>
  class lockfree_hashmap<Key, T>::old_hashmap
  {
    public:
      old_hashmap ();

      void init (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
		 lf_entry_descriptor &edes, int entry_idx);
      void destroy ();

      T *find (cubthread::entry *thread_p, Key &key);
      bool find_or_insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert_given (cubthread::entry *thread_p, Key &key, T *&t);
      void unlock (cubthread::entry *thread_p, T *&t);

    private:
      pthread_mutex_t *get_pthread_mutex (T *t);
      template <typename F>
      bool generic_insert (F &ins_func, cubthread::entry *thread_p, Key &key, T *&t);

      lf_freelist m_freelist;
      lf_hash_table m_hash;
      int m_entry_idx;
  };

  template <class Key, class T>
  class lockfree_hashmap<Key, T>::new_hashmap
  {
    public:
      new_hashmap () = default;

      void init () {}
      void destroy () {}

      T *
      find (cubthread::entry *thread_p, Key &key)
      {
	return NULL;
      }
      bool
      find_or_insert (cubthread::entry *thread_p, Key &key, T *&t)
      {
	return false;
      }
      bool
      insert (cubthread::entry *thread_p, Key &key, T *&t)
      {
	return false;
      }
      bool
      insert_given (cubthread::entry *thread_p, Key &key, T *&t)
      {
	return false;
      }

      void unlock (cubthread::entry *thread_p, T *&t) {}

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
    m_old_hash.init (transys, hash_size, freelist_block_count, freelist_block_size, entry_idx);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init_as_new ()
  {
    // not implemented
    assert (false);
  }

#define lockfree_hashmap_forward_func(f_, ...) \
  is_old_type () ? m_old_hash.(f_) (__VA_ARGS__) : else m_new_hash.(f_) (__VA_ARGS__)
#define lockfree_hashmap_forward_func_noarg(f_) \
  is_old_type () ? m_old_hash.(f_) () : else m_new_hash.(f_) ()

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
  void
  lockfree_hashmap<Key, T>::unlock (cubthread::entry *thread_p, T *&t)
  {
    lockfree_hashmap_forward_func (unlock, thread_p, t);
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

  //
  // lockfree_hashmap::old_hashmap
  //
  template <class Key, class T>
  lockfree_hashmap<Key, T>::old_hashmap::old_hashmap ()
    : m_freelist LF_FREELIST_INITIALIZER
    , m_hash LF_HASH_TABLE_INITIALIZER
    , m_entry_idx (-1)
  {
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::old_hashmap::init (lf_tran_system &transys, int hash_size, int freelist_block_count,
      int freelist_block_size, lf_entry_descriptor &edesc, int entry_idx)
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
    m_entry_idx = entry_idx;
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::old_hashmap::destroy ()
  {
    lf_hash_destroy (&m_hash);
    lf_freelist_destroy (&m_freelist);
  }

  template <class Key, class T>
  pthread_mutex_t *
  lockfree_hashmap<Key, T>::old_hashmap::get_pthread_mutex (T *t)
  {
    assert (m_freelist.entry_desc->using_mutex);
    return (pthread_mutex_t *) (((char *) t) + m_freelist.entry_desc->of_mutex);
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::old_hashmap::find (cubthread::entry *thread_p, Key &key)
  {
    lf_tran_entry *t_entry = thread_get_tran_entry (thread_p, m_entry_idx);
    T *ret = NULL;
    if (lf_hash_find (t_entry, &m_hash, &key, ret) != NO_ERROR)
      {
	assert (false);
      }
    return ret;
  }

  template <class Key, class T>
  template <typename F>
  bool
  lockfree_hashmap<Key, T>::old_hashmap::generic_insert (F &ins_func, cubthread::entry *thread_p, Key &key, T *&t)
  {
    lf_tran_entry *t_entry = thread_get_tran_entry (thread_p, m_entry_idx);
    int inserted = 0;
    if (ins_func (t_entry, &m_hash, &key, t, &inserted) != NO_ERROR)
      {
	assert (false);
      }
    return inserted != 0;
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::old_hashmap::find_or_insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return generic_insert (lf_hash_find_or_insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::old_hashmap::insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return generic_insert (lf_hash_insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::old_hashmap::insert_given (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return generic_insert (lf_hash_insert_given, thread_p, key, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::old_hashmap::unlock (cubthread::entry *thread_p, T *&t)
  {
    assert (t != NULL);
    lf_tran_entry *t_entry = thread_get_tran_entry (thread_p, m_entry_idx);
    if (m_freelist.entry_desc->using_mutex)
      {
	pthread_mutex_unlock (get_pthread_mutex (t));
      }
    else
      {
	lf_tran_end_with_mb (t_entry);
      }
    t = NULL;
  }
}

#endif // !_THREAD_LOCKFREE_HASH_MAP_
