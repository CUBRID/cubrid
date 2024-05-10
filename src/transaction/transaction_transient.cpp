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

#include "transaction_transient.hpp"

#include "es.h"
#include "heap_file.h"
#include "lob_locator.hpp"
#include "oid.h"
#include "memory_hash.h"
#include "memory_private_allocator.hpp"
#include "rb_tree.h"
#include "string_buffer.hpp"
#include "vacuum.h"

#include <cstring>  // for std::strcpy
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
//
// TODO: Create a "transaction transient" abstract interface and split this into multiple files
//

//
// Modified classes
//

tx_transient_class_entry::tx_transient_class_entry (const char *class_name, const OID &class_oid, const LOG_LSA &lsa)
  : m_class_oid (class_oid)
  , m_last_modified_lsa (lsa)
  , m_class_name (class_name)
{
}

const char *
tx_transient_class_entry::get_classname () const
{
  return m_class_name.c_str ();
}

void
tx_transient_class_registry::add (const char *classname, const OID &class_oid, const LOG_LSA &lsa)
{
  assert (classname != NULL);
  assert (!OID_ISNULL (&class_oid));
  assert (class_oid.volid >= 0);      // is not temp volume

  for (auto &it : m_list)
    {
      if (it.m_class_name == classname && OID_EQ (&it.m_class_oid, &class_oid))
	{
	  it.m_last_modified_lsa = lsa;
	  return;
	}
    }
  m_list.emplace_front (classname, class_oid, lsa);
}

bool
tx_transient_class_registry::has_class (const OID &class_oid) const
{
  for (auto &it : m_list)
    {
      if (OID_EQ (&class_oid, &it.m_class_oid))
	{
	  return true;
	}
    }
  return false;
}

char *
tx_transient_class_registry::to_string () const
{
  const size_t DEFAULT_STRBUF_SIZE = 128;
  string_buffer strbuf (cubmem::PRIVATE_BLOCK_ALLOCATOR, DEFAULT_STRBUF_SIZE);

  strbuf ("{");
  for (list_type::const_iterator it = m_list.cbegin (); it != m_list.cend (); it++)
    {
      if (it != m_list.cbegin ())
	{
	  strbuf (", ");
	}
      strbuf ("name=%s, oid=%d|%d|%d, lsa=%lld|%d", it->m_class_name.c_str (), OID_AS_ARGS (&it->m_class_oid),
	      LSA_AS_ARGS (&it->m_last_modified_lsa));
    }
  strbuf ("}");
  return strbuf.release_ptr ();
}

void
tx_transient_class_registry::map (const map_func_type &func) const
{
  bool stop = false;
  for (const auto &it : m_list)
    {
      func (it, stop);
      if (stop)
	{
	  return;
	}
    }
}

bool
tx_transient_class_registry::empty () const
{
  return m_list.empty ();
}

void
tx_transient_class_registry::decache_heap_repr (const LOG_LSA &downto_lsa)
{
  for (auto &it : m_list)
    {
      assert (!it.m_last_modified_lsa.is_null ());
      if (downto_lsa.is_null () || it.m_last_modified_lsa > downto_lsa)
	{
	  (void) heap_classrepr_decache (NULL, &it.m_class_oid);
	}
    }
}

void
tx_transient_class_registry::clear ()
{
  m_list.clear ();
}

//
// Lobs
//

struct lob_savepoint_entry
{
  LOB_LOCATOR_STATE state;
  LOG_LSA savept_lsa;
  lob_savepoint_entry *prev;
  ES_URI locator;
};

struct lob_locator_entry
{
  /* RB_ENTRY defines red-black tree node header for this structure. see base/rb_tree.h for more information. */
  RB_ENTRY (lob_locator_entry) head;
  lob_savepoint_entry *top;
  /* key_hash is used to reduce the number of strcmp. see the comment of lob_locator_cmp for more information */
  int key_hash;
  std::string m_key;
};

static void lob_locator_put_meta (const char *locator, std::string &meta_name);  // copy meta from locator
static void lob_locator_free (lob_locator_entry *&entry);
static int lob_locator_cmp (const lob_locator_entry *e1, const lob_locator_entry *e2);
/* RB_PROTOTYPE_STATIC declares red-black tree functions. see base/rb_tree.h */
RB_PROTOTYPE_STATIC (lob_rb_root, lob_locator_entry, head, lob_locator_cmp);

