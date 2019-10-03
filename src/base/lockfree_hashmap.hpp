/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

//
// lock-free hash map structure
//

#ifndef _LOCKFREE_HASHMAP_HPP_
#define _LOCKFREE_HASHMAP_HPP_

#include "error_code.h"
#include "lock_free.h"                        // for lf_entry_descriptor
#include "lockfree_address_marker.hpp"
#include "lockfree_freelist.hpp"
#include "porting.h"

#include <cassert>
#include <limits>
#include <mutex>

namespace lockfree
{
  template <class Key, class T>
  class hashmap
  {
    public:
      class iterator;

      hashmap () = default;
      ~hashmap ();

      void init (tran::system &transys, size_t hash_size, size_t freelist_block_size, size_t freelist_block_count,
		 lf_entry_descriptor &edesc);
      void destroy ();

      T *find (tran::index tran_index, Key &key);
      bool find_or_insert (tran::index tran_index, Key &key, T *&entry);
      bool insert (tran::index tran_index, Key &key, T *&entry);
      bool insert_given (tran::index tran_index, Key &key, T *&entry);
      bool erase (tran::index tran_index, Key &key);
      bool erase_locked (tran::index tran_index, Key &key, T *&locked_entry);

      void unlock (tran::index tran_index, T *&entry);

      void clear (tran::index tran_index);    // NOT LOCK-FREE

    private:
      using address_type = address_marker<T>;
      using freelist_type = freelist<T>;
      using free_node_type = typename freelist_type::free_node;

      freelist_type *m_freelist;

      T **m_buckets;
      size_t m_size;

      T **m_backbuffer;
      std::mutex m_backbuffer_mutex;

      lf_entry_descriptor *m_edesc;

      void *volatile *get_ref (T *p, size_t o);
      void *get_ptr (T *p, size_t o);
      void *get_ptr_deref (T *p, size_t o);
      void *get_keyp (T *p);
      T *get_nextp (T *p);
      T *&get_nextp_ref (T *p);
      pthread_mutex_t *get_pthread_mutexp (T *p);

      free_node_type *to_free_node (T *p);
      T *from_free_node (free_node_type *fn);
      void save_temporary (tran::descriptor &tdes, T *&p);
      T *claim_temporary (tran::descriptor &tdes);
      void freelist_retire (tran::descriptor &tdes, T *&p);
      T *freelist_claim (tran::descriptor &tdes);

      void safeguard_use_mutex_or_tran_started (const tran::descriptor &tdes, const pthread_mutex_t *mtx);
      void start_tran_if_not_started (tran::descriptor &tdes);
      void start_tran_force (tran::descriptor &tdes);
      void promote_tran_force (tran::descriptor &tdes);
      void end_tran_if_started (tran::descriptor &tdes);
      void end_tran_force (tran::descriptor &tdes);
      void lock_entry_mutex (T &tolock, pthread_mutex_t *&mtx);
      void unlock_entry_mutex_if_locked (pthread_mutex_t *&mtx);
      void unlock_entry_mutex_force (pthread_mutex_t *&mtx);

      size_t get_hash (Key &key) const;
      T *&get_bucket (Key &key);
      tran::descriptor &get_tran_descriptor (tran::index tran_index);

      void list_find (tran::index tran_index, T *list_head, Key &key, int *behavior_flags, T *&found_node);
      bool list_insert_internal (tran::index tran_index, T *&list_head, Key &key, int *behavior_flags,
				 T *&found_node);
      bool list_delete (tran::index tran_index, T *&list_head, Key &key, T *locked_entry, int *behavior_flags);

      bool hash_insert_internal (tran::index tran_index, Key &key, int bflags, T *&entry);
      bool hash_erase_internal (tran::index tran_index, Key &key, int bflags, T *locked_entry);
  }; // class hashmap

  template <class Key, class T>
  class hashmap<Key, T>::iterator
  {
    public:
      iterator () = default;
      iterator (tran::index tran_index, hashmap &hash);

      T *iterate ();

