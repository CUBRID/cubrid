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

#include "lock_free.h"                        // for lf_entry_descriptor
#include "lockfree_address_marker.hpp"
#include "lockfree_freelist.hpp"

#include <mutex>

namespace lockfree
{
  template <class Key, class T>
  class hashmap
  {
    public:
      hashmap () = default;

    private:
      using address_type = address_marker<T>;
      using freelist_type = freelist<T>;

      address_type *m_buckets;
      size_t m_size;

      freelist_type *m_freelist;

      address_type *m_backbuffer;
      std::mutex m_backbuffer_mutex;

      lf_entry_descriptor *m_edesc;

      void *get_ptr (T *p, size_t o);
      void *get_keyp (T *p);
      T *get_nextp (T *p);
      pthread_mutex_t *get_pthread_mutexp (T *p);

      freelist_type::free_node *to_free_node (T *p);
      T *from_free_node (freelist_type::free_node *fn);
      void save_temporary (tran::descriptor &tdes, T *&p);
      T *claim_temporary (tran::descriptor &tdes);

      void list_find (tran::index tran_index, address_marker &list_head, Key &key, int *behavior_flags, T *&found_node);
      bool list_insert_internal (tran::index tran_index, address_marker &list_head, Key &key, int *behavior_flags,
				 T *&found_node);
  };

}

//
// implementation
//

namespace lockfree
{
  template <class Key, class T>
  void *
  hashmap<Key, T>::get_ptr (T *p, size_t o)
  {
    assert (!address_type::is_address_marked (p));
    return (void *) (((char *) p) + o);
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
    return (T *) get_ptr (p, m_edesc->of_next);
  }

  template <class Key, class T>
  pthread_mutex_t *
  hashmap<Key, T>::get_pthread_mutexp (T *p)
  {
    return (pthread_mutex_t *) get_ptr (p, m_edesc->of_mutex);
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::from_free_node (freelist_type::free_node *fn)
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
    cp -= freelist_type::free_node::OFFSET_TO_DATA;
    return (freelist_type::free_node *) cp;
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::save_temporary (tran::descriptor &tdes, T *&p)
  {
    freelist_type::free_node *fn = to_free_node (p);
    tdes.save_reclaimable (fn);
    p = NULL;
  }

  template <class Key, class T>
  T *
  hashmap<Key, T>::claim_temporary (tran::descriptor &tdes)
  {
    return from_free_node (reinterpret_cast<freelist_type::free_node *> (tdes.pull_saved_reclaimable ()));
  }

  template <class Key, class T>
  void
  hashmap<Key, T>::list_find (tran::index tran_index, address_marker &list_head, Key &key, int *behavior_flags,
			      T *&found_node)
  {
    tran::descriptor &tdes = m_freelist->get_transaction_table ().get_descriptor (tran_index);
    T *curr = NULL;
    pthread_mutex_t *entry_mutex;

    tdes.start_tran ();
    bool restart_search = true;

    while (restart_search)    // restart_search:
      {
	curr = list_head.get_address ();
	restart_search = false;

	while (curr != NULL)
	  {
	    if (m_edesc->f_key_cmp (&key, get_keyp (curr)) == 0)
	      {
		/* found! */
		if (m_edesc->using_mutex)
		  {
		    /* entry has a mutex protecting it's members; lock it */
		    entry_mutex = get_pthread_mutexp (curr);
		    pthread_mutex_lock (entry_mutex);

		    /* mutex has been locked, no need to keep transaction */
		    tdes.end_tran ();

		    if (address_type::is_address_marked (get_nextp (curr)))
		      {
			/* while waiting for lock, somebody else deleted the entry; restart the search */
			pthread_mutex_unlock (entry_mutex);

			if (behavior_flags & (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
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
		  }
		else
		  {
		    // not using mutex
		    // fall through
		  }

		found_node = curr;
		return;
	      }

	    /* advance */
	    curr = address_marker::atomic_strip_address_mark (get_nextp (curr));
	  }
      }

    /* all ok but not found */
    tdes.end_tran ();
  }

  template <class Key, class T>
  bool
  hashmap<Key, T>::list_insert_internal (tran::index tran_index, address_marker &list_head, Key &key,
					 int *behavior_flags, T *&entry)
  {
    pthread_mutex_t *entry_mutex = NULL;	/* Locked entry mutex when not NULL */
    T *curr_no_strip = NULL;
    T *curr = NULL;
    bool is_tran_started = false;
    tran::descriptor &tdes = m_freelist->get_transaction_table ().get_descriptor (tran_index);
    bool restart_search = true;

    while (restart_search)
      {
	tdes.start_tran ();


	curr_no_strip = list_head.get_address_no_strip ();
	curr = address_marker::strip_address_mark (curr_no_strip);

	/* search */
	while (curr_no_strip != NULL)
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
			entry_mutex = get_pthread_mutexp (curr);
			pthread_mutex_lock (entry_mutex);

			tdes.end_tran ();

			/// todo...
		      }
		  }
	      }
	  }
      }
  }
}

#endif // !_LOCKFREE_HASHMAP_HPP_