int
xtx_add_lob_locator (cubthread::entry *thread_p, const char *locator, LOB_LOCATOR_STATE state)
{
  int tran_index;
  LOG_TDES *tdes = NULL;
  lob_locator_entry *entry = NULL;
  lob_savepoint_entry *savept = NULL;

  assert (lob_locator_is_valid (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  entry = new lob_locator_entry ();
  savept = new lob_savepoint_entry ();

  entry->top = savept;
  entry->m_key = lob_locator_key (locator);
  entry->key_hash = (int) mht_5strhash (entry->m_key.c_str (), INT_MAX);

  savept->state = state;
  savept->savept_lsa = LSA_LT (&tdes->savept_lsa, &tdes->topop_lsa) ? tdes->topop_lsa : tdes->savept_lsa;
  savept->prev = NULL;
  strlcpy (savept->locator, locator, sizeof (ES_URI));

  /* Insert entry to the red-black tree (see base/rb_tree.h) */
  RB_INSERT (lob_rb_root, &tdes->lob_locator_root, entry);

  return NO_ERROR;
}

LOB_LOCATOR_STATE
xtx_find_lob_locator (cubthread::entry *thread_p, const char *locator, char *real_locator)
{
  int tran_index;
  LOG_TDES *tdes;

  assert (lob_locator_is_valid (locator));
  assert (real_locator != NULL);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);

  if (tdes != NULL)
    {
      lob_locator_entry *entry, find;

      find.m_key = lob_locator_key (locator);
      find.key_hash = (int) mht_5strhash (find.m_key.c_str (), INT_MAX);
      /* Find entry from red-black tree (see base/rb_tree.h) */
      entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);
      if (entry != NULL)
	{
	  std::strcpy (real_locator, entry->top->locator);
	  return entry->top->state;
	}
    }
  else
    {
      // is this acceptable?
    }

  std::strcpy (real_locator, locator);
  return LOB_NOT_FOUND;
}