    private:
      const size_t INVALID_INDEX = std::numeric_limits<size_t>::max ();

      hashmap *m_hashmap;
      tran::descriptor *m_tdes;
      size_t m_bucket_index;
      T *m_curr;
  };

} // namespace lockfree

//
// implementation
//

namespace lockfree
{
  template <class Key, class T>
  hashmap<Key, T>::~hashmap ()
  {
    destroy ();
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::destroy ()
  {
    T *node_iter;
    T *save_next;
    tran::descriptor &tdes = get_tran_descriptor (0);

    for (size_t i = 0; i < m_size; ++i)
      {
	for (node_iter = m_buckets[i]; node_iter != NULL; node_iter = save_next)
	  {
	    assert (!address_type::is_address_marked (node_iter));
	    save_next = get_nextp (node_iter);
	    freelist_retire (tdes, node_iter);
	  }
      }

    delete [] m_buckets;
    delete [] m_backbuffer;

    delete m_freelist;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::init (tran::system &transys, size_t hash_size, size_t freelist_block_size,
			 size_t freelist_block_count, lf_entry_descriptor &edesc)
  {
    m_freelist = new freelist_type (transys, freelist_block_size, freelist_block_count);

    m_edesc = &edesc;

    m_size = hash_size;
    m_buckets = new T *[m_size] ();

    m_backbuffer = new T *[m_size] ();
    for (size_t i = 0; i < m_size; i++)
      {
	m_backbuffer[i] = address_type::set_adress_mark (NULL);
      }
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::find (tran::index tran_index, Key &key)
  {
    int bflags = 0;
    bool restart = true;
    T *entry = NULL;

    while (restart)
      {
	T *&list_head = get_bucket (key);
	entry = NULL;
	bflags = LF_LIST_BF_RETURN_ON_RESTART;
	list_find (tran_index, list_head, &bflags, entry);
	restart = (bflags & LF_LIST_BR_RESTARTED) != 0;
      }
    return entry;
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::find_or_insert (tran::index tran_index, Key &key, T *&entry)
  {
    return hash_insert_internal (tran_index, key, LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_FIND_OR_INSERT, entry);
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::insert (tran::index tran_index, Key &key, T *&entry)
  {
    return hash_insert_internal (tran_index, key, LF_LIST_BF_RETURN_ON_RESTART, entry);
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::insert_given (tran::index tran_index, Key &key, T *&entry)
  {
    assert (entry != NULL);
    return hash_insert_internal (tran_index, key,
				 LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_INSERT_GIVEN | LF_LIST_BF_FIND_OR_INSERT,
				 entry);
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::erase (tran::index tran_index, Key &key)
  {
    return hash_erase_internal (tran_index, key, LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_LOCK_ON_DELETE, NULL);
  }

  /*
   * NOTE: Careful when calling this function. The typical scenario to call this function is to first find entry using
   *	   lf_hash_find and then call lf_hash_delete on the found entry.
   * NOTE: lf_hash_delete_already_locks can be called only if entry has mutexes.
   * NOTE: The delete will be successful only if the entry found by key matches the given entry.
   *	   Usually, the entry will match. However, we do have a limited scenario when a different entry with the same
   *	   key may be found:
   *	    1. Entry was found or inserted by this transaction.
   *	    2. Another transaction cleared the hash. All current entries are moved to back buffer and will be soon
   *           retired.
   *	    3. A third transaction inserts a new entry with the same key.
   *	    4. This transaction tries to delete the entry but the entry inserted by the third transaction si found.
   */
  template <class Key, class T>
  bool
  hashmap<Key, T>::erase_locked (tran::index tran_index, Key &key, T *&locked_entry)
  {
    assert (locked_entry != NULL);
    assert (m_edesc->using_mutex);

    bool success = hash_erase_internal (tran_index, key, LF_LIST_BF_RETURN_ON_RESTART, locked_entry);
    if (success)
      {
	locked_entry = NULL;
      }
    return success;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::unlock (tran::index tran_index, T *&entry)
  {
    assert (entry != NULL);
    if (m_edesc->using_mutex)
      {
	pthread_mutex_unlock (get_pthread_mutexp (entry));
      }
    else
      {
	get_tran_descriptor (tran_index).end_tran ();
      }
    entry = NULL;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::clear (tran::index tran_index)
  {
    tran::descriptor &tdes = get_tran_descriptor (tran_index);

    /* lock mutex */
    std::unique_lock<std::mutex> ulock (m_backbuffer_mutex);

    /* swap bucket pointer with current backbuffer */
    T **old_buckets = ATOMIC_TAS_ADDR (&m_buckets, m_backbuffer);

    /* clear bucket buffer, containing remains of old entries marked for delete */
    for (size_t i = 0; i < m_size; ++i)
      {
	assert (m_backbuffer[i] == address_type::set_adress_mark (NULL));
	m_buckets[i] = NULL;
      }

    /* register new backbuffer */
    m_backbuffer = m_buckets;

    /* retire all entries from old buckets; note that threads currently operating on the entries will not be disturbed
     * since the actual deletion is performed when the entries are no longer handled by active transactions */
    T *curr = NULL;
    T **next_p = NULL;
    T *next = NULL;
    pthread_mutex_t *mutex_p = NULL;
    for (size_t i = 0; i < m_size; ++i)
      {
	// remove list from bucket
	// warning: this may spin
	do
	  {
	    curr = address_type::strip_address_mark (old_buckets[i]);
	  }
	while (!ATOMIC_CAS_ADDR (&old_buckets[i], curr, address_type::set_adress_mark (NULL)));

	while (curr != NULL)
	  {
	    next_p = &get_nextp_ref (curr);

	    /* unlink from hash chain */
	    // warning: this may spin
	    do
	      {
		next = address_type::strip_address_mark (*next_p);
	      }
	    while (!ATOMIC_CAS_ADDR (next_p, next, address_type::set_adress_mark (next)));

	    /* wait for mutex */
	    if (m_edesc->using_mutex)
	      {
		mutex_p = get_pthread_mutexp (curr);

		pthread_mutex_lock (mutex_p);
		pthread_mutex_unlock (mutex_p);

		/* there should be only one mutex lock-unlock per entry per access via bucket array, so locking/unlocking
		 * once while the entry is inaccessible should be enough to guarantee nobody will be using it afterwards */
	      }

	    /* retire */
	    freelist_retire (tdes, curr);

	    /* advance */
	    curr = next;
	  }
      }
  }

  template <class Key, class T>
  size_t
  hashmap<Key, T>::get_hash (Key &key) const
  {
    return m_edesc->f_hash (&key, m_size);
  }

  template <class Key, class T>
  T *&
  hashmap<Key, T>::get_bucket (Key &key)
  {
    return m_buckets[get_hash (key)];
  }

  template <class Key, class T>
  tran::descriptor &
  hashmap<Key, T>::get_tran_descriptor (tran::index tran_index)
  {
    return m_freelist->get_transaction_table ().get_descriptor (tran_index);
  }

  template <class Key, class T>
  void *
  hashmap<Key, T>::get_ptr (T *p, size_t o)
  {
    assert (!address_type::is_address_marked (p));
    return (void *) (((char *) p) + o);
  }

  template <class Key, class T>
  void *volatile *
  hashmap<Key, T>::get_ref (T *p, size_t o)
  {
    return (void *volatile *) (((char *) p) + o);
  }

  template <class Key, class T>
  void *
  hashmap<Key, T>::get_ptr_deref (T *p, size_t o)
  {
    return *get_ref (p, o);
  }

  template <class Key, class T>
  void *
  hashmap<Key, T>::get_keyp (T *p)
  {
    return get_ptr (p, m_edesc->of_key);
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::get_nextp (T *p)
  {
    return (T *) get_ptr_deref (p, m_edesc->of_next);
  }

  template <class Key, class T>
  T *&
  hashmap<Key, T>::get_nextp_ref (T *p)
  {
    return *get_ref (p, m_edesc->of_next);
  }

  template <class Key, class T>
  pthread_mutex_t *
  hashmap<Key, T>::get_pthread_mutexp (T *p)
  {
    return (pthread_mutex_t *) get_ptr (p, m_edesc->of_mutex);
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::from_free_node (free_node_type *fn)
  {
    assert (fn != NULL);
    return &fn->get_data ();
  }

  template <class Key, class T>
  typename hashmap<Key, T>::freelist_type::free_node *
  hashmap<Key, T>::to_free_node (T *p)
  {
    // not nice, but necessary until we fully refactor lockfree hashmap
    char *cp = (char *) p;
    cp -= free_node_type::OFFSET_TO_DATA;
    return (free_node_type *) cp;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::save_temporary (tran::descriptor &tdes, T *&p)
  {
    free_node_type *fn = to_free_node (p);
    tdes.save_reclaimable (fn);
    p = NULL;
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::claim_temporary (tran::descriptor &tdes)
  {
    return from_free_node (reinterpret_cast<free_node_type *> (tdes.pull_saved_reclaimable ()));
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::freelist_retire (tran::descriptor &tdes, T *&p)
  {
    assert (p != NULL);
    m_freelist->retire (tdes, *to_free_node (p));
    p = NULL;
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::freelist_claim (tran::descriptor &tdes)
  {
    free_node_type *fn = reinterpret_cast<free_node_type *> (tdes.pull_saved_reclaimable ());
    bool is_local_tran = false;
    if (!tdes.is_tran_started ())
      {
	tdes.start_tran ();
	is_local_tran = true;
      }
    if (fn == NULL)
      {
	fn = m_freelist->claim (tdes);
	assert (fn != NULL);
      }
    T *claimed = from_free_node (fn);
    get_nextp_ref (claimed) = NULL;   // make sure link is removed

    if (is_local_tran)
      {
	tdes.end_tran ();
      }
    return claimed;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::safeguard_use_mutex_or_tran_started (const tran::descriptor &tdes, const pthread_mutex_t *mtx)
  {
    /* Assert used to make sure the current entry is protected by either transaction or mutex. */

    /* The transaction is started if and only if we don't use mutex */ \
    assert (tdes.is_tran_started () == !m_edesc->using_mutex);

    /* If we use mutex, we have a mutex locked. */
    assert (!m_edesc->using_mutex || mtx != NULL);
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::start_tran_if_not_started (tran::descriptor &tdes)
  {
    tdes.start_tran ();   // same result if it was started or not
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::start_tran_force (tran::descriptor &tdes)
  {
    assert (!tdes.is_tran_started ());
    tdes.start_tran ();
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::promote_tran_force (tran::descriptor &tdes)
  {
    assert (tdes.is_tran_started ());
    tdes.start_tran_and_increment_id ();
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::end_tran_if_started (tran::descriptor &tdes)
  {
    if (tdes.is_tran_started ())
      {
	tdes.end_tran ();
      }
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::end_tran_force (tran::descriptor &tdes)
  {
    assert (tdes.is_tran_started ());
    tdes.end_tran ();
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::lock_entry_mutex (T &tolock, pthread_mutex_t *&mtx)
  {
    assert (m_edesc->using_mutex);
    assert (mtx == NULL);

    mtx = get_pthread_mutexp (tolock);
    pthread_mutex_lock (mtx);
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::unlock_entry_mutex_if_locked (pthread_mutex_t *&mtx)
  {
    if (m_edesc->using_mutex && mtx != NULL)
      {
	pthread_mutex_unlock (mtx);
	mtx = NULL;
      }
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::unlock_entry_mutex_force (pthread_mutex_t *&mtx)
  {
    assert (m_edesc->using_mutex && mtx != NULL);
    pthread_mutex_unlock (mtx);
    mtx = NULL;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::list_find (tran::index tran_index, T *list_head, Key &key, int *behavior_flags, T *&entry)
  {
    tran::descriptor &tdes = get_tran_descriptor (tran_index);
    T *curr = NULL;
    pthread_mutex_t *entry_mutex = NULL;

    /* by default, not found */
    entry = NULL;

    tdes.start_tran ();
    bool restart_search = true;

    while (restart_search)    // restart_search:
      {
	curr = address_type::atomic_strip_address_mark (list_head);
	restart_search = false;

	while (curr != NULL)
	  {
	    if (m_edesc->f_key_cmp (&key, get_keyp (curr)) == 0)
	      {
		/* found! */
		if (m_edesc->using_mutex)
		  {
		    /* entry has a mutex protecting it's members; lock it */
		    lock_entry_mutex (curr, entry_mutex);

		    /* mutex has been locked, no need to keep transaction */
		    tdes.end_tran ();

		    if (address_type::is_address_marked (get_nextp_ref (curr)))
		      {
			/* while waiting for lock, somebody else deleted the entry; restart the search */
			pthread_mutex_unlock (entry_mutex);

			if (behavior_flags != NULL && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
			  {
			    *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
			    return;
			  }
			else
			  {
			    // restart
			    restart_search = true;
			    break;
			  }
		      }
		  } // m_edesc->using_mutex

		entry = curr;
		return;
	      }

	    /* advance */
	    curr = address_type::strip_address_mark (get_nextp_ref (curr));
	  }
      }

    /* all ok but not found */
    tdes.end_tran ();
  }


  /*
   * Behavior flags:
   *
   * LF_LIST_BF_RETURN_ON_RESTART - When insert fails because last entry in bucket was deleted, if this flag is set,
   *				    then the operation is restarted from here, instead of looping inside
   *				    lf_list_insert_internal (as a consequence, hash key is recalculated).
   *				    NOTE: Currently, this flag is always used (I must find out why).
   *
   * LF_LIST_BF_INSERT_GIVEN	  - If this flag is set, the caller means to force its own entry into hash table.
   *				    When the flag is not set, a new entry is claimed from freelist.
   *				    NOTE: If an entry with the same key already exists, the entry given as argument is
   *					  automatically retired.
   *
   * LF_LIST_BF_FIND_OR_INSERT	  - If this flag is set and an entry for the same key already exists, the existing
   *				    key will be output. If the flag is not set and key exists, insert just gives up
   *				    and a NULL entry is output.
   *				    NOTE: insert will not give up when key exists, if m_edesc->f_update is provided.
   *					  a new key is generated and insert is restarted.
   */
  template <class Key, class T>
  bool
  hashmap<Key, T>::list_insert_internal (tran::index tran_index, T *&list_head, Key &key, int *behavior_flags,
					 T *&entry)
  {
    pthread_mutex_t *entry_mutex = NULL;	/* Locked entry mutex when not NULL */
    T **curr_p = NULL;
    T *curr = NULL;
    tran::descriptor &tdes = get_tran_descriptor (tran_index);
    bool restart_search = true;

    while (restart_search)
      {
	restart_search = false;

	start_tran_force (tdes);

	curr_p = &list_head;
	curr = address_type::atomic_strip_address_mark (*curr_p);

	/* search */
	while (curr_p != NULL)    // this is always true actually...
	  {
	    assert (tdes.is_tran_started ());
	    assert (entry_mutex == NULL);

	    if (curr != NULL)
	      {
		if (m_edesc->f_key_cmp (&key, get_keyp (curr)) == 0)
		  {
		    /* found an entry with the same key. */

		    if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN) && entry != NULL)
		      {
			/* save this for further (local) use. */
			save_temporary (tdes, entry);
		      }

		    if (m_edesc->using_mutex)
		      {
			/* entry has a mutex protecting it's members; lock it */
			lock_entry_mutex (curr, entry_mutex);

			/* mutex has been locked, no need to keep transaction alive */
			end_tran_force (tdes);

			if (address_type::is_address_marked (get_nextp_ref (curr)))
			  {
			    /* while waiting for lock, somebody else deleted the entry; restart the search */
			    unlock_entry_mutex_force (entry_mutex);

			    if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
			      {
				*behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
				return false;
			      }
			    else
			      {
				restart_search = true;
				break;
			      }
			  }
		      }

		    safeguard_use_mutex_or_tran_started (tdes, entry_mutex);
		    if (m_edesc->f_duplicate != NULL)
		      {
			/* we have a duplicate key callback. */
			if (m_edesc->f_duplicate (&key, curr) != NO_ERROR)
			  {
			    end_tran_if_started (tdes);
			    unlock_entry_mutex_if_locked (entry_mutex);
			    return false;
			  }

#if 1
			LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
			end_tran_if_started (tdes);
			unlock_entry_mutex_if_locked (entry_mutex);
			return false;
#else  /* !1 = 0 */
			/* Could we have such cases that we just update existing entry without modifying anything else?
			 * And would it be usable with just a flag?
			 * Then this code may be used.
			 * So far we have only one usage for f_duplicate, which increment SESSION_ID and requires
			 * restarting hash search. This will be the usual approach if f_duplicate.
			 * If we just increment a counter in existing entry, we don't need to do anything else. This
			 * however most likely depends on f_duplicate implementation. Maybe it is more useful to give
			 * behavior_flags argument to f_duplicate to tell us if restart is or is not needed.
			 */
			if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_RESTART_ON_DUPLICATE))
			  {
			    LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
			    end_tran_if_started (tdes);
			    unlock_entry_mutex_if_locked (entry_mutex);
			    return false;
			  }
			else
			  {
			    /* duplicate does not require restarting search. */
			    if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
			      {
				/* Could not be inserted. Retire the entry. */
				freelist_retire (tdes, entry);
			      }

			    /* fall through to output current entry. */
			  }
#endif /* 0 */
		      }
		    else // m_edesc->f_duplicate == NULL
		      {
			if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
			  {
			    /* the given entry could not be inserted. retire it. */
			    freelist_retire (tdes, entry);
			  }

			if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_FIND_OR_INSERT))
			  {
			    /* found entry is not accepted */
			    end_tran_if_started (tdes);
			    unlock_entry_mutex_if_locked (entry_mutex);
			    return false;
			  }

			/* fall through to output current entry. */
		      } // m_edesc->f_duplicate == NULL

		    assert (entry == NULL);
		    safeguard_use_mutex_or_tran_started (tdes, entry_mutex);
		    entry = curr;
		    return false;
		  } // m_edesc->f_key_cmp (&key, get_keyp (curr)) == 0

		/* advance */
		curr_p = &get_nextp_ref (curr);
		curr = address_type::strip_address_mark (*curr_p);
	      }
	    else // curr == NULL
	      {
		/* end of bucket, we must insert */
		if (entry == NULL)
		  {
		    assert (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN));

		    entry = freelist_claim (tdes);
		    assert (entry != NULL);

		    /* set it's key */
		    if (m_edesc->f_key_copy (&key, get_keyp (entry)) != NO_ERROR)
		      {
			assert (false);
			end_tran_force (tdes);
			return false;
		      }
		  }

		if (m_edesc->using_mutex)
		  {
		    /* entry has a mutex protecting it's members; lock it */
		    lock_entry_mutex (entry, entry_mutex);
		  }

		/* attempt an add */
		if (!ATOMIC_CAS_ADDR (curr_p, NULL, entry))
		  {
		    if (m_edesc->using_mutex)
		      {
			/* link failed, unlock mutex */
			unlock_entry_mutex_force (entry_mutex);
		      }

		    /* someone added before us, restart process */
		    if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_RETURN_ON_RESTART))
		      {
			if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
			  {
			    save_temporary (entry);
			  }
			LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
			end_tran_force (tdes);
			return false;
		      }
		    else
		      {
			end_tran_force (tdes);
			restart_search = true;
			break;
		      }
		  }

		/* end transaction if mutex is acquired */
		if (m_edesc->using_mutex)
		  {
		    end_tran_force (tdes);
		  }

		/* done! */
		return true;
	      } //  else of if (curr != NULL)
	  } // while (curr_p != NULL)

	/* impossible case */
	assert (false);
      } // while (restart_search)
    /* impossible case */
    assert (false);
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::list_delete (tran::index tran_index, T *&list_head, Key &key, T *locked_entry, int *behavior_flags)
  {
    pthread_mutex_t *entry_mutex = NULL;
    tran::descriptor &tdes = get_tran_descriptor (tran_index);
    T **curr_p;
    T *curr;
    T **next_p;
    T *next;
    bool restart_search = true;

    while (restart_search)
      {
	start_tran_force (tdes);
	curr_p = &list_head;
	curr = address_type::atomic_strip_address_mark (*curr_p);

	/* search */
	while (curr != NULL)
	  {
	    /* is this the droid we are looking for? */
	    if (m_edesc->f_key_cmp (key, get_keyp (curr)) == 0)
	      {
		if (locked_entry != NULL && locked_entry != curr)
		  {
		    assert (m_edesc->using_mutex
			    && !LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE));

		    /* We are here because lf_hash_delete_already_locked was called. The entry found by matching key is
		     * different from the entry we were trying to delete.
		     * This is possible (please find the description of hash_delete_already_locked). */
		    end_tran_force (tdes);
		    return false;
		  }
	      }

	    /* fetch next entry */
	    next_p = &get_nextp_ref (curr);
	    next = address_type::strip_address_mark (*next_p);

	    /* set mark on next pointer; this way, if anyone else is trying to delete the next entry, it will fail */
	    if (!ATOMIC_CAS_ADDR (next_p, next, address_type::set_adress_mark (next)))
	      {
		/* joke's on us, this time; somebody else marked it before */

		end_tran_force (tdes);
		if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		  {
		    *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		    assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		    return false;
		  }
		else
		  {
		    restart_search = true;
		    break;
		  }
	      }

	    /* lock mutex if necessary */
	    if (m_edesc->using_mutex)
	      {
		if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE))
		  {
		    lock_entry_mutex (curr, entry_mutex);
		  }
		else
		  {
		    /* Must be already locked! */
		    entry_mutex = get_pthread_mutexp (curr);
		  }

		/* since we set the mark, nobody else can delete it, so we have nothing else to check */
	      }

	    /* unlink */
	    if (!ATOMIC_CAS_ADDR (curr_p, curr, next))
	      {
		/* unlink failed; first step is to remove lock (if applicable) */
		if (m_edesc->using_mutex && LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE))
		  {
		    unlock_entry_mutex_force (entry_mutex);
		  }

		/* remove mark and restart search */
		if (!ATOMIC_CAS_ADDR (next_p, address_type::set_adress_mark (next), next))
		  {
		    /* impossible case */
		    assert (false);
		    end_tran_force (tdes);
		    return ER_FAILED;
		  }

		end_tran_force (tdes);
		if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		  {
		    *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		    assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		    return NO_ERROR;
		  }
		else
		  {
		    restart_search = true;
		    break;
		  }
	      }
	    /* unlink successful */

	    /* unlock mutex */
	    if (m_edesc->using_mutex)
	      {
		unlock_entry_mutex_force (entry_mutex);
	      }

	    promote_tran_force (tdes);

	    /* now we can feed the entry to the freelist and forget about it */
	    freelist_retire (tdes, curr);

	    /* end the transaction */
	    end_tran_force (tdes);

	    /* success! */
	    return true;
	  } // while (curr != NULL)
      } // while (restart_search)
  }

  /*
   * Behavior flags:
   *
   * LF_LIST_BF_RETURN_ON_RESTART - When insert fails because last entry in bucket was deleted, if this flag is set,
   *				    then the operation is restarted from here, instead of looping inside
   *				    lf_list_insert_internal (as a consequence, hash key is recalculated).
   *				    NOTE: Currently, this flag is always used (I must find out why).
   *
   * LF_LIST_BF_INSERT_GIVEN	  - If this flag is set, the caller means to force its own entry into hash table.
   *				    When the flag is not set, a new entry is claimed from freelist.
   *				    NOTE: If an entry with the same key already exists, the entry given as argument is
   *					  automatically retired.
   *
   * LF_LIST_BF_FIND_OR_INSERT	  - If this flag is set and an entry for the same key already exists, the existing
   *				    key will be output. If the flag is not set and key exists, insert just gives up
   *				    and a NULL entry is output.
   *				    NOTE: insert will not give up when key exists, if edesc->f_update is provided.
   *					  a new key is generated and insert is restarted.
   */
  template <class Key, class T>
  bool
  hashmap<Key, T>::hash_insert_internal (tran::index tran_index, Key &key, int bflags, T *&entry)
  {
    bool inserted = false;

    while (true)
      {
	T *&list_head = get_bucket (key);
	if (LF_LIST_BF_IS_FLAG_SET (&bflags, LF_LIST_BF_INSERT_GIVEN))
	  {
	    assert (entry != NULL);
	  }
	else
	  {
	    entry = NULL;
	  }

	inserted = list_insert_internal (tran_index, list_head, key, &bflags, entry);
	if ((bflags & LF_LIST_BR_RESTARTED) != 0)
	  {
	    // restart
	    bflags &= ~LF_LIST_BR_RESTARTED;
	  }
	else
	  {
	    // done
	    break;
	  }
      }
    return inserted;
  }


  template <class Key, class T>
  bool
  hashmap<Key, T>::hash_erase_internal (tran::index tran_index, Key &key, int bflags, T *locked_entry)
  {
    bool erased = false;

    while (true)
      {
	T *&list_head = get_bucket (key);
	erased = list_delete (tran_index, list_head, key, locked_entry, &bflags);
	if ((bflags & LF_LIST_BR_RESTARTED) != 0)
	  {
	    // restart
	    bflags &= ~LF_LIST_BR_RESTARTED;
	  }
	else
	  {
	    // done
	    break;
	  }
      }
    return erased;
  }

  //
  // hashmap::iterator
  //
  template <class Key, class T>
  hashmap<Key, T>::iterator::iterator (tran::index tran_index, hashmap &hash)
    : m_hashmap (&hash)
    , m_tdes (&hash.get_tran_descriptor (tran_index))
    , m_bucket_index (INVALID_INDEX)
    , m_curr (NULL)
  {
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::iterator::iterate ()
  {
    if (m_hashmap == NULL && m_tdes == NULL)
      {
	assert (false);
	return NULL;
      }

    T **next_p = NULL;
    do
      {
	/* save current leader as trailer */
	if (m_curr != NULL)
	  {
	    if (m_hashmap->m_edesc->using_mutex)
	      {
		/* follow house rules: lock mutex */
		pthread_mutex_unlock (m_hashmap->get_pthread_mutexp (m_curr));
	      }

	    /* load next entry */
	    next_p = &m_hashmap->get_nextp_ref (m_curr);
	    m_curr = address_type::strip_address_mark (*next_p);
	  }
	else
	  {
	    /* reset transaction for each bucket */
	    if (m_bucket_index != INVALID_INDEX)
	      {
		m_tdes->end_tran ();
	      }
	    m_tdes->start_tran ();

	    /* load next bucket */
	    m_bucket_index++;

	    if (m_bucket_index < m_hashmap->m_size)
	      {
		m_curr = address_type::atomic_strip_address_mark (m_hashmap->m_buckets[m_bucket_index]);
	      }
	    else
	      {
		/* end */
		assert (m_bucket_index == m_hashmap->m_size);
		m_tdes->end_tran ();
		return NULL;
	      }
	  }

	if (m_curr != NULL)
	  {
	    if (m_hashmap->m_edesc->using_mutex)
	      {
		pthread_mutex_lock (m_hashmap->get_pthread_mutexp (m_curr));

		if (address_type::is_address_marked (m_hashmap->get_nextp_ref (m_curr)))
		  {
		    /* deleted in the meantime, skip it */
		    continue;
		  }
	      }
	  }
      }
    while (m_curr == NULL);

    /* we have a valid entry */
    return m_curr;
  }
} // namespace lockfree

#endif // !_LOCKFREE_HASHMAP_HPP_