int
xtx_change_state_of_locator (cubthread::entry *thread_p, const char *locator, const char *new_locator,
			     LOB_LOCATOR_STATE state)
{
  int tran_index;
  LOG_TDES *tdes;
  lob_locator_entry *entry, find;

  assert (lob_locator_is_valid (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  find.m_key = lob_locator_key (locator);
  find.key_hash = (int) mht_5strhash (find.m_key.c_str (), INT_MAX);
  entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);

  if (entry != NULL)
    {
      LOG_LSA last_lsa;

      last_lsa = LSA_GE (&tdes->savept_lsa, &tdes->topop_lsa) ? tdes->savept_lsa : tdes->topop_lsa;

      /* if it is created prior to current savepoint, push the previous state in the savepoint list */
      if (LSA_LT (&entry->top->savept_lsa, &last_lsa))
	{
	  lob_savepoint_entry *savept = new lob_savepoint_entry ();

	  /* copy structure (avoid redundant memory copy) */
	  savept->state = entry->top->state;
	  savept->savept_lsa = entry->top->savept_lsa;
	  std::strcpy (savept->locator, entry->top->locator);
	  savept->prev = entry->top;
	  entry->top = savept;
	}

      /* set the current state */
      if (new_locator != NULL)
	{
	  strlcpy (entry->top->locator, new_locator, sizeof (ES_URI));
	}
      entry->top->state = state;
      entry->top->savept_lsa = last_lsa;
    }

  return NO_ERROR;
}

/*
 * xlog_drop_lob_locator -
 *
 * return: error code
 *
 *   thread_p(in):
 *   locator(in):
 *
 * NOTE:
 */
int
xtx_drop_lob_locator (cubthread::entry *thread_p, const char *locator)
{
  int tran_index;
  LOG_TDES *tdes;
  lob_locator_entry *entry, find;

  assert (lob_locator_is_valid (locator));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  find.m_key = lob_locator_key (locator);
  find.key_hash = (int) mht_5strhash (find.m_key.c_str (), INT_MAX);
  /* Remove entry that matches 'find' entry from the red-black tree. see base/rb_tree.h for more information */
  entry = RB_FIND (lob_rb_root, &tdes->lob_locator_root, &find);
  if (entry != NULL)
    {
      entry = RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
      if (entry != NULL)
	{
	  lob_locator_free (entry);
	}
    }

  return NO_ERROR;
}

void
lob_locator_put_meta (const char *locator, std::string &meta_name)
{
  const char *keyp = lob_locator_key (locator);
  const char *metap = lob_locator_meta (locator);

  if (keyp - 1 <= metap)
    {
      assert (false);
      return;
    }
  size_t diff = (size_t) (keyp - metap - 1);

  meta_name.append (metap, diff);
  meta_name[diff] = '\0';
}

static void
lob_locator_free (lob_locator_entry *&entry)
{
  while (entry->top != NULL)
    {
      lob_savepoint_entry *savept;

      savept = entry->top;
      entry->top = savept->prev;
      delete savept;
    }

  delete entry;
  entry = NULL;
}

void
tx_lob_locator_clear (cubthread::entry *thread_p, log_tdes *tdes, bool at_commit, LOG_LSA *savept_lsa)
{
  lob_locator_entry *entry, *next;
  bool need_to_delete;

  if (tdes == NULL)
    {
      return;
    }

  for (entry = RB_MIN (lob_rb_root, &tdes->lob_locator_root); entry != NULL; entry = next)
    {
      /* setup next link before destroy */
      next = RB_NEXT (lob_rb_root, &tdes->lob_locator_root, entry);

      need_to_delete = false;

      if (at_commit)
	{
	  if (entry->top->state != LOB_PERMANENT_CREATED)
	    {
	      need_to_delete = true;
	    }
	}
      else			/* rollback */
	{
	  /* at partial rollback, pop the previous states in the savepoint list util it meets the rollback savepoint */
	  if (savept_lsa != NULL)
	    {
	      lob_savepoint_entry *savept, *tmp;

	      assert (entry->top != NULL);
	      savept = entry->top->prev;

	      while (savept != NULL)
		{
		  if (LSA_LT (&entry->top->savept_lsa, savept_lsa))
		    {
		      break;
		    }

		  /* rollback file renaming */
		  if (strcmp (entry->top->locator, savept->locator) != 0)
		    {
		      std::string meta_name;

		      assert (lob_locator_is_valid (savept->locator));
		      lob_locator_put_meta (savept->locator, meta_name);
		      /* ignore return value */
		      (void) es_rename_file (entry->top->locator, meta_name.c_str (), savept->locator);
		    }
		  tmp = entry->top;
		  entry->top = savept;
		  savept = savept->prev;
		  delete tmp;
		}
	    }

	  /* delete the locator to be created */
	  if ((savept_lsa == NULL || LSA_GE (&entry->top->savept_lsa, savept_lsa))
	      && entry->top->state != LOB_TRANSIENT_DELETED)
	    {
	      need_to_delete = true;
	    }
	}

      /* remove from the locator tree */
      if (need_to_delete)
	{
#if defined (SERVER_MODE)
	  if (at_commit && entry->top->state == LOB_PERMANENT_DELETED)
	    {
	      vacuum_notify_es_deleted (thread_p, entry->top->locator);
	    }
	  else
	    {
	      /* The file is created and rolled-back and it is not visible to anyone. Delete it directly without
	       * notifying vacuum. */
	      (void) es_delete_file (entry->top->locator);
	    }
#else /* !SERVER_MODE */
	  /* SA_MODE */
	  (void) es_delete_file (entry->top->locator);
#endif /* !SERVER_MODE */
	  RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
	  lob_locator_free (entry);
	}
    }

  /* at the end of transaction, free the locator list */
  if (savept_lsa == NULL)
    {
      for (entry = RB_MIN (lob_rb_root, &tdes->lob_locator_root); entry != NULL; entry = next)
	{
	  next = RB_NEXT (lob_rb_root, &tdes->lob_locator_root, entry);
	  RB_REMOVE (lob_rb_root, &tdes->lob_locator_root, entry);
	  lob_locator_free (entry);
	}
      assert (RB_EMPTY (&tdes->lob_locator_root));
    }
}

static int
lob_locator_cmp (const lob_locator_entry *e1, const lob_locator_entry *e2)
{
  if (e1->key_hash != e2->key_hash)
    {
      return e1->key_hash - e2->key_hash;
    }
  else
    {
      return e1->m_key.compare (e2->m_key);
    }
}

/*
 * Below macro generates lob rb tree functions (see base/rb_tree.h)
 * Note: semi-colon is intentionally added to be more beautiful
 */
RB_GENERATE_STATIC (lob_rb_root, lob_locator_entry, head, lob_locator_cmp);

void
lob_rb_root::init ()
{
  RB_INIT (this);
}
