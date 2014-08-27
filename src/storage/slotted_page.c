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
 * slotted_page.c - slotted page management module (at the server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "slotted_page.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "memory_hash.h"
#include "page_buffer.h"
#include "log_manager.h"
#include "critical_section.h"
#include "mvcc.h"
#if defined(SERVER_MODE)
#include "thread.h"
#include "connection_error.h"
#endif /* SERVER_MODE */

#define SPAGE_SEARCH_NEXT       1
#define SPAGE_SEARCH_PREV       -1

#define SPAGE_VERIFY_HEADER(sphdr) 				\
  do {								\
    assert ((sphdr) != NULL);					\
    assert ((sphdr)->total_free >= 0);				\
    assert ((sphdr)->cont_free >= 0);				\
    assert ((sphdr)->cont_free <= (sphdr)->total_free);		\
    assert ((sphdr)->offset_to_free_area < DB_PAGESIZE);	\
    assert ((sphdr)->num_records >= 0);				\
    assert ((sphdr)->num_slots >= 0);				\
    assert ((sphdr)->num_records <= (sphdr)->num_slots);	\
  } while (0)

enum
{
  SPAGE_EMPTY_OFFSET = 0	/* uninitialized offset */
};

typedef struct spage_save_entry SPAGE_SAVE_ENTRY;
typedef struct spage_save_head SPAGE_SAVE_HEAD;

struct spage_save_entry
{				/* A hash entry to save space for future undos */
  TRANID tranid;		/* Transaction identifier */
  int saved;			/* Amount of space saved  */
  SPAGE_SAVE_ENTRY *next;	/* Next save */
  SPAGE_SAVE_ENTRY *prev;	/* Previous save */
  SPAGE_SAVE_ENTRY *tran_next_save;	/* Next save stored by the same transaction */
  SPAGE_SAVE_HEAD *head;	/* Head of the save list */
};

struct spage_save_head
{				/* Head of a saving space */
  VPID vpid;			/* Page and volume where the space is saved */
  int total_saved;		/* Total saved space by all transactions */
  SPAGE_SAVE_ENTRY *first;	/* First saving space entry */
};

/* context for slotted page header scan */
typedef struct spage_header_context SPAGE_HEADER_CONTEXT;
struct spage_header_context
{
  VPID vpid;
  SPAGE_HEADER header;
};

/* context for slotted page slots scan */
typedef struct spage_slots_context SPAGE_SLOTS_CONTEXT;
struct spage_slots_context
{
  VPID vpid;			/* vpid of specified page */
  PAGE_PTR pgptr;		/* The page load by pgbuf_fix */
  SPAGE_SLOT *slot;		/* Iterator about slot in the page */
};

static MHT_TABLE *spage_Mht_saving;	/* Memory hash table for savings */

static int spage_save_space (THREAD_ENTRY * thread_p, SPAGE_HEADER * sphdr,
			     PAGE_PTR pgptr, int save);
static int spage_get_saved_spaces (THREAD_ENTRY * thread_p,
				   SPAGE_HEADER * page_header_p,
				   PAGE_PTR page_p, int *other_saved_spaces);
static int spage_get_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
						  SPAGE_HEADER * sphdr,
						  PAGE_PTR pgptr);
static int spage_get_total_saved_spaces (THREAD_ENTRY * thread_p,
					 SPAGE_HEADER * page_header_p,
					 PAGE_PTR page_p);
static void spage_dump_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
						    FILE * fp, VPID * vpid);
static int spage_compare_slot_offset (const void *arg1, const void *arg2);
static bool spage_is_slotted_page_type (PAGE_TYPE ptype);

static int spage_check_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			      SPAGE_HEADER * page_header_p, int space);
static void spage_set_slot (SPAGE_SLOT * slot_p, int offset, int length,
			    INT16 type);
static int spage_find_empty_slot (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				  int length, INT16 type, SPAGE_SLOT ** sptr,
				  int *space, PGSLOTID * slotid);
static int spage_find_empty_slot_at (THREAD_ENTRY * thread_p,
				     PAGE_PTR pgptr, PGSLOTID slotid,
				     int length, INT16 type,
				     SPAGE_SLOT ** sptr);
static void spage_shift_slot_up (PAGE_PTR page_p,
				 SPAGE_HEADER * page_header_p,
				 SPAGE_SLOT * slot_p);
static void spage_shift_slot_down (PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   SPAGE_SLOT * slot_p);
static int spage_add_new_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			       SPAGE_HEADER * page_header_p,
			       int *out_space_p);
static int spage_take_slot_in_use (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   PGSLOTID slot_id, SPAGE_SLOT * slot_p,
				   int *out_space_p);

static int spage_check_record_for_insert (RECDES * record_descriptor_p);
static int spage_insert_data (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			      RECDES * recdes, void *slotptr);
static bool spage_is_record_located_at_end (SPAGE_HEADER * page_header_p,
					    SPAGE_SLOT * slot_p);
static void spage_reduce_a_slot (PAGE_PTR page_p);

static int spage_check_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				  PGSLOTID slot_id,
				  int record_descriptor_length,
				  SPAGE_SLOT ** out_slot_p, int *out_space_p,
				  int *out_old_waste_p, int *out_new_waste_p);
static int spage_check_mvcc_updatable (THREAD_ENTRY * thread_p,
				       PAGE_PTR page_p, PGSLOTID slot_id,
				       int mvcc_delete_record_length,
				       int mvcc_insert_record_length);
static int spage_update_record_in_place (PAGE_PTR page_p,
					 SPAGE_HEADER * page_header_p,
					 SPAGE_SLOT * slot_p,
					 const RECDES * record_descriptor_p,
					 int space);
static int spage_update_record_after_compact (PAGE_PTR page_p,
					      SPAGE_HEADER * page_header_p,
					      SPAGE_SLOT * slot_p,
					      const RECDES *
					      record_descriptor_p, int space,
					      int old_waste, int new_waste);

static SCAN_CODE spage_search_record (PAGE_PTR page_p,
				      PGSLOTID * out_slot_id_p,
				      RECDES * record_descriptor_p,
				      int is_peeking, int direction,
				      bool skip_empty);

static const char *spage_record_type_string (INT16 record_type);

static void spage_dump_header (FILE * fp, const SPAGE_HEADER * sphdr);
static void spage_dump_header_to_string (char *buffer, int size,
					 const SPAGE_HEADER * page_header_p);
static void spage_dump_slots (FILE * fp, const SPAGE_SLOT * sptr,
			      PGNSLOTS nslots, unsigned short alignment);
static void spage_dump_record (FILE * Fp, PAGE_PTR page_p, PGSLOTID slot_id,
			       SPAGE_SLOT * slot_p);

static bool spage_is_unknown_slot (PGSLOTID slotid, SPAGE_HEADER * sphdr,
				   SPAGE_SLOT * sptr);
static SPAGE_SLOT *spage_find_slot (PAGE_PTR pgptr, SPAGE_HEADER * sphdr,
				    PGSLOTID slotid,
				    bool is_unknown_slot_check);
static SCAN_CODE spage_get_record_data (PAGE_PTR pgptr, SPAGE_SLOT * sptr,
					RECDES * recdes, int ispeeking);
static SCAN_CODE spage_get_record_mvcc_data (PAGE_PTR page_p,
					     SPAGE_SLOT * slot_p,
					     RECDES * record_descriptor_p,
					     int is_peeking);
static bool spage_has_enough_total_space (THREAD_ENTRY * thread_p,
					  PAGE_PTR pgptr,
					  SPAGE_HEADER * sphdr, int space);
static bool spage_has_enough_contiguous_space (PAGE_PTR pgptr,
					       SPAGE_HEADER * sphdr,
					       int space);
static int spage_put_helper (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			     PGSLOTID slotid, int offset,
			     const RECDES * recdes, bool is_append);
static void spage_add_contiguous_free_space (PAGE_PTR pgptr, int space);
static void spage_reduce_contiguous_free_space (PAGE_PTR pgptr, int space);
static void spage_verify_header (PAGE_PTR page_p);

/*
 * spage_verify_header () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 */
static void
spage_verify_header (PAGE_PTR page_p)
{
  char header_info[1024];
  SPAGE_HEADER *sphdr;
  VPID vpid;

  sphdr = (SPAGE_HEADER *) page_p;
  if (sphdr == NULL
      || sphdr->total_free < 0
      || sphdr->cont_free < 0
      || sphdr->cont_free > sphdr->total_free
      || sphdr->offset_to_free_area >= DB_PAGESIZE
      || sphdr->num_records < 0
      || sphdr->num_slots < 0 || sphdr->num_records > sphdr->num_slots)
    {
      spage_dump_header_to_string (header_info, sizeof (header_info), sphdr);

      pgbuf_get_vpid (page_p, &vpid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
	      3, vpid.pageid, fileio_get_volume_label (vpid.volid, PEEK),
	      header_info);
      assert (false);
    }
}

/*
 * spage_is_valid_anchor_type ()
 *   return: whether the given number represents a valid anchor type constant
 *           for a slotted page
 *
 *   anchor_type(in): the anchor type constant
 */
bool
spage_is_valid_anchor_type (const INT16 anchor_type)
{
  assert (ANCHORED <= anchor_type && anchor_type <= UNANCHORED_KEEP_SEQUENCE);

  return (anchor_type == ANCHORED
	  || anchor_type == ANCHORED_DONT_REUSE_SLOTS
	  || anchor_type == UNANCHORED_ANY_SEQUENCE
	  || anchor_type == UNANCHORED_KEEP_SEQUENCE);
}

/*
 * spage_free_saved_spaces () - Release the savings of transaction
 *   return: void
 *
 *   first_save_entry(in): first save entry to be released
 *
 * Note: This function could be called once a transaction has finished.
 *       This is optional, it does not need to be done.
 */
void
spage_free_saved_spaces (THREAD_ENTRY * thread_p, void *first_save_entry)
{
  SPAGE_SAVE_ENTRY *entry, *current;
  SPAGE_SAVE_HEAD *head;

  assert (first_save_entry != NULL);

  entry = (SPAGE_SAVE_ENTRY *) first_save_entry;

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      assert (false);
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);

      return;
    }

  while (entry != NULL)
    {
      current = entry;
      head = entry->head;
      entry = entry->tran_next_save;

      assert (current->tranid == logtb_find_current_tranid (thread_p));

      /* Delete the current node from save entry list */
      if (current->prev == NULL)
	{
	  head->first = current->next;

	  /* There is no more entry. Delete save head and go to next entry */
	  if (head->first == NULL)
	    {
	      (void) mht_rem (spage_Mht_saving, &(head->vpid), NULL, NULL);
	      free_and_init (head);
	      free_and_init (current);
	      continue;
	    }
	}
      else
	{
	  current->prev->next = current->next;
	}

      if (current->next != NULL)
	{
	  current->next->prev = current->prev;
	}

      /* If there is saved space, decrease total saved space held by head */
      if (current->saved > 0)
	{
	  head->total_saved -= current->saved;

	  /* Total saved space should be 0 or positive */
	  if (head->total_saved < 0)
	    {
	      /* defense code */
	      head->total_saved = 0;
	    }
	}
      free_and_init (current);
    }
  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
}

/*
 * spage_free_all_saved_spaces_helper () - Release all savings
 *   return: NO_ERROR
 *
 *   vpid_key(in):  Volume and page identifier
 *   ent(in): Head entry information
 *   tid(in): Transaction identifier or NULL_TRANID
 */
static int
spage_free_all_saved_spaces_helper (const void *vpid_key, void *ent,
				    void *tid)
{
  SPAGE_SAVE_ENTRY *entry, *current;

  entry = ((SPAGE_SAVE_HEAD *) ent)->first;

  while (entry != NULL)
    {
      current = entry;
      entry = entry->next;

      free_and_init (current);
    }

  /* Release the head of savings */
  (void) mht_rem (spage_Mht_saving, vpid_key, NULL, NULL);
  free_and_init (ent);

  return NO_ERROR;
}

/*
 * spage_save_space () - Save some space for recovery (undo) purposes
 *   return:
 *
 *   page_header_p(in): Pointer to header of slotted page
 *   page_p(in): Pointer to slotted page
 *   space(in): Amount of space to save
 *
 * Note: The current transaction saving information is kept on the page only if
 *       the page is not held by the other transaction. When a transaction
 *       has occupied the page, the following transactions which try to save
 *       some space to the page should be make a hash entry.
 *
 *       The head of the save entries holds the total saved space of its list
 *       except the saved space stored by the page header.
 *
 */
static int
spage_save_space (THREAD_ENTRY * thread_p, SPAGE_HEADER * page_header_p,
		  PAGE_PTR page_p, int space)
{
  SPAGE_SAVE_HEAD *head_p;
  SPAGE_SAVE_ENTRY *entry_p;
  VPID *vpid_p;
  TRANID tranid;
  LOG_TDES *tdes;
  bool found;

  assert (page_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  if (space == 0 || log_is_in_crash_recovery ())
    {
      return NO_ERROR;
    }

  tranid = logtb_find_current_tranid (thread_p);

  /*
   * increase saved space when the transaction is active.
   */
  if (space < 0 || !logtb_is_active (thread_p, tranid))
    {
      return NO_ERROR;
    }

  vpid_p = pgbuf_get_vpid_ptr (page_p);
  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  head_p = (SPAGE_SAVE_HEAD *) mht_get (spage_Mht_saving, vpid_p);
  if (head_p == NULL)
    {
      head_p = (SPAGE_SAVE_HEAD *) malloc (sizeof (*head_p));
      if (head_p == NULL)
	{
	  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
	  return ER_FAILED;
	}

      entry_p = (SPAGE_SAVE_ENTRY *) malloc (sizeof (*entry_p));
      if (entry_p == NULL)
	{
	  free_and_init (head_p);
	  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
	  return ER_FAILED;
	}

      /*
       * Form the head and the first entry with information of the page
       * header, modify the header with current transaction saving, and
       * add first entry into hash
       */

      head_p->vpid.volid = vpid_p->volid;
      head_p->vpid.pageid = vpid_p->pageid;
      head_p->total_saved = space;
      head_p->first = entry_p;

      entry_p->tranid = tranid;
      entry_p->saved = space;
      entry_p->next = NULL;
      entry_p->prev = NULL;
      entry_p->head = head_p;

      /*
       * Add this entry to the save entry list of this transaction.
       * It will be used to release the save entries when the transaction
       * is completed.
       */
      tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      if (tdes != NULL)
	{
	  entry_p->tran_next_save = ((SPAGE_SAVE_ENTRY *)
				     (tdes->first_save_entry));
	  tdes->first_save_entry = (void *) entry_p;
	}
      else
	{
	  entry_p->tran_next_save = NULL;
	}

      (void) mht_put (spage_Mht_saving, &(head_p->vpid), head_p);

      csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
      return NO_ERROR;
    }

  /*
   * Check if the current transaction is in the list. If it is, adjust the
   * total saved space on the head entry. otherwise, create a new entry.
   */
  found = false;
  for (entry_p = head_p->first; entry_p != NULL; entry_p = entry_p->next)
    {
      if (tranid == entry_p->tranid)
	{
	  found = true;
	  break;
	}
    }

  if (found == true)
    {
      head_p->total_saved += space;
      entry_p->saved += space;
    }
  else
    {
      /* Current transaction is not in the list */

      /* Need to allocate an entry */
      entry_p = malloc (sizeof (*entry_p));
      if (entry_p == NULL)
	{
	  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
	  return ER_FAILED;
	}

      head_p->total_saved += space;

      entry_p->tranid = tranid;
      entry_p->saved = space;
      entry_p->head = head_p;
      entry_p->prev = NULL;
      entry_p->next = head_p->first;

      if (head_p->first != NULL)
	{
	  head_p->first->prev = entry_p;
	}

      head_p->first = entry_p;

      /*
       * Add this entry to the save entry list of this transaction.
       * It will be used to release the save entries when the transaction
       * is completed.
       */
      tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      if (tdes != NULL)
	{
	  entry_p->tran_next_save =
	    (SPAGE_SAVE_ENTRY *) (tdes->first_save_entry);
	  tdes->first_save_entry = (void *) entry_p;
	}
      else
	{
	  assert (false);
	  entry_p->tran_next_save = NULL;
	}

    }
  assert (head_p->total_saved >= 0);
  assert (entry_p->saved >= 0);

  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);

  return NO_ERROR;
}

/*
 * spage_get_saved_spaces_by_other_trans () - Find the total saved space by
 *                                            other transactions
 *   return:
 *
 *   page_header_p(in): Pointer to header of slotted page
 *   page_p(in): Pointer to slotted page
 */
static int
spage_get_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
				       SPAGE_HEADER * page_header_p,
				       PAGE_PTR page_p)
{
  int saved_by_other_trans = 0;

  spage_get_saved_spaces (thread_p, page_header_p, page_p,
			  &saved_by_other_trans);
  assert (saved_by_other_trans >= 0);

  return saved_by_other_trans;
}

/*
 * spage_get_total_saved_spaces () - Find the total saved space
 *   return:
 *
 *   page_header_p(in): Pointer to header of slotted page
 *   page_p(in): Pointer to slotted page
 */
static int
spage_get_total_saved_spaces (THREAD_ENTRY * thread_p,
			      SPAGE_HEADER * page_header_p, PAGE_PTR page_p)
{
  int total_saved = 0;
  int dummy;

  if (page_header_p->is_saving)
    {
      total_saved =
	spage_get_saved_spaces (thread_p, page_header_p, page_p, &dummy);
    }

  return total_saved;
}

/*
 * spage_get_saved_spaces () - Find the total saved space and the space by
                               other transactions
 *   return:
 *
 *   page_header_p(in): Pointer to header of slotted page
 *   page_p(in): Pointer to slotted page
 *   saved_by_other_trans(in/out) : The other transaction's saved space will
 *                                  be returned
 */
static int
spage_get_saved_spaces (THREAD_ENTRY * thread_p, SPAGE_HEADER * page_header_p,
			PAGE_PTR page_p, int *saved_by_other_trans)
{
  SPAGE_SAVE_HEAD *head_p;
  SPAGE_SAVE_ENTRY *entry_p;
  VPID *vpid_p;
  TRANID tranid;
  int total_saved, my_saved_space;

  assert (page_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  /*
   * If we are recovering, no other transaction should exist.
   */
  if (log_is_in_crash_recovery ())
    {
      if (saved_by_other_trans != NULL)
	{
	  *saved_by_other_trans = 0;
	}
      return 0;
    }

  tranid = logtb_find_current_tranid (thread_p);
  vpid_p = pgbuf_get_vpid_ptr (page_p);

  my_saved_space = 0;
  total_saved = 0;

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      /* TODO: missing to return ?? */
    }

  /*
   * Get the saved space held by the head of the save entries. This is
   * the aggregate value of spaces saved on all entries.
   */
  head_p = (SPAGE_SAVE_HEAD *) mht_get (spage_Mht_saving, vpid_p);
  if (head_p != NULL)
    {
      entry_p = head_p->first;
      while (entry_p != NULL)
	{
	  if (entry_p->tranid == tranid)
	    {
	      my_saved_space += entry_p->saved;
	      break;
	    }
	  entry_p = entry_p->next;
	}
      total_saved += head_p->total_saved;
    }

  /* Saved space should be 0 or positive. */
  assert (my_saved_space >= 0);
  assert (total_saved >= 0);
  assert (total_saved >= my_saved_space);

  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);

  if (saved_by_other_trans != NULL)
    {
      *saved_by_other_trans = total_saved - my_saved_space;
      assert (*saved_by_other_trans >= 0);
    }

  return total_saved;
}

/*
 * spage_dump_saved_spaces_by_other_trans () - Dump the savings associated with
 *                                             the given page that are part of
 *                                             the hash table
 *   return: void
 *
 *   fp(in/out):
 *   vpid_p(in):  Volume and page identifier
 */
static void
spage_dump_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p, FILE * fp,
					VPID * vpid_p)
{
  SPAGE_SAVE_ENTRY *entry_p;
  SPAGE_SAVE_HEAD *head_p;

  if (csect_enter_as_reader (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) !=
      NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      return;
    }

  head_p = (SPAGE_SAVE_HEAD *) mht_get (spage_Mht_saving, vpid_p);
  if (head_p != NULL)
    {
      fprintf (fp,
	       "Other savings of VPID = %d|%d total_saved = %d\n",
	       head_p->vpid.volid, head_p->vpid.pageid, head_p->total_saved);

      /* Go over the linked list of entries */
      entry_p = head_p->first;

      while (entry_p != NULL)
	{
	  fprintf (fp, "   Tranid = %d save = %d\n",
		   entry_p->tranid, entry_p->saved);
	  entry_p = entry_p->next;
	}
    }

  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
}

/*
 * spage_boot () - Initialize the slotted page module. The save_space hash table
 *              is initialized
 *   return:
 */
int
spage_boot (THREAD_ENTRY * thread_p)
{
  int r;

  assert (sizeof (SPAGE_HEADER) % DOUBLE_ALIGNMENT == 0);
  assert (sizeof (SPAGE_SLOT) == INT_ALIGNMENT);

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (spage_Mht_saving != NULL)
    {
      (void) mht_clear (spage_Mht_saving, NULL, NULL);
    }
  else
    {
      spage_Mht_saving = mht_create ("Page space saving table", 50,
				     pgbuf_hash_vpid, pgbuf_compare_vpid);
    }

  r = ((spage_Mht_saving != NULL) ? NO_ERROR : ER_FAILED);

  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);

  return r;
}

/*
 * spage_finalize () - Terminate the slotted page module
 *   return: void
 *
 * Note: Any memory allocated for the page space saving hash table is
 *       deallocated.
 */
void
spage_finalize (THREAD_ENTRY * thread_p)
{
  TRANID tranid = NULL_TRANID;

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  if (spage_Mht_saving != NULL)
    {
      (void) mht_map (spage_Mht_saving, spage_free_all_saved_spaces_helper,
		      &tranid);
      mht_destroy (spage_Mht_saving);
      spage_Mht_saving = NULL;
    }

  csect_exit (thread_p, CSECT_SPAGE_SAVESPACE);
}

/*
 * spage_slot_size () - Find the overhead used to store one slotted record
 *   return: overhead of slot
 */
int
spage_slot_size (void)
{
  return sizeof (SPAGE_SLOT);
}

/*
 * spage_header_size () - Find the overhead used by the page header
 *   return: overhead of slot
 */
int
spage_header_size (void)
{
  return sizeof (SPAGE_HEADER);
}

/*
 * spage_max_record_size () - Find the maximum record length that can be stored in
 *                       a slotted page
 *   return: Max length for a new record
 */
int
spage_max_record_size (void)
{
  return DB_PAGESIZE - sizeof (SPAGE_HEADER) - sizeof (SPAGE_SLOT);
}

/*
 * spage_number_of_records () - Return the total number of records in the slotted page
 *   return: Number of records (PGNSLOTS)
 *
 *   page_p(in): Pointer to slotted page
 */
PGNSLOTS
spage_number_of_records (PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  return page_header_p->num_records;
}

/*
 * spage_number_of_slots () - Return the total number of slots in the slotted page
 *   return: Number of slots (PGNSLOTS)
 *
 *   page_p(in): Pointer to slotted page
 */
PGNSLOTS
spage_number_of_slots (PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  return page_header_p->num_slots;
}

/*
 * spage_get_free_space () - Returns total free area
 *   return: Total free space
 *
 *   page_p(in): Pointer to slotted page
 */
int
spage_get_free_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  int free_space;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  free_space = (page_header_p->total_free -
		spage_get_total_saved_spaces (thread_p, page_header_p,
					      page_p));
  if (free_space < 0)
    {
      free_space = 0;
    }

  return free_space;
}

/*
 * spage_get_free_space_without_saving () - Returns total free area without saving
 *   return: Total free space
 *
 *   page_p(in): Pointer to slotted page
 *   need_update(out): Value of need_update_best_hint
 */
int
spage_get_free_space_without_saving (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				     bool * need_update)
{
  SPAGE_HEADER *page_header_p;
  int free_space;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (need_update != NULL)
    {
      *need_update = page_header_p->need_update_best_hint;
    }

  free_space = page_header_p->total_free;
  if (free_space < 0)
    {				/* defense code */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      free_space = 0;
    }

  return free_space;
}

/*
 * spage_set_need_update_best_hint () - Set need_update_best_hint on slotted page header
 *   return: void
 *
 *   page_p(in): Pointer to slotted page
 *   need_update(in): Value of need_update_best_hint
 *
 * NOTE: We will set this value as TRUE when we failed updating best page hints
 *       on heap header. See heap_stats_update function.
 */
void
spage_set_need_update_best_hint (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				 bool need_update)
{
  SPAGE_HEADER *page_header_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  page_header_p->need_update_best_hint = need_update;
}

/*
 * spage_max_space_for_new_record () - Find the maximum free space for a new
 *                                     insertion
 *   return: Maximum free length for an insertion
 *   page_p(in): Pointer to slotted page
 *
 * Note: The function subtract any pointer array information that may be
 *       needed for a new insertion.
 */
int
spage_max_space_for_new_record (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  int total_free;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  total_free = (page_header_p->total_free -
		spage_get_total_saved_spaces (thread_p, page_header_p,
					      page_p));

  if ((page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS)
      && (page_header_p->num_slots > page_header_p->num_records))
    {
      if (total_free < 0)
	{
	  total_free = 0;
	}
    }
  else
    {
      total_free -= SSIZEOF (SPAGE_SLOT);
      if (total_free < SSIZEOF (SPAGE_SLOT))
	{
	  total_free = 0;
	}
    }

  total_free = DB_ALIGN_BELOW (total_free, page_header_p->alignment);
  if (total_free < 0)
    {
      total_free = 0;
    }

  return total_free;
}

/*
 * spage_collect_statistics () - Collect statistical information of the page
 *   return: none
 *
 *   page_p(in): Pointer to slotted page
 *   npages(out): the number of pages
 *   nrecords(out): the number of records
 *   rec_length(out): total length of records
 */
void
spage_collect_statistics (PAGE_PTR page_p,
			  int *npages, int *nrecords, int *rec_length)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  PGNSLOTS i, last_slot_id;
  int pages, records, length;

  assert (page_p != NULL);
  assert (npages != NULL && nrecords != NULL && rec_length != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  last_slot_id = page_header_p->num_slots - 1;
  slot_p = spage_find_slot (page_p, page_header_p, last_slot_id, false);

  pages = 1;
  records = 0;
  length = 0;

  for (i = last_slot_id; i > 0; slot_p++, i--)
    {
      if (slot_p->offset_to_record == SPAGE_EMPTY_OFFSET)
	{
	  continue;
	}

      if (slot_p->record_type == REC_BIGONE)
	{
	  pages += 2;
	  length += (DB_PAGESIZE * 2);	/* Assume two pages */
	}

      if (slot_p->record_type != REC_NEWHOME)
	{
	  records++;
	}

      if (slot_p->record_type == REC_HOME
	  || slot_p->record_type == REC_NEWHOME)
	{
	  length += slot_p->record_length;
	}
    }

  *npages = pages;
  *nrecords = records;
  *rec_length = length;
}

/*
 * spage_initialize () - Initialize a slotted page
 *   return: void
 *
 *   page_p(in): Pointer to slotted page
 *   slots_type(in): Flag which indicates the type of slots
 *   alignment(in): page alignment type
 *   safeguard_rvspace(in): Save space during updates. for transaction recovery
 *
 * Note: A slotted page must be initialized before records are inserted on the
 *       page. The alignment indicates the valid offset where the records
 *       should be stored. This is a requirement for peeking records on pages
 *       according to alignment restrictions.
 *       A slotted page can optionally be initialized with recovery safeguard
 *       space in mind. In this case when records are removed or shrunk, the
 *       space is saved for possible undoes.
 */
void
spage_initialize (THREAD_ENTRY * thread_p, PAGE_PTR page_p, INT16 slot_type,
		  unsigned short alignment, bool is_saving)
{
  SPAGE_HEADER *page_header_p;

  assert (page_p != NULL);
  assert (spage_is_valid_anchor_type (slot_type));
  assert (alignment == CHAR_ALIGNMENT || alignment == SHORT_ALIGNMENT ||
	  alignment == INT_ALIGNMENT || alignment == LONG_ALIGNMENT ||
	  alignment == FLOAT_ALIGNMENT || alignment == DOUBLE_ALIGNMENT);

  page_header_p = (SPAGE_HEADER *) page_p;

  page_header_p->num_slots = 0;
  page_header_p->num_records = 0;
  page_header_p->is_saving = is_saving;
  page_header_p->flags = SPAGE_HEADER_FLAG_NONE;
  page_header_p->need_update_best_hint = 0;
  page_header_p->reserved_bits = 0;
  page_header_p->reserved1 = 0;

  page_header_p->anchor_type = slot_type;
  page_header_p->total_free = DB_ALIGN (DB_PAGESIZE - sizeof (SPAGE_HEADER),
					alignment);

  page_header_p->cont_free = page_header_p->total_free;
  page_header_p->offset_to_free_area = DB_ALIGN (sizeof (SPAGE_HEADER),
						 alignment);

  page_header_p->alignment = alignment;
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
}

/*
 * spage_compare_slot_offset () - Compare the location (offset) of slots
 *   return: s1 - s2
 *   s1(in): slot 1
 *   s2(in): slot 2
 */
static int
spage_compare_slot_offset (const void *arg1, const void *arg2)
{
  SPAGE_SLOT **s1, **s2;

  assert (arg1 != NULL);
  assert (arg2 != NULL);

  s1 = (SPAGE_SLOT **) arg1;
  s2 = (SPAGE_SLOT **) arg2;

  return ((*s1)->offset_to_record - (*s2)->offset_to_record);
}

/*
 * spage_is_slotted_page_type () - Whether or not a slotted page type
 *   return: yes - true, no - false
 *
 *   ptype(in): page type
 */
static bool
spage_is_slotted_page_type (PAGE_TYPE ptype)
{
  switch (ptype)
    {
    case PAGE_HEAP:
    case PAGE_BTREE:
    case PAGE_EHASH:
    case PAGE_LARGEOBJ:
    case PAGE_CATALOG:
      return true;

    default:
      return false;
    }
}

/*
 * spage_compact () -  Compact an slotted page
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *
 * Note: Only the records are compacted, the slots are not compacted.
 */
int
spage_compact (PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  SPAGE_SLOT **slot_array = NULL;
  int to_offset;
  int i, j;
  PAGE_TYPE ptype;

  assert (page_p != NULL);

  ptype = pgbuf_get_page_ptype (NULL, page_p);
  assert_release (spage_is_slotted_page_type (ptype));

  page_header_p = (SPAGE_HEADER *) page_p;
  spage_verify_header (page_p);

  if (page_header_p->num_records > 0)
    {
      slot_array =
	(SPAGE_SLOT **) calloc ((unsigned int) (page_header_p->num_slots),
				sizeof (SPAGE_SLOT *));
      if (slot_array == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (page_header_p->num_slots) * sizeof (SPAGE_SLOT *));
	  return ER_FAILED;
	}

      /* Create an array of sorted offsets... actually pointers to slots */

      slot_p = spage_find_slot (page_p, page_header_p, 0, false);
      for (j = 0, i = 0; i < page_header_p->num_slots; slot_p--, i++)
	{
	  if (slot_p->record_type < REC_UNKNOWN
	      || slot_p->record_type > REC_4BIT_USED_TYPE_MAX)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return ER_FAILED;
	    }
	  if (slot_p->offset_to_record != SPAGE_EMPTY_OFFSET)
	    {
	      slot_array[j++] = slot_p;
	    }
	}

      if (page_header_p->num_records != j)
	{
	  VPID vpid;

	  pgbuf_get_vpid (page_p, &vpid);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SP_WRONG_NUM_SLOTS, 4,
		  vpid.pageid, fileio_get_volume_label (vpid.volid, PEEK),
		  j, page_header_p->num_records);

	  assert (false);

	  free_and_init (slot_array);

	  /* will exit here */
	  logpb_fatal_error_exit_immediately_wo_flush (NULL, ARG_FILE_LINE,
						       "spage_compact");
	  return ER_FAILED;
	}

      qsort ((void *) slot_array, page_header_p->num_records,
	     sizeof (SPAGE_SLOT *), spage_compare_slot_offset);

      /* Now start compacting the page */
      to_offset = sizeof (SPAGE_HEADER);
      for (i = 0; i < page_header_p->num_records; i++)
	{
	  /* Make sure that the offset is aligned */
	  to_offset = DB_ALIGN (to_offset, page_header_p->alignment);
	  if (to_offset == slot_array[i]->offset_to_record)
	    {
	      /* Record slot is already in place */
	      to_offset += slot_array[i]->record_length;
	    }
	  else
	    {
	      /* Move the record */
	      if ((unsigned int) to_offset + slot_array[i]->record_length >
		  (unsigned int) DB_PAGESIZE)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			  0);
		  assert_release (false);
		  return ER_FAILED;
		}
	      memmove ((char *) page_p + to_offset,
		       (char *) page_p + slot_array[i]->offset_to_record,
		       slot_array[i]->record_length);
	      slot_array[i]->offset_to_record = to_offset;
	      to_offset += slot_array[i]->record_length;
	    }
	}
      free_and_init (slot_array);
    }
  else
    {
      to_offset = sizeof (SPAGE_HEADER);
    }

  /* Make sure that the next inserted record will be aligned */
  to_offset = DB_ALIGN (to_offset, page_header_p->alignment);
  page_header_p->total_free = (DB_PAGESIZE - to_offset
			       - (page_header_p->num_slots
				  * sizeof (SPAGE_SLOT)));
  page_header_p->cont_free = page_header_p->total_free;

  page_header_p->offset_to_free_area = to_offset;

  spage_verify_header (page_p);

  /* The page is set dirty somewhere else */
  return NO_ERROR;
}

/*
 * spage_find_free_slot () -
 *   return: void
 *
 *   page_p(in): Pointer to slotted page
 *   out_slot_p(out):
 *   start_slot(in):
 */
PGSLOTID
spage_find_free_slot (PAGE_PTR page_p, SPAGE_SLOT ** out_slot_p,
		      PGSLOTID start_slot)
{
  PGSLOTID slot_id;
  SPAGE_SLOT *slot_p;
  SPAGE_HEADER *page_header_p;

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, 0, false);

  if (page_header_p->num_slots == page_header_p->num_records)
    {
      slot_id = page_header_p->num_slots;
      slot_p -= slot_id;
    }
  else
    {
      for (slot_id = start_slot, slot_p -= slot_id;
	   slot_id < page_header_p->num_slots; slot_p--, slot_id++)
	{
	  if (slot_p->record_type == REC_DELETED_WILL_REUSE)
	    {
	      break;		/* found free slot */
	    }
	}
    }

  if (out_slot_p != NULL)
    {
      *out_slot_p = slot_p;
    }

  if (spage_find_slot (page_p, page_header_p, slot_id, false) != slot_p)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  return slot_id;
}

/*
 * spage_check_space () -
 *   return: void
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in):
 *   space(in):
 */
static int
spage_check_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		   SPAGE_HEADER * page_header_p, int space)
{
  if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
				    space) == false)
    {
      return SP_DOESNT_FIT;
    }
  else if (spage_has_enough_contiguous_space (page_p, page_header_p,
					      space) == false)
    {
      return SP_ERROR;
    }

  return SP_SUCCESS;
}

/*
 * spage_set_slot () -
 *   return:
 *
 *   slot_p(in): Pointer to slotted page array pointer
 *   offset(in):
 *   length(in):
 *   type(in):
 */
static void
spage_set_slot (SPAGE_SLOT * slot_p, int offset, int length, INT16 type)
{
  assert (REC_UNKNOWN <= type && type <= REC_4BIT_USED_TYPE_MAX);

  slot_p->offset_to_record = offset;
  slot_p->record_length = length;
  slot_p->record_type = type;
}

/*
 * spage_find_empty_slot () - Find a free area/slot where a record of
 *               the given length can be inserted onto the given slotted page
 *   return: either SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   record_length(in): Length of area/record
 *   record_type(in): Type of record to be inserted
 *   out_slot_p(out): Pointer to slotted page array pointer
 *   out_space_p(out): Space used/defined
 *   out_slot_id_p(out): Allocated slot or NULL_SLOTID
 *
 * Note: If there is not enough space on the page, an error condition is
 *       returned and out_slot_id_p is set to NULL_SLOTID.
 */
static int
spage_find_empty_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		       int record_length, INT16 record_type,
		       SPAGE_SLOT ** out_slot_p, int *out_space_p,
		       PGSLOTID * out_slot_id_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  PGSLOTID slot_id;
  int waste, space, status;

  assert (page_p != NULL);
  assert (out_slot_p != NULL);
  assert (out_space_p != NULL);
  assert (out_slot_id_p != NULL);

  *out_slot_p = NULL;
  *out_space_p = 0;
  *out_slot_id_p = NULL_SLOTID;

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  /* Calculate the wasted space that this record will introduce. We need to
     take in consideration the wasted space when there is space saved */
  waste = DB_WASTED_ALIGN (record_length, page_header_p->alignment);
  space = record_length + waste;

  /* Quickly check for available space. We may need to check again if a slot
     is created (instead of reused) */
  if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
				    space) == false)
    {
      return SP_DOESNT_FIT;
    }

  /* Find a free slot. Try to reuse an unused slotid, instead of allocating a
     new one */
  slot_id = spage_find_free_slot (page_p, &slot_p, 0);

  /* Make sure that there is enough space for the record and the slot */

  if (slot_id > page_header_p->num_slots)
    {				/* defense code */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  if (slot_id == page_header_p->num_slots)
    {
      /* We are allocating a new slotid. Check for available space again */
      space += sizeof (SPAGE_SLOT);

      status = spage_check_space (thread_p, page_p, page_header_p, space);
      if (status != SP_SUCCESS)
	{
	  return status;
	}

      /* Adjust the number of slots */
      page_header_p->num_slots++;
    }
  else
    {
      /* We already know that there is total space available since the slot is
         reused and the space was checked above */
      if (spage_has_enough_contiguous_space (page_p, page_header_p,
					     space) == false)
	{
	  return SP_ERROR;
	}
    }

  /* Now separate an empty area for the record */
  spage_set_slot (slot_p, page_header_p->offset_to_free_area, record_length,
		  record_type);

  /* Adjust the header */
  page_header_p->num_records++;
  page_header_p->total_free -= space;
  page_header_p->cont_free -= space;
  page_header_p->offset_to_free_area += (record_length + waste);

  spage_verify_header (page_p);

  *out_slot_p = slot_p;
  *out_space_p = space;
  *out_slot_id_p = slot_id;

  /* The page is set dirty somewhere else */
  return SP_SUCCESS;
}

/*
 * spage_shift_slot_up() -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in/out): Pointer to slotted page pointer array
 */
static void
spage_shift_slot_up (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		     SPAGE_SLOT * slot_p)
{
  SPAGE_SLOT *last_slot_p;

  assert (page_p != NULL);
  assert (slot_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  last_slot_p = spage_find_slot (page_p, page_header_p,
				 page_header_p->num_slots, false);

  if (page_header_p->anchor_type == UNANCHORED_ANY_SEQUENCE)
    {
      spage_set_slot (last_slot_p, slot_p->offset_to_record,
		      slot_p->record_length, slot_p->record_type);
    }
  else
    {
      for (; last_slot_p < slot_p; last_slot_p++)
	{
	  spage_set_slot (last_slot_p, (last_slot_p + 1)->offset_to_record,
			  (last_slot_p + 1)->record_length,
			  (last_slot_p + 1)->record_type);
	}
      assert (last_slot_p == slot_p);
    }

  spage_set_slot (slot_p, SPAGE_EMPTY_OFFSET, 0, REC_UNKNOWN);

  SPAGE_VERIFY_HEADER (page_header_p);
}

/*
 * spage_shift_slot_down () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in/out): Pointer to slotted page pointer array
 */
static void
spage_shift_slot_down (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		       SPAGE_SLOT * slot_p)
{
  SPAGE_SLOT *last_slot_p;

  assert (page_p != NULL);
  assert (slot_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  last_slot_p = spage_find_slot (page_p, page_header_p,
				 page_header_p->num_slots - 1, false);

  if (page_header_p->anchor_type == UNANCHORED_ANY_SEQUENCE)
    {
      spage_set_slot (slot_p, last_slot_p->offset_to_record,
		      last_slot_p->record_length, last_slot_p->record_type);
    }
  else
    {
      for (; slot_p > last_slot_p; slot_p--)
	{
	  spage_set_slot (slot_p, (slot_p - 1)->offset_to_record,
			  (slot_p - 1)->record_length,
			  (slot_p - 1)->record_type);
	}
    }

  spage_set_slot (last_slot_p, SPAGE_EMPTY_OFFSET, 0, REC_UNKNOWN);

  SPAGE_VERIFY_HEADER (page_header_p);
}

/*
 * spage_add_new_slot () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   out_space_p(in/out):
 */
static int
spage_add_new_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		    SPAGE_HEADER * page_header_p, int *out_space_p)
{
  SPAGE_SLOT *last_slot_p;
  int status;

  /*
   * New one slot is are being allocated.
   */
  SPAGE_VERIFY_HEADER (page_header_p);

  *out_space_p += sizeof (SPAGE_SLOT);

  status = spage_check_space (thread_p, page_p, page_header_p, *out_space_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* Adjust the space for creation of new slot. The space for the record
     is adjusted later on */
  page_header_p->num_slots++;

  last_slot_p = spage_find_slot (page_p, page_header_p,
				 page_header_p->num_slots - 1, false);
  spage_set_slot (last_slot_p, SPAGE_EMPTY_OFFSET, 0, REC_UNKNOWN);

  spage_verify_header (page_p);

  return SP_SUCCESS;
}

/*
 * spage_take_slot_in_use () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_id(in):
 *   slot_p(in/out):
 *   out_space_p(in/out):
 */
static int
spage_take_slot_in_use (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			SPAGE_HEADER * page_header_p, PGSLOTID slot_id,
			SPAGE_SLOT * slot_p, int *out_space_p)
{
  int status;

  SPAGE_VERIFY_HEADER (page_header_p);

  /*
   * An already defined slot. The slotid can be used in the following
   * cases:
   * 1) The slot is marked as deleted. (Reuse)
   * 2) The type of slotted page is unanchored and there is space to define
   *    a new slot for the shift operation
   */
  if (slot_p->record_type == REC_DELETED_WILL_REUSE)
    {
      /* Make sure that there is enough space for the record. There is not
         need for slot space. */
      status = spage_check_space (thread_p, page_p,
				  page_header_p, *out_space_p);
      if (status != SP_SUCCESS)
	{
	  return status;
	}
    }
  else
    {
      /* Slot is in use. The pointer array must be shifted
         (i.e., addresses of records are modified). */
      if (page_header_p->anchor_type == ANCHORED
	  || page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS)
	{
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SP_BAD_INSERTION_SLOT, 3, slot_id,
		  pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p));
	  return SP_ERROR;
	}

      /* Make sure that there is enough space for the record and the slot */

      *out_space_p += sizeof (SPAGE_SLOT);
      status = spage_check_space (thread_p, page_p, page_header_p,
				  *out_space_p);
      if (status != SP_SUCCESS)
	{
	  return status;
	}

      spage_shift_slot_up (page_p, page_header_p, slot_p);
      /* Adjust the header for the newly created slot */
      page_header_p->num_slots += 1;
    }

  spage_verify_header (page_p);

  return SP_SUCCESS;
}

/*
 * spage_find_empty_slot_at () - Find a free area where a record of
 *                   the given length can be inserted onto the given slotted page
 *   return: either SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Requested slot_id
 *   record_length(in): Length of area/record
 *   record_type(in): Type of record to be inserted
 *   out_slot_p(out): Pointer to slotted page array pointer
 *
 */
static int
spage_find_empty_slot_at (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			  PGSLOTID slot_id, int record_length,
			  INT16 record_type, SPAGE_SLOT ** out_slot_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int waste, space, status;

  assert (page_p != NULL);
  assert (out_slot_p != NULL);

  *out_slot_p = NULL;

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);

  /* Calculate the wasted space that this record will introduce. We need to
     take in consideration the wasted space when there is space saved */
  waste = DB_WASTED_ALIGN (record_length, page_header_p->alignment);
  space = record_length + waste;

  if (slot_id > page_header_p->num_slots)
    {				/* defense code */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  if (slot_id == page_header_p->num_slots)
    {
      assert (log_is_in_crash_recovery ()
	      || !(page_header_p->is_saving
		   && !logtb_is_current_active (thread_p)));

      status = spage_add_new_slot (thread_p, page_p, page_header_p, &space);
    }
  else
    {
      status = spage_take_slot_in_use (thread_p, page_p, page_header_p,
				       slot_id, slot_p, &space);
    }

  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* Now separate an empty area for the record */
  spage_set_slot (slot_p, page_header_p->offset_to_free_area, record_length,
		  record_type);

  /* Adjust the header */
  page_header_p->num_records++;
  page_header_p->total_free -= space;
  page_header_p->cont_free -= space;
  page_header_p->offset_to_free_area += (record_length + waste);

  spage_verify_header (page_p);

  *out_slot_p = slot_p;

  /* The page is set dirty somewhere else */
  return SP_SUCCESS;
}

/*
 * spage_check_record_for_insert () -
 *   return:
 *
 *   record_descriptor_p(in):
 */
static int
spage_check_record_for_insert (RECDES * record_descriptor_p)
{
  if (record_descriptor_p->length > spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  if (record_descriptor_p->type == REC_MARKDELETED
      || record_descriptor_p->type == REC_DELETED_WILL_REUSE)
    {
      record_descriptor_p->type = REC_HOME;
    }

  return SP_SUCCESS;
}

/*
 * spage_insert () - Insert a record
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   record_descriptor_p(in): Pointer to a record descriptor
 *   out_slot_id_p(out): Slot identifier
 */
int
spage_insert (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
	      RECDES * record_descriptor_p, PGSLOTID * out_slot_id_p)
{
  SPAGE_SLOT *slot_p;
  int used_space;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);
  assert (out_slot_id_p != NULL);

  status = spage_find_slot_for_insert (thread_p, page_p, record_descriptor_p,
				       out_slot_id_p, (void **) &slot_p,
				       &used_space);
  if (status == SP_SUCCESS)
    {
      status = spage_insert_data (thread_p, page_p,
				  record_descriptor_p, slot_p);
    }

  return status;
}

/*
 * spage_find_slot_for_insert () - Find a slot id and related information in the
 *                          given page
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   record_descriptor_p(in): Pointer to a record descriptor
 *   out_slot_id_p(out): Slot identifier
 *   out_slot_p(out): Pointer to slotted array
 *   out_used_space_p(out): Pointer to int
 */
int
spage_find_slot_for_insert (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			    RECDES * record_descriptor_p,
			    PGSLOTID * out_slot_id_p, void **out_slot_p,
			    int *out_used_space_p)
{
  SPAGE_SLOT *slot_p;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);
  assert (out_slot_id_p != NULL);
  assert (out_slot_p != NULL);
  assert (out_used_space_p != NULL);

  *out_slot_id_p = NULL_SLOTID;
  status = spage_check_record_for_insert (record_descriptor_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  status = spage_find_empty_slot (thread_p, page_p,
				  record_descriptor_p->length,
				  record_descriptor_p->type, &slot_p,
				  out_used_space_p, out_slot_id_p);
  *out_slot_p = (void *) slot_p;

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return status;
}

/*
 * spage_insert_data () - Copy the contents of a record into the given page/slot
 *   return: either of SP_ERROR, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   record_descriptor_p(in): Pointer to a record descriptor
 *   slot_p(in): Pointer to slotted array
 */
int
spage_insert_data (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		   RECDES * record_descriptor_p, void *slot_p)
{
  SPAGE_SLOT *tmp_slot_p;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);
  assert (slot_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      return SP_ERROR;
    }

  tmp_slot_p = (SPAGE_SLOT *) slot_p;
  if (record_descriptor_p->type != REC_ASSIGN_ADDRESS)
    {
      if ((unsigned int) tmp_slot_p->offset_to_record +
	  record_descriptor_p->length > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}

      memcpy (((char *) page_p + tmp_slot_p->offset_to_record),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  else
    {
      if (tmp_slot_p->offset_to_record + SSIZEOF (TRANID) >
	  (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}

      *((TRANID *) (page_p + tmp_slot_p->offset_to_record)) =
	logtb_find_current_tranid (thread_p);
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_insert_at () - Insert a record onto the given slotted page at the given
 *                  slot_id
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot_id for newly inserted record
 *   record_descriptor_p(in): Pointer to a record descriptor
 *
 * Note: The records on this page must be UNANCHORED, otherwise, an error is
 *       set and an indication of an error is returned. If the record does not
 *       fit on the page, such effect is returned.
 */
int
spage_insert_at (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
		 RECDES * record_descriptor_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      return SP_ERROR;
    }

  status = spage_check_record_for_insert (record_descriptor_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (slot_id > page_header_p->num_slots)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  status = spage_find_empty_slot_at (thread_p, page_p, slot_id,
				     record_descriptor_p->length,
				     record_descriptor_p->type, &slot_p);
  if (status == SP_SUCCESS)
    {
      status = spage_insert_data (thread_p, page_p,
				  record_descriptor_p, slot_p);
    }

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return status;
}

/*
 * spage_insert_for_recovery () - Insert a record onto the given slotted page at
 *                                the given slot_id (only for recovery)
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot_id for insertion
 *   record_descriptor_p(in): Pointer to a record descriptor
 *
 * Note: If there is a record located at this slot and the page type of the
 *       page is anchored the slot record will be replaced by the new record.
 *       Otherwise, the slots will be moved.
 */
int
spage_insert_for_recovery (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			   PGSLOTID slot_id, RECDES * record_descriptor_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      return SP_ERROR;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (page_header_p->anchor_type != ANCHORED
      && page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS)
    {
      return spage_insert_at (thread_p, page_p, slot_id, record_descriptor_p);
    }

  status = spage_check_record_for_insert (record_descriptor_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* If there is a record located at the given slot, the record is removed */

  if (slot_id < page_header_p->num_slots)
    {
      slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);

      assert (page_header_p->anchor_type == ANCHORED ||
	      page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS);
      assert (slot_p->offset_to_record == SPAGE_EMPTY_OFFSET);

      slot_p->record_type = REC_DELETED_WILL_REUSE;
    }

  status = spage_find_empty_slot_at (thread_p, page_p, slot_id,
				     record_descriptor_p->length,
				     record_descriptor_p->type, &slot_p);
  if (status == SP_SUCCESS)
    {
      if (record_descriptor_p->type != REC_ASSIGN_ADDRESS)
	{
	  if (slot_p->offset_to_record + record_descriptor_p->length
	      > (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy (((char *) page_p + slot_p->offset_to_record),
		  record_descriptor_p->data, record_descriptor_p->length);
	}

      SPAGE_VERIFY_HEADER (page_header_p);

      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return status;
}

/*
 * spage_is_record_located_at_end () -
 *   return:
 *
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in):
 */
static bool
spage_is_record_located_at_end (SPAGE_HEADER * page_header_p,
				SPAGE_SLOT * slot_p)
{
  int waste;

  waste = DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment);

  assert ((int) slot_p->offset_to_record + (int) slot_p->record_length +
	  waste <= page_header_p->offset_to_free_area);

  return ((int) slot_p->offset_to_record + (int) slot_p->record_length +
	  waste) == page_header_p->offset_to_free_area;
}

/*
 * spage_reduce_a_slot () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 */
static void
spage_reduce_a_slot (PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  page_header_p = (SPAGE_HEADER *) page_p;

  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p,
			    page_header_p->num_slots - 1, false);
  spage_set_slot (slot_p, SPAGE_EMPTY_OFFSET, 0, REC_UNKNOWN);

  page_header_p->num_slots--;
  page_header_p->total_free += sizeof (SPAGE_SLOT);
  page_header_p->cont_free += sizeof (SPAGE_SLOT);

  spage_verify_header (page_p);
}

/*
 * spage_delete () - Delete the record located at given slot on the given page
 *   return: slot_id on success and NULL_SLOTID on failure
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to delete
 */
PGSLOTID
spage_delete (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int waste;
  int free_space;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  assert (spage_is_valid_anchor_type (page_header_p->anchor_type));

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return NULL_SLOTID;
    }

  page_header_p->num_records--;
  waste = DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment);
  free_space = slot_p->record_length + waste;
  page_header_p->total_free += free_space;

  /* If it is the last slot in the page, the contiguous free space can be
     adjusted. Avoid future compaction as much as possible */
  if (spage_is_record_located_at_end (page_header_p, slot_p))
    {
      page_header_p->cont_free += free_space;
      page_header_p->offset_to_free_area -= free_space;
    }

  switch (page_header_p->anchor_type)
    {
    case ANCHORED:
      slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      slot_p->record_type = REC_DELETED_WILL_REUSE;
      break;
    case ANCHORED_DONT_REUSE_SLOTS:
      slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      slot_p->record_type = REC_MARKDELETED;
      break;
    case UNANCHORED_ANY_SEQUENCE:
    case UNANCHORED_KEEP_SEQUENCE:
      assert (page_header_p->is_saving == false);

      spage_shift_slot_down (page_p, page_header_p, slot_p);

      spage_reduce_a_slot (page_p);
      free_space += sizeof (SPAGE_SLOT);
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL_SLOTID;
    }

  /* Indicate that we are savings */
  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   free_space) != NO_ERROR)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL_SLOTID;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  spage_verify_header (page_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return slot_id;
}

/*
 * spage_delete_for_recovery () - Delete the record located at given slot on the given page
 *                  (only for recovery)
 *   return: slot_id on success and NULL_SLOTID on failure
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to delete
 *
 * Note: The slot is always reused even in ANCHORED_DONT_REUSE_SLOTS pages
 *       since the record was never made permanent.
 */
PGSLOTID
spage_delete_for_recovery (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			   PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  if (spage_delete (thread_p, page_p, slot_id) != slot_id)
    {
      return NULL_SLOTID;
    }

  /* Set the slot as deleted with reuse since the address was never permanent */
  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS)
    {
      slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);
      if (slot_p->offset_to_record == SPAGE_EMPTY_OFFSET
	  && slot_p->record_type == REC_MARKDELETED)
	{
	  slot_p->record_type = REC_DELETED_WILL_REUSE;
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	}
    }

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return slot_id;
}

/*
 * spage_check_updatable () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to delete
 *   record_descriptor_p(in):
 *   out_slot_p(out):
 *   out_space_p(out):
 *   out_old_waste_p(out):
 *   out_new_waste_p(out):
 */
static int
spage_check_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		       PGSLOTID slot_id, int record_descriptor_length,
		       SPAGE_SLOT ** out_slot_p, int *out_space_p,
		       int *out_old_waste_p, int *out_new_waste_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int new_waste, old_waste;
  int space;

  assert (page_p != NULL);

  if (record_descriptor_length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  if (record_descriptor_length > spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  old_waste = DB_WASTED_ALIGN (slot_p->record_length,
			       page_header_p->alignment);
  new_waste = DB_WASTED_ALIGN (record_descriptor_length,
			       page_header_p->alignment);
  space = record_descriptor_length + new_waste - slot_p->record_length -
    old_waste;

  if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
				    space) == false)
    {
      return SP_DOESNT_FIT;
    }

  if (out_slot_p)
    {
      *out_slot_p = slot_p;
    }

  if (out_space_p)
    {
      *out_space_p = space;
    }

  if (out_old_waste_p)
    {
      *out_old_waste_p = old_waste;
    }

  if (out_new_waste_p)
    {
      *out_new_waste_p = new_waste;
    }

  return SP_SUCCESS;
}

/*
* spage_check_mvcc_updatable () - check whether is enough free area to update
*                   a record in MVCC
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to logical delete
 *   mvcc_delete_record_length(in): the length of delete record
 *   mvcc_insert_record_length(in): the length of insert record 
 */
static int
spage_check_mvcc_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			    PGSLOTID slot_id, int mvcc_delete_record_length,
			    int mvcc_insert_record_length)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int new_waste, old_waste;
  int space;
  int insert_record_space;
  int max_record_size;

  assert (mvcc_Enabled == true && page_p != NULL);

  if (mvcc_delete_record_length < 0 || mvcc_insert_record_length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  max_record_size = spage_max_record_size ();
  if (mvcc_delete_record_length > max_record_size
      || mvcc_insert_record_length > max_record_size)
    {
      return SP_DOESNT_FIT;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  old_waste = DB_WASTED_ALIGN (slot_p->record_length,
			       page_header_p->alignment);
  new_waste = DB_WASTED_ALIGN (mvcc_delete_record_length,
			       page_header_p->alignment);
  space = mvcc_delete_record_length + new_waste - slot_p->record_length -
    old_waste;

  if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
				    space) == false)
    {
      return SP_DOESNT_FIT;
    }

  /* delete record fit into page_p
   * check whether both delete and insert record fit into page_p
   */
  new_waste = DB_WASTED_ALIGN (mvcc_insert_record_length,
			       page_header_p->alignment);
  insert_record_space = (mvcc_insert_record_length + new_waste);
  space += insert_record_space;

  if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
				    space) == false)
    {
      return SP_DOESNT_FIT;
    }

  /* Find a free slot. Try to reuse an unused slotid, instead of allocating a
     new one */
  slot_id = spage_find_free_slot (page_p, &slot_p, 0);

  /* Make sure that there is enough space for the record and the slot */
  if (slot_id > page_header_p->num_slots)
    {				/* defense code */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  if (slot_id == page_header_p->num_slots)
    {
      /* check whether is enough space for old record, new record and the slot */
      insert_record_space += sizeof (SPAGE_SLOT);
      space += sizeof (SPAGE_SLOT);

      if (spage_has_enough_total_space (thread_p, page_p, page_header_p,
					space) == false)
	{
	  return SP_DOESNT_FIT;
	}
      else if (spage_has_enough_contiguous_space (page_p, page_header_p,
						  insert_record_space) ==
	       false)
	{
	  return SP_ERROR;
	}
    }
  else
    {
      /* We already know that there is total space available since the slot is
         reused and the space was checked above */
      if (spage_has_enough_contiguous_space (page_p, page_header_p,
					     insert_record_space) == false)
	{
	  return SP_ERROR;
	}
    }

  return SP_SUCCESS;
}

/*
 * spage_update_record_in_place () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in/out): Pointer to slotted page pointer array
 *   record_descriptor_p(in/out):
 *   space(in):
 */
static int
spage_update_record_in_place (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
			      SPAGE_SLOT * slot_p,
			      const RECDES * record_descriptor_p, int space)
{
  bool is_located_end;

  SPAGE_VERIFY_HEADER (page_header_p);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  /* Update the record in place. Same area */
  is_located_end = spage_is_record_located_at_end (page_header_p, slot_p);

  slot_p->record_length = record_descriptor_p->length;
  if (slot_p->offset_to_record + record_descriptor_p->length >
      (unsigned int) DB_PAGESIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      assert_release (false);
      return SP_ERROR;
    }
  memcpy (((char *) page_p + slot_p->offset_to_record),
	  record_descriptor_p->data, record_descriptor_p->length);
  page_header_p->total_free -= space;

  /* If the record was located at the end, we can execute a simple
     compaction */
  if (is_located_end)
    {
      page_header_p->cont_free -= space;
      page_header_p->offset_to_free_area += space;

      SPAGE_VERIFY_HEADER (page_header_p);
    }

  spage_verify_header (page_p);

  return SP_SUCCESS;
}

/*
 * spage_update_record_after_compact () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in/out): Pointer to slotted page pointer array
 *   record_descriptor_p(in/out):
 *   space(in):
 *   old_waste(in):
 *   new_waste(in):
 */
static int
spage_update_record_after_compact (PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   SPAGE_SLOT * slot_p,
				   const RECDES * record_descriptor_p,
				   int space, int old_waste, int new_waste)
{
  int old_offset;

  SPAGE_VERIFY_HEADER (page_header_p);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  /*
   * If record does not fit in the contiguous free area, compress the page
   * leaving the desired record at the end of the free area.
   *
   * If the record is at the end and there is free space. Do a simple
   * compaction
   */
  if (spage_is_record_located_at_end (page_header_p, slot_p)
      && space <= page_header_p->cont_free)
    {
      old_waste += slot_p->record_length;
      spage_add_contiguous_free_space (page_p, old_waste);
      space = record_descriptor_p->length + new_waste;
      old_waste = 0;
    }
  else if (record_descriptor_p->length + new_waste > page_header_p->cont_free)
    {
      /*
       * Full compaction: eliminate record from compaction (like a quick
       * delete). Compaction always finish with the correct amount of free
       * space.
       */
      old_offset = slot_p->offset_to_record;
      slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      page_header_p->total_free += (old_waste + slot_p->record_length);
      page_header_p->num_records--;

      if (spage_compact (page_p) != NO_ERROR)
	{
	  slot_p->offset_to_record = old_offset;
	  page_header_p->total_free -= (old_waste + slot_p->record_length);
	  page_header_p->num_records++;

	  spage_verify_header (page_p);
	  return SP_ERROR;
	}

      page_header_p->num_records++;
      space = record_descriptor_p->length + new_waste;
    }

  /* Now update the record */
  spage_set_slot (slot_p, page_header_p->offset_to_free_area,
		  record_descriptor_p->length, slot_p->record_type);
  if (page_header_p->offset_to_free_area + record_descriptor_p->length
      > DB_PAGESIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      assert_release (false);
      return SP_ERROR;
    }
  memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	  record_descriptor_p->data, record_descriptor_p->length);

  /* Adjust the header */
  page_header_p->total_free -= space;
  page_header_p->cont_free -= (record_descriptor_p->length + new_waste);
  page_header_p->offset_to_free_area += (record_descriptor_p->length
					 + new_waste);

  spage_verify_header (page_p);

  return SP_SUCCESS;
}

/*
 * spage_update () - Update the record located at the given slot with the data
 *                described by the given record descriptor
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to update
 *   record_descriptor_p(in): Pointer to a record descriptor
 */
int
spage_update (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
	      const RECDES * record_descriptor_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int new_waste, old_waste;
  int space;
  int total_free_save;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  total_free_save = page_header_p->total_free;

  status = spage_check_updatable (thread_p, page_p, slot_id,
				  record_descriptor_p->length, &slot_p,
				  &space, &old_waste, &new_waste);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* If the new representation fits onto the area of the old representation,
     execute the update in place */

  if (record_descriptor_p->length <= (int) slot_p->record_length)
    {
      status = spage_update_record_in_place (page_p, page_header_p, slot_p,
					     record_descriptor_p, space);
    }
  else
    {
      status = spage_update_record_after_compact (page_p, page_header_p,
						  slot_p, record_descriptor_p,
						  space, old_waste,
						  new_waste);
    }

  if (status != SP_SUCCESS)
    {
      return status;
    }

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save)
      != NO_ERROR)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }
  SPAGE_VERIFY_HEADER (page_header_p);

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_is_updatable () - Find if there is enough area to update the record with
 *                   the given data
 *   return: true if there is enough area to update, or false
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to update
 *   record_descriptor_length(in): record descriptor length
 */
bool
spage_is_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		    PGSLOTID slot_id, int record_descriptor_length)
{
  if (spage_check_updatable (thread_p, page_p, slot_id,
			     record_descriptor_length, NULL, NULL, NULL, NULL)
      != SP_SUCCESS)
    {
      return false;
    }

  return true;
}

/*
* spage_is_mvcc_updatable () - check whether is enough free area to update
 *                   a record in MVCC
 *   return: true if there is enough area to update in MVCC, or false
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to logical delete
 *   mvcc_delete_record_length(in): the length of delete record
 *   mvcc_insert_record_length(in): the length of insert record 
 */
bool
spage_is_mvcc_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			 PGSLOTID slot_id, int delete_record_length,
			 int insert_record_length)
{
  if (spage_check_mvcc_updatable (thread_p, page_p, slot_id,
				  delete_record_length, insert_record_length)
      != SP_SUCCESS)
    {
      return false;
    }

  return true;
}

/*
 * spage_update_record_type () - Update the type of the record located at the
 *                               given slot
 *   return: void
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to update
 *   type(in): record type
 */
void
spage_update_record_type (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			  PGSLOTID slot_id, INT16 record_type)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  assert (REC_UNKNOWN <= record_type
	  && record_type <= REC_4BIT_USED_TYPE_MAX);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return;
    }

  slot_p->record_type = record_type;
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
}

/*
 * spage_reclaim () - Reclaim all slots of marked deleted slots of anchored with
 *                 don't reuse slots pages
 *   return: true if anything was reclaimed and false if nothing was reclaimed
 *
 *   page_p(in): Pointer to slotted page
 *
 * Note: This function is intended to be run when there are no more references
 *       of the marked deleted slots, and thus they can be reused.
 */
bool
spage_reclaim (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p, *first_slot_p;
  PGSLOTID slot_id;
  bool is_reclaim = false;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (page_header_p->num_slots > 0)
    {
      first_slot_p = spage_find_slot (page_p, page_header_p, 0, false);

      /* Start backwards so we can reuse space easily */
      for (slot_id = page_header_p->num_slots - 1; slot_id >= 0; slot_id--)
	{
	  slot_p = first_slot_p - slot_id;
	  if (slot_p->offset_to_record == SPAGE_EMPTY_OFFSET
	      && (slot_p->record_type == REC_MARKDELETED
		  || slot_p->record_type == REC_DELETED_WILL_REUSE))
	    {
	      assert (page_header_p->anchor_type ==
		      ANCHORED_DONT_REUSE_SLOTS);

	      if ((slot_id + 1) == page_header_p->num_slots)
		{
		  spage_reduce_a_slot (page_p);
		}
	      else
		{
		  slot_p->record_type = REC_DELETED_WILL_REUSE;
		}
	      is_reclaim = true;
	    }
	}
    }

  if (is_reclaim == true)
    {
      assert (page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS);

      if (page_header_p->num_slots == 0)
	{
	  /* Initialize the page to avoid future compactions */
	  spage_initialize (thread_p, page_p, page_header_p->anchor_type,
			    page_header_p->alignment,
			    page_header_p->is_saving);
	}
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

  SPAGE_VERIFY_HEADER (page_header_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return is_reclaim;
}

/*
 * spage_split () - Split the record stored at given slot_id at offset location
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to update
 *   offset(in): Location of split must be > 0 and smaller than record length
 *   new_slotid(out): new slot id
 */
int
spage_split (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
	     int offset, PGSLOTID * out_new_slot_id_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  SPAGE_SLOT *new_slot_p;
  char *copyarea;
  int remain_length;
  int total_free_save;
  int old_waste, remain_waste, new_waste;
  int space;
  int status;

  assert (page_p != NULL);
  assert (out_new_slot_id_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if (offset < 0 || offset > (int) slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_SPLIT_WRONG_OFFSET, 3, offset, slot_p->record_length,
	      slot_id);
      /* Offset is wrong */
      *out_new_slot_id_p = NULL_SLOTID;
      return SP_ERROR;
    }


  status = spage_find_empty_slot (thread_p, page_p, 0, slot_p->record_type,
				  &new_slot_p, &new_waste, out_new_slot_id_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* Do we need to worry about wasted space */
  remain_length = slot_p->record_length - offset;
  remain_waste = DB_WASTED_ALIGN (offset, page_header_p->alignment);
  if (remain_waste == 0)
    {
      /* We are not wasting any space due to alignments. We do not need
         to move any data, just modify the length and offset of the slots. */
      new_slot_p->offset_to_record = slot_p->offset_to_record + offset;
      new_slot_p->record_length = remain_length;
      slot_p->record_length = offset;
    }
  else
    {
      /*
       * We must move the second portion of the record to an offset that
       * is aligned according to the page alignment method. In fact we
       * can moved it to the location (offset) returned by sp_empty, if
       * there is enough contiguous space, otherwise, we need to compact
       * the page.
       */
      total_free_save = page_header_p->total_free;
      old_waste = DB_WASTED_ALIGN (slot_p->record_length,
				   page_header_p->alignment);
      remain_waste = DB_WASTED_ALIGN (offset, page_header_p->alignment);
      new_waste = DB_WASTED_ALIGN (remain_length, page_header_p->alignment);
      /*
       * Difference in space:
       *   newlength1 + new_waste1  :   sptr->record_length - offset + new_waste1
       * + newlength2 + new_waste2  : + offset + new_waste2
       * - oldlength  - oldwaste    : - sptr->record_length - old_waste
       * --------------------------------------------------------------------
       * new_waste1 + newwaste2 - oldwaste
       */
      space = remain_waste + new_waste - old_waste;
      if (space > 0
	  && spage_has_enough_total_space (thread_p, page_p, page_header_p,
					   space) == false)
	{
	  (void) spage_delete_for_recovery (thread_p, page_p,
					    *out_new_slot_id_p);
	  *out_new_slot_id_p = NULL_SLOTID;
	  return SP_DOESNT_FIT;
	}

      if (remain_length > page_header_p->cont_free)
	{
	  /*
	   * Need to compact the page, before the second part is moved
	   * to an alignment position.
	   *
	   * Save the second portion
	   */
	  copyarea = (char *) malloc (remain_length);
	  if (copyarea == NULL)
	    {
	      (void) spage_delete_for_recovery (thread_p, page_p,
						*out_new_slot_id_p);
	      *out_new_slot_id_p = NULL_SLOTID;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return SP_ERROR;
	    }

	  if (slot_p->offset_to_record + offset + remain_length >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy (copyarea,
		  (char *) page_p + slot_p->offset_to_record + offset,
		  remain_length);

	  /* For now indicate that it has an empty slot */
	  new_slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
	  new_slot_p->record_length = remain_length;
	  /* New length for first part of split. */
	  slot_p->record_length = offset;

	  /* Adjust some of the space for the compaction, then return the
	     space back. That is, second part is gone for now. */
	  page_header_p->total_free += (space + remain_length + new_waste);
	  page_header_p->num_records--;

	  if (spage_compact (page_p) != NO_ERROR)
	    {
	      slot_p->record_length += remain_length;
	      page_header_p->total_free -=
		(space + remain_length + new_waste);
	      (void) spage_delete_for_recovery (thread_p, page_p,
						*out_new_slot_id_p);
	      *out_new_slot_id_p = NULL_SLOTID;

	      free_and_init (copyarea);
	      spage_verify_header (page_p);
	      return SP_ERROR;
	    }
	  page_header_p->num_records++;

	  /* Now update the record */
	  new_slot_p->offset_to_record = page_header_p->offset_to_free_area;
	  new_slot_p->record_length = remain_length;

	  if (new_slot_p->offset_to_record + remain_length >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy (((char *) page_p + new_slot_p->offset_to_record), copyarea,
		  remain_length);
	  /* Adjust the header */
	  spage_reduce_contiguous_free_space (page_p,
					      remain_length + new_waste);
	  free_and_init (copyarea);
	}
      else
	{
	  /* We can just move the second part to the end of the page */
	  if (new_slot_p->offset_to_record + remain_length >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy (((char *) page_p + new_slot_p->offset_to_record),
		  (char *) page_p + slot_p->offset_to_record + offset,
		  remain_length);
	  new_slot_p->record_length = remain_length;
	  slot_p->record_length = offset;
	  /* Adjust the header */
	  spage_reduce_contiguous_free_space (page_p, space);
	}

      if (page_header_p->is_saving
	  && spage_save_space (thread_p, page_header_p, page_p,
			       page_header_p->total_free - total_free_save) !=
	  NO_ERROR)
	{
	  assert (false);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return SP_ERROR;
	}
    }

  spage_verify_header (page_p);

  /* set page dirty */
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_take_out () - REMOVE A PORTION OF A RECORD
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of desired record
 *   takeout_offset(in): Location where to remove a portion of the data
 *   takeout_length(in): Length of data to remove starting at takeout_offset
 */
int
spage_take_out (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
		int takeout_offset, int takeout_length)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int new_waste, old_waste;
  int total_free_save;
  int mayshift_left;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((takeout_offset + takeout_length) > (int) slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_TAKEOUT_WRONG_OFFSET, 4,
	      takeout_offset, takeout_length, slot_p->record_length, slot_id);
      return SP_ERROR;
    }

  total_free_save = page_header_p->total_free;
  old_waste =
    DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment);
  new_waste =
    DB_WASTED_ALIGN (slot_p->record_length - takeout_offset,
		     page_header_p->alignment);
  /*
   * How to shift: The left portion to the right or
   *               the right portion to the left ?
   *
   * We shift the left portion to the right only when the left portion is
   * smaller than the right portion and we will end up with the record
   * aligned without moving the right portion (is left aligned when we
   * shifted "takeout_length" ?).
   */
  /* Check alignment of second part */
  mayshift_left = DB_WASTED_ALIGN (slot_p->offset_to_record + takeout_length,
				   page_header_p->alignment);
  if (mayshift_left == 0
      && (takeout_offset <
	  ((int) slot_p->record_length - takeout_offset - takeout_length)))
    {
      /*
       * Move left part to right since we can archive alignment by moving left
       * part "takeout_length" spaces and the left part is smaller than right
       * part.
       */
      if (takeout_offset == 0)
	{
	  /* Don't need to move anything we are chopping the record from the
	     left */
	  ;
	}
      else
	{
	  if (slot_p->offset_to_record + takeout_length +
	      takeout_offset > (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memmove ((char *) page_p + slot_p->offset_to_record +
		   takeout_length, (char *) page_p + slot_p->offset_to_record,
		   takeout_offset);
	}
      slot_p->offset_to_record += takeout_length;
    }
  else
    {
      /* Move right part "takeout_length" positions to the left */
      if (((int) slot_p->record_length - takeout_offset - takeout_length) > 0)
	{
	  /* We are removing a portion of the record from the middle. That is, we
	     remove a portion of the record and glue the remaining two pieces */
	  if (slot_p->offset_to_record + takeout_offset +
	      (slot_p->record_length - takeout_offset - takeout_length)
	      > (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memmove ((char *) page_p + slot_p->offset_to_record +
		   takeout_offset,
		   (char *) page_p + slot_p->offset_to_record +
		   takeout_offset + takeout_length,
		   slot_p->record_length - takeout_offset - takeout_length);
	}
      else
	{
	  /* We are truncating the record */
	  ;
	}

      if (spage_is_record_located_at_end (page_header_p, slot_p))
	{
	  /*
	   * The record is located just before the contiguous free area. That is,
	   * at the end of the page.
	   *
	   * Do a simple compaction
	   */
	  page_header_p->cont_free += takeout_length + old_waste - new_waste;
	  page_header_p->offset_to_free_area -=
	    takeout_length + old_waste - new_waste;
	}
    }

  slot_p->record_length -= takeout_length;
  page_header_p->total_free += (takeout_length + old_waste - new_waste);

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  spage_verify_header (page_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_append () - Append the data described by the given record descriptor
 *                into the record located at the given slot
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of desired record
 *   record_descriptor_p(in): Pointer to a record descriptor
 */
int
spage_append (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
	      const RECDES * record_descriptor_p)
{
  return spage_put_helper (thread_p, page_p, slot_id, 0, record_descriptor_p,
			   true);
}

/*
 * spage_put () - Add the data described by the given record descriptor within
 *               the given offset of the record located at the given slot
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of desired record
 *   offset(in): Location where to add the portion of the data
 *   record_descriptor_p(in): Pointer to a record descriptor
 */
int
spage_put (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
	   int offset, const RECDES * record_descriptor_p)
{
  return spage_put_helper (thread_p, page_p, slot_id, offset,
			   record_descriptor_p, false);
}

/*
 * spage_put_helper () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of desired record
 *   offset(in):
 *   record_descriptor_p(in):
 *   is_append(in):
 */
static int
spage_put_helper (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
		  int offset, const RECDES * record_descriptor_p,
		  bool is_append)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int old_offset;
  int new_waste, old_waste;
  int space;
  int total_free_save;
  char *copyarea;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      return SP_ERROR;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((record_descriptor_p->length + (int) slot_p->record_length) >
      spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  total_free_save = page_header_p->total_free;
  old_waste =
    DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment);
  new_waste =
    DB_WASTED_ALIGN (record_descriptor_p->length + slot_p->record_length,
		     page_header_p->alignment);
  space = record_descriptor_p->length + new_waste - old_waste;
  if (space > 0
      && spage_has_enough_total_space (thread_p, page_p, page_header_p,
				       space) == false)
    {
      return SP_DOESNT_FIT;
    }

  if (spage_is_record_located_at_end (page_header_p, slot_p)
      && space <= page_header_p->cont_free)
    {
      /*
       * The record is at the end of the page (just before contiguous free
       * space), and there is space on the contiguous free are to put in the
       * new data.
       */

      spage_add_contiguous_free_space (page_p, old_waste);
      if (!is_append)
	{
	  /* Move anything after offset, so we can insert the desired data */
	  if (slot_p->offset_to_record + offset +
	      record_descriptor_p->length + (slot_p->record_length -
					     offset) >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memmove ((char *) page_p + slot_p->offset_to_record + offset +
		   record_descriptor_p->length,
		   (char *) page_p + slot_p->offset_to_record + offset,
		   slot_p->record_length - offset);
	}
    }
  else if ((int) record_descriptor_p->length + (int) slot_p->record_length <=
	   page_header_p->cont_free)
    {
      /* Move the old data to the end and remove wasted space from the old
         data, so we can append at the right place. */
      if (is_append)
	{
	  if (page_header_p->offset_to_free_area + slot_p->record_length
	      > (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  (char *) page_p + slot_p->offset_to_record,
		  slot_p->record_length);
	}
      else
	{
	  if (page_header_p->offset_to_free_area + offset > DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  (char *) page_p + slot_p->offset_to_record, offset);
	  if (page_header_p->offset_to_free_area + offset +
	      record_descriptor_p->length + (slot_p->record_length -
					     offset) >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return (SP_ERROR);
	    }
	  memmove ((char *) page_p + page_header_p->offset_to_free_area +
		   offset + record_descriptor_p->length,
		   (char *) page_p + slot_p->offset_to_record + offset,
		   slot_p->record_length - offset);
	}
      slot_p->offset_to_record = page_header_p->offset_to_free_area;
      page_header_p->offset_to_free_area += slot_p->record_length;	/* Don't increase waste here */
      page_header_p->cont_free =
	page_header_p->cont_free - slot_p->record_length + old_waste;
      if (is_append)
	{
	  page_header_p->total_free =
	    page_header_p->total_free - slot_p->record_length + old_waste;
	}
      else
	{
	  page_header_p->total_free += old_waste;
	}

      SPAGE_VERIFY_HEADER (page_header_p);
    }
  else
    {
      /*
       * We need to compress the data leaving the desired record at the end.
       * Eliminate the old data from compaction (like a quick delete), by
       * saving the data in memory. Then, after the compaction we place the
       * data on the contiguous free space. We remove the old_waste space
       * since we are appending.
       */

      copyarea = (char *) malloc (slot_p->record_length);
      if (copyarea == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return SP_ERROR;
	}

      if (slot_p->offset_to_record + slot_p->record_length >
	  (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (copyarea, (char *) page_p + slot_p->offset_to_record,
	      slot_p->record_length);
      /* For now indicate that it has an empty slot */
      old_offset = slot_p->offset_to_record;
      slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      page_header_p->total_free += (slot_p->record_length + old_waste);
      page_header_p->num_records--;

      if (spage_compact (page_p) != NO_ERROR)
	{
	  slot_p->offset_to_record = old_offset;
	  page_header_p->total_free -= (old_waste + slot_p->record_length);
	  page_header_p->num_records++;
	  free_and_init (copyarea);

	  spage_verify_header (page_p);
	  return SP_ERROR;
	}
      page_header_p->num_records++;
      /* Move the old data to the end */
      if (is_append)
	{
	  if (page_header_p->offset_to_free_area +
	      slot_p->record_length > (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  copyarea, slot_p->record_length);
	}
      else
	{
	  if (page_header_p->offset_to_free_area + offset > DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  copyarea, offset);
	  if (page_header_p->offset_to_free_area + offset +
	      record_descriptor_p->length + (slot_p->record_length -
					     offset) >
	      (unsigned int) DB_PAGESIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert_release (false);
	      return SP_ERROR;
	    }
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area +
		  offset + record_descriptor_p->length, copyarea + offset,
		  slot_p->record_length - offset);
	}
      free_and_init (copyarea);
      slot_p->offset_to_record = page_header_p->offset_to_free_area;
      spage_reduce_contiguous_free_space (page_p, slot_p->record_length);
    }

  /* Now perform the put operation. */
  if (is_append)
    {
      if (page_header_p->offset_to_free_area +
	  record_descriptor_p->length > DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  else
    {
      if (slot_p->offset_to_record + offset +
	  record_descriptor_p->length > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (((char *) page_p + slot_p->offset_to_record + offset),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  slot_p->record_length += record_descriptor_p->length;
  /* Note that we have already eliminated the old waste, so do not take it
     in consideration right now. */
  spage_reduce_contiguous_free_space (page_p,
				      record_descriptor_p->length +
				      new_waste);
  if (page_header_p->is_saving
      && (spage_save_space (thread_p, page_header_p, page_p,
			    page_header_p->total_free - total_free_save) !=
	  NO_ERROR))
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  spage_verify_header (page_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_overwrite () - Overwrite a portion of the record stored at given slot_id
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record to overwrite
 *   overwrite_offset(in): Offset on the record to start the overwrite process
 *   record_descriptor_p(in): New replacement data
 *
 * Note: overwrite_offset + record_descriptor_p->length must be <= length of record stored
 *       on slot.
 *       If this is not the case, you must use a combination of overwrite and
 *       append.
 */
int
spage_overwrite (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
		 int overwrite_offset, const RECDES * record_descriptor_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length < 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((overwrite_offset + record_descriptor_p->length) >
      (int) slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_OVERWRITE_WRONG_OFFSET, 4,
	      overwrite_offset, record_descriptor_p->length,
	      slot_p->record_length, slot_id);
      return SP_ERROR;
    }

  if (slot_p->offset_to_record + overwrite_offset +
      record_descriptor_p->length > (unsigned int) DB_PAGESIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      assert_release (false);
      return SP_ERROR;
    }
  memcpy (((char *) page_p + slot_p->offset_to_record + overwrite_offset),
	  record_descriptor_p->data, record_descriptor_p->length);

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_merge () - Merge the record of the second slot onto the record of the
 *               first slot
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slotid1(in): Slot identifier of first slot
 *   slotid2(in): Slot identifier of second slot
 *
 * Note: Then the second slot is removed.
 */
int
spage_merge (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID first_slot_id,
	     PGSLOTID second_slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *first_slot_p;
  SPAGE_SLOT *second_slot_p;
  int first_old_offset, second_old_offset;
  int new_waste, first_old_waste, second_old_waste;
  int total_free_save;
  char *copyarea;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  /* Find the slots */
  first_slot_p = spage_find_slot (page_p, page_header_p, first_slot_id, true);
  if (first_slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      first_slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  second_slot_p = spage_find_slot (page_p, page_header_p, second_slot_id,
				   true);
  if (second_slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      second_slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  total_free_save = page_header_p->total_free;
  first_old_waste = DB_WASTED_ALIGN (first_slot_p->record_length,
				     page_header_p->alignment);
  second_old_waste = DB_WASTED_ALIGN (second_slot_p->record_length,
				      page_header_p->alignment);
  new_waste =
    DB_WASTED_ALIGN (first_slot_p->record_length +
		     second_slot_p->record_length, page_header_p->alignment);
  if (spage_is_record_located_at_end (page_header_p, first_slot_p)
      && (int) second_slot_p->record_length <= page_header_p->cont_free)
    {
      /*
       * The first record is at the end of the page (just before contiguous free
       * space), and there is space on the contiguous free area to append the
       * second record.
       *
       * Remove the wasted space from the free spaces, so we can append at
       * the right place.
       */

      spage_add_contiguous_free_space (page_p, first_old_waste);
      first_old_waste = 0;
    }
  else if ((int) first_slot_p->record_length +
	   (int) second_slot_p->record_length <= page_header_p->cont_free)
    {
      /* Move the first data to the end and remove wasted space from the first
         record, so we can append at the right place. */
      if (page_header_p->offset_to_free_area +
	  first_slot_p->record_length > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy ((char *) page_p + page_header_p->offset_to_free_area,
	      (char *) page_p + first_slot_p->offset_to_record,
	      first_slot_p->record_length);
      first_slot_p->offset_to_record = page_header_p->offset_to_free_area;
      page_header_p->offset_to_free_area += first_slot_p->record_length;

      /* Don't increase waste here */
      page_header_p->total_free -=
	(first_slot_p->record_length - first_old_waste);
      page_header_p->cont_free -=
	(first_slot_p->record_length - first_old_waste);
      first_old_waste = 0;

      SPAGE_VERIFY_HEADER (page_header_p);
    }
  else
    {
      /*
       * We need to compress the page leaving the desired record at end.
       * We eliminate the data of both records (like quick deletes), by
       * saving their data in memory. Then, after the compaction we restore
       * the data on the contiguous space.
       */

      copyarea = (char *)
	malloc (first_slot_p->record_length + second_slot_p->record_length);
      if (copyarea == NULL)
	{
	  return SP_ERROR;
	}

      if (first_slot_p->offset_to_record + first_slot_p->record_length
	  > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (copyarea, (char *) page_p + first_slot_p->offset_to_record,
	      first_slot_p->record_length);
      if (second_slot_p->offset_to_record +
	  second_slot_p->record_length > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (copyarea + first_slot_p->record_length,
	      (char *) page_p + second_slot_p->offset_to_record,
	      second_slot_p->record_length);

      /* Now indicate empty slots. */
      first_old_offset = first_slot_p->offset_to_record;
      second_old_offset = second_slot_p->offset_to_record;
      first_slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      second_slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
      page_header_p->total_free +=
	(first_slot_p->record_length + second_slot_p->record_length +
	 first_old_waste + second_old_waste);
      page_header_p->num_records -= 2;

      if (spage_compact (page_p) != NO_ERROR)
	{
	  first_slot_p->offset_to_record = first_old_offset;
	  second_slot_p->offset_to_record = second_old_offset;
	  page_header_p->total_free -=
	    (first_slot_p->record_length + second_slot_p->record_length +
	     first_old_waste + second_old_waste);
	  page_header_p->num_records += 2;
	  free_and_init (copyarea);

	  spage_verify_header (page_p);
	  return SP_ERROR;
	}
      page_header_p->num_records += 2;

      /* Move the old data to the end */
      if (page_header_p->offset_to_free_area +
	  (first_slot_p->record_length + second_slot_p->record_length)
	  > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy ((char *) page_p + page_header_p->offset_to_free_area, copyarea,
	      first_slot_p->record_length + second_slot_p->record_length);
      free_and_init (copyarea);

      first_slot_p->offset_to_record = page_header_p->offset_to_free_area;
      first_slot_p->record_length += second_slot_p->record_length;
      second_slot_p->record_length = 0;
      second_slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;

      spage_reduce_contiguous_free_space (page_p,
					  first_slot_p->record_length);
      first_old_waste = 0;
      second_old_waste = 0;
    }

  /* Now perform the append operation if needed */
  if (second_slot_p->record_length != 0)
    {
      if (page_header_p->offset_to_free_area +
	  second_slot_p->record_length > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	      (char *) page_p + second_slot_p->offset_to_record,
	      second_slot_p->record_length);
      first_slot_p->record_length += second_slot_p->record_length;
      second_slot_p->record_length = 0;
    }

  /* Note that we have already eliminated the old waste, so do not take it
     in consideration right now. */

  spage_reduce_contiguous_free_space (page_p,
				      new_waste - first_old_waste -
				      second_old_waste);
  (void) spage_delete (thread_p, page_p, second_slot_id);

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  spage_verify_header (page_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif /* SPAGE_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_search_record () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   out_slot_id_p(in/out): Slot identifier of desired record
 *   record_descriptor_p(in/out):
 *   is_peeking(in):
 *   direction(in):
 */
static SCAN_CODE
spage_search_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		     RECDES * record_descriptor_p, int is_peeking,
		     int direction, bool skip_empty)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  PGSLOTID slot_id;

  assert (page_p != NULL);
  assert (out_slot_id_p != NULL);
  assert (record_descriptor_p != NULL);

  slot_id = *out_slot_id_p;
  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (slot_id < 0 || slot_id > page_header_p->num_slots)
    {
      if (direction == SPAGE_SEARCH_NEXT)
	{
	  slot_id = 0;
	}
      else
	{
	  slot_id = page_header_p->num_slots - 1;
	}
    }
  else
    {
      slot_id += direction;
    }

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);
  while (slot_id >= 0 && slot_id < page_header_p->num_slots
	 && slot_p->offset_to_record == SPAGE_EMPTY_OFFSET)
    {
      if (skip_empty)
	{
	  slot_id += direction;
	  slot_p -= direction;
	}
      else
	{
	  record_descriptor_p->data = NULL;
	  *out_slot_id_p = slot_id;
	  return S_SUCCESS;
	}
    }

  SPAGE_VERIFY_HEADER (page_header_p);

  if (slot_id >= 0 && slot_id < page_header_p->num_slots)
    {
      *out_slot_id_p = slot_id;
      return spage_get_record_data (page_p, slot_p, record_descriptor_p,
				    is_peeking);
    }
  else
    {
      /* There is not anymore records */
      *out_slot_id_p = -1;
      record_descriptor_p->length = 0;
      return S_END;
    }
}

/*
 * spage_next_record () - Get next record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *   page_p(in): Pointer to slotted page
 *   out_slot_id_p(in/out): Slot identifier of current record
 *   record_descriptor_p(out): Pointer to a record descriptor
 *   is_peeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 *
 * Note: When is_peeking is PEEK, the next available record is peeked onto the
 *       page. The address of the record descriptor is set to the portion of
 *       the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted.
 *
 *       When is_peeking is COPY, the next available record is read
 *       onto the area pointed by the record descriptor. If the record does not
 *       fit in such an area, the length of the record is returned as a
 *       negative value in record_descriptor_p->length and an error is
 *       indicated in the return value.
 *
 *       If the current value of out_slot_id_p is negative, the first record on the
 *       page is retrieved.
 */
SCAN_CODE
spage_next_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		   RECDES * record_descriptor_p, int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_NEXT, true);
}

/*
 * spage_previous_record () - Get previous record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *   page_p(in): Pointer to slotted page
 *   slot_id(out): Slot identifier of current record
 *   record_descriptor_p(out): Pointer to a record descriptor
 *   is_peeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 *
 * Note: When is_peeking is PEEK, the previous available record is peeked onto
 *       the page. The address of the record descriptor is set to the portion
 *       of the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted.
 *
 *       When is_peeking is COPY, the previous available record is
 *       read onto the area pointed by the record descriptor. If the record
 *       does not fit in such an area, the length of the record is returned
 *       as a negative value in record_descriptor_p->length and an error is
 *       indicated in the return value.
 *
 *       If the current value of slot_id is negative, the first record on the
 *       page is retrieved.
 */
SCAN_CODE
spage_previous_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		       RECDES * record_descriptor_p, int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_PREV, true);
}

/*
 * spage_get_record () - Get specified record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of current record
 *   record_descriptor_p(out): Pointer to a record descriptor
 *   is_peeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 *
 * Note: When is_peeking is PEEK, the desired available record is peeked onto
 *       the page. The address of the record descriptor is set to the portion
 *       of the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted.
 *
 *       When is_peeking is COPY, the desired available record is
 *       read onto the area pointed by the record descriptor. If the record
 *       does not fit in such an area, the length of the record is returned
 *       as a negative value in record_descriptor_p->length and an error is
 *       indicated in the return value.
 */
SCAN_CODE
spage_get_record (PAGE_PTR page_p, PGSLOTID slot_id,
		  RECDES * record_descriptor_p, int is_peeking)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *sptr;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  assert (record_descriptor_p != NULL);

  sptr = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (sptr == NULL)
    {
      record_descriptor_p->length = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return S_DOESNT_EXIST;
    }

  return spage_get_record_data (page_p, sptr, record_descriptor_p,
				is_peeking);
}

/*
 * spage_get_record_data () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   slot_p(in/out): Pointer to slotted page pointer array
 *   record_descriptor_p(in/out):
 *   is_peeking(in):
 */
static SCAN_CODE
spage_get_record_data (PAGE_PTR page_p, SPAGE_SLOT * slot_p,
		       RECDES * record_descriptor_p, int is_peeking)
{
  assert (page_p != NULL);
  assert (slot_p != NULL);
  assert (record_descriptor_p != NULL);

  /*
   * If peeking, the address of the data in the descriptor is set to the
   * address of the record in the buffer. Otherwise, the record is copied
   * onto the area specified by the descriptor
   */
  if (is_peeking == PEEK)
    {
      record_descriptor_p->area_size = -1;
      record_descriptor_p->data = (char *) page_p + slot_p->offset_to_record;
    }
  else
    {
      /* copy the record */
      if (record_descriptor_p->area_size < 0
	  || record_descriptor_p->area_size < (int) slot_p->record_length)
	{
	  /*
	   * DOES NOT FIT
	   * Give a hint to the user of the needed length. Hint is given as a
	   * negative value
	   */
	  /* do not use unary minus because slot_p->record_length is unsigned */
	  record_descriptor_p->length = (slot_p->record_length * -1);
	  return S_DOESNT_FIT;
	}

      if (slot_p->offset_to_record + slot_p->record_length >
	  (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (record_descriptor_p->data,
	      (char *) page_p + slot_p->offset_to_record,
	      slot_p->record_length);
    }

  record_descriptor_p->length = slot_p->record_length;
  record_descriptor_p->type = slot_p->record_type;

  return S_SUCCESS;
}

/*
 * spage_get_record_mvcc () - Get only mvcc of specified record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *   page_p(in): Pointer to slotted page
 *   slotid(in): Slot identifier of current record
 *   recdes(out): Pointer to a record descriptor
 *   ispeeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 */
SCAN_CODE
spage_get_record_mvcc (PAGE_PTR page_p, PGSLOTID slot_id,
		       RECDES * record_descriptor_p, int is_peeking)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *sptr;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  assert (record_descriptor_p != NULL);

  sptr = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (sptr == NULL)
    {
      record_descriptor_p->length = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return S_DOESNT_EXIST;
    }

  return spage_get_record_mvcc_data (page_p, sptr, record_descriptor_p,
				     is_peeking);
}

/*
 * spage_get_record_mvcc_data () - Find the mvcc data of the record associated with
 *                              the given slot on the given page
 *   return: scan code
 *   page_p(in): Pointer to slotted page
 *   slot_p(in): Pointer to page slot
 *   record_descriptor(out): Pointer to a record descriptor
 *  
 */
static SCAN_CODE
spage_get_record_mvcc_data (PAGE_PTR page_p, SPAGE_SLOT * slot_p,
			    RECDES * record_descriptor_p, int is_peeking)
{
  int header_size;
  assert (page_p != NULL);
  assert (slot_p != NULL);
  assert (record_descriptor_p != NULL);

  header_size = OR_HEADER_SIZE ((char *) page_p + slot_p->offset_to_record);

  /*
   * If peeking, the address of the data in the descriptor is set to the
   * address of the record in the buffer. Otherwise, the record is copied
   * onto the area specified by the descriptor
   */
  if (is_peeking == PEEK)
    {
      record_descriptor_p->area_size = -1;
      record_descriptor_p->data = (char *) page_p + slot_p->offset_to_record;
    }
  else
    {
      if (record_descriptor_p->area_size < 0
	  || record_descriptor_p->area_size < header_size)
	{
	  /*
	   * DOES NOT FIT
	   * Give a hint to the user of the needed length. Hint is given as a
	   * negative value
	   */
	  /* do not use unary minus because slot_p->record_length is unsigned */
	  record_descriptor_p->length = -header_size;
	  return S_DOESNT_FIT;
	}

      if (slot_p->offset_to_record + header_size > (unsigned int) DB_PAGESIZE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert_release (false);
	  return SP_ERROR;
	}
      memcpy (record_descriptor_p->data,
	      (char *) page_p + slot_p->offset_to_record, header_size);
    }

  record_descriptor_p->length = header_size;
  record_descriptor_p->type = slot_p->record_type;

  return S_SUCCESS;
}

/*
 * spage_get_record_length () - Find the length of the record associated with
 *                              the given slot on the given page
 *   return: Length of the record or -1 in case of error
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record
 */
int
spage_get_record_length (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return -1;
    }

  return slot_p->record_length;
}

/*
 * spage_get_space_for_record () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record
 */
int
spage_get_space_for_record (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return -1;
    }

  return (slot_p->record_length
	  + DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment)
	  + spage_slot_size ());
}

/*
 * spage_get_record_type () - Find the type of the record associated with the given slot
 *                 on the given page
 *   return: record type, or -1 if the given slot is invalid
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record
 */
INT16
spage_get_record_type (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL
      || slot_p->record_type == REC_MARKDELETED
      || slot_p->record_type == REC_DELETED_WILL_REUSE)
    {
      return REC_UNKNOWN;
    }

  return slot_p->record_type;
}

/*
 * spage_mark_deleted_slot_as_reusable () - Marks the slot of a previously
 *                                          deleted record as reusable
 *   return: SP_ERROR or SP_SUCCESS
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier
 */
int
spage_mark_deleted_slot_as_reusable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				     PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p = NULL;
  SPAGE_SLOT *slot_p = NULL;
  SPAGE_SLOT *first_slot_p = NULL;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (0 > slot_id || slot_id >= page_header_p->num_slots)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);

  if (slot_p
      && slot_p->offset_to_record == SPAGE_EMPTY_OFFSET
      && (slot_p->record_type == REC_MARKDELETED
	  || slot_p->record_type == REC_DELETED_WILL_REUSE))
    {
      ;				/* OK */
    }
  else
    {
      /*
       * If the function is called in the scenarios it was designed for, this
       * should not happen. The slot to be set as reusable should always
       * exist and should not point to a valid record.
       */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_BAD_INSERTION_SLOT, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  slot_p->record_type = REC_DELETED_WILL_REUSE;

  SPAGE_VERIFY_HEADER (page_header_p);

  return SP_SUCCESS;
}

/*
 * spage_is_slot_exist () - Find if there is a valid record in given slot
 *   return: true/false
 *
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of record
 */
bool
spage_is_slot_exist (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL
      || slot_p->record_type == REC_MARKDELETED
      || slot_p->record_type == REC_DELETED_WILL_REUSE)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * spage_record_type_string () -
 *   return:
 *
 *   record_type(in):
 */
static const char *
spage_record_type_string (INT16 record_type)
{
  assert (REC_UNKNOWN <= record_type
	  && record_type <= REC_4BIT_USED_TYPE_MAX);

  switch (record_type)
    {
    case REC_HOME:
      return "HOME";
    case REC_NEWHOME:
      return "NEWHOME";
    case REC_RELOCATION:
      return "RELOCATION";
    case REC_BIGONE:
      return "BIGONE";
    case REC_MARKDELETED:
      return "MARKDELETED";
    case REC_DELETED_WILL_REUSE:
      return "DELETED_WILL_REUSE";
    case REC_ASSIGN_ADDRESS:
      return "ASSIGN_ADDRESS";
    case REC_MVCC_NEXT_VERSION:
      return "REC_MVCC_NEXT_VERSION";
    default:
      assert (false);
      return "UNKNOWN";
    }
}

/*
 * spage_anchor_flag_string () -
 *   return:
 *
 *   anchor_type(in):
 */
const char *
spage_anchor_flag_string (const INT16 anchor_type)
{
  assert (ANCHORED <= anchor_type && anchor_type <= UNANCHORED_KEEP_SEQUENCE);

  switch (anchor_type)
    {
    case ANCHORED:
      return "ANCHORED";
    case ANCHORED_DONT_REUSE_SLOTS:
      return "ANCHORED_DONT_REUSE_SLOTS";
    case UNANCHORED_ANY_SEQUENCE:
      return "UNANCHORED_ANY_SEQUENCE";
    case UNANCHORED_KEEP_SEQUENCE:
      return "UNANCHORED_KEEP_SEQUENCE";
    default:
      assert (false);
      return "UNKNOWN";
    }
}

/*
 * spage_alignment_string () -
 *   return:
 *
 *   alignment(in):
 */
const char *
spage_alignment_string (unsigned short alignment)
{
  switch (alignment)
    {
    case CHAR_ALIGNMENT:
      return "CHAR";
    case SHORT_ALIGNMENT:
      return "SHORT";
    case INT_ALIGNMENT:
      return "INT";
    case DOUBLE_ALIGNMENT:
      return "DOUBLE";
    default:
      return "UNKNOWN";
    }
}

/*
 * spage_dump_header () - Dump an slotted page header
 *   return: void
 *   page_header_p(in): Pointer to header of slotted page
 *
 * Note: This function is used for debugging purposes.
 */
static void
spage_dump_header (FILE * fp, const SPAGE_HEADER * page_header_p)
{
  char buffer[1024];

  spage_dump_header_to_string (buffer, sizeof (buffer), page_header_p);

  (void) fprintf (fp, "%s", buffer);
}

/*
 * spage_dump_header_to_string () -
 *   return: void
 *
 *   buffer(out): char buffer pointer
 *   size(in): buffer size
 *   page_header_p(in): Pointer to header of slotted page
 *
 * Note:
 */
static void
spage_dump_header_to_string (char *buffer, int size,
			     const SPAGE_HEADER * page_header_p)
{
  int n = 0;
  /* Dump header information */
  n += snprintf (buffer + n, size - n,
		 "NUM SLOTS = %d, NUM RECS = %d, TYPE OF SLOTS = %s,\n",
		 page_header_p->num_slots, page_header_p->num_records,
		 spage_anchor_flag_string (page_header_p->anchor_type));
  n += snprintf (buffer + n, size - n,
		 "ALIGNMENT-TO = %s\n",
		 spage_alignment_string (page_header_p->alignment));
  n += snprintf (buffer + n, size - n,
		 "TOTAL FREE AREA = %d, CONTIGUOUS FREE AREA = %d,"
		 " FREE SPACE OFFSET = %d, NEED UPDATE BEST HINT = %d\n",
		 page_header_p->total_free,
		 page_header_p->cont_free,
		 page_header_p->offset_to_free_area,
		 page_header_p->need_update_best_hint);
  n += snprintf (buffer + n, size - n,
		 "IS_SAVING = %d\n", page_header_p->is_saving);
}

/*
 * spage_dump_slots () - Dump the slotted page array
 *   return: void
 *
 *   slot_p(in): Pointer to slotted page pointer array
 *   num_slots(in): Number of slots
 *   alignment(in): Alignment for records
 *
 * Note: The content of the record is not dumped by this function.
 *       This function is used for debugging purposes.
 */
static void
spage_dump_slots (FILE * fp, const SPAGE_SLOT * slot_p, PGNSLOTS num_slots,
		  unsigned short alignment)
{
  int i;
  unsigned int waste;

  assert (slot_p != NULL);

  for (i = 0; i < num_slots; slot_p--, i++)
    {
      (void) fprintf (fp, "\nSlot-id = %2d, offset = %4d, type = %s",
		      i, slot_p->offset_to_record,
		      spage_record_type_string (slot_p->record_type));
      if (slot_p->offset_to_record != SPAGE_EMPTY_OFFSET)
	{
	  waste = DB_WASTED_ALIGN (slot_p->record_length, alignment);
	  (void) fprintf (fp, ", length = %4d, waste = %u",
			  slot_p->record_length, waste);
	}
      (void) fprintf (fp, "\n");
    }
}

/*
 * spage_dump_record () -
 *   return:
 *
 *   fp(in/out):
 *   page_p(in): Pointer to slotted page
 *   slot_id(in): Slot identifier of desired record
 *   slot_p(in): Pointer to slotted page pointer array
 */
static void
spage_dump_record (FILE * fp, PAGE_PTR page_p, PGSLOTID slot_id,
		   SPAGE_SLOT * slot_p)
{
  VFID *vfid;
  OID *oid;
  char *record_p;
  int i;

  if (slot_p->offset_to_record != SPAGE_EMPTY_OFFSET)
    {
      (void) fprintf (fp, "\nSlot-id = %2d\n", slot_id);
      switch (slot_p->record_type)
	{
	case REC_BIGONE:
	  vfid = (VFID *) (page_p + slot_p->offset_to_record);
	  fprintf (fp, "VFID = %d|%d\n", vfid->volid, vfid->fileid);
	  break;

	case REC_RELOCATION:
	case REC_MVCC_NEXT_VERSION:
	  oid = (OID *) (page_p + slot_p->offset_to_record);
	  fprintf (fp, "OID = %d|%d|%d\n",
		   oid->volid, oid->pageid, oid->slotid);
	  break;

	default:
	  record_p = (char *) page_p + slot_p->offset_to_record;
	  for (i = 0; i < (int) slot_p->record_length; i++)
	    {
	      (void) fprintf (fp, "%02X ", (unsigned char) (*record_p++));
	      if (i % 20 == 19)
		{
		  fputc ('\n', fp);
		}
	      else if (i % 10 == 9)
		{
		  fputc (' ', fp);
		}
	    }
	  (void) fprintf (fp, "\n");
	}
    }
  else
    {
      assert (slot_p->record_type == REC_MARKDELETED
	      || slot_p->record_type == REC_DELETED_WILL_REUSE);

      (void) fprintf (fp, "\nSlot-id = %2d has been deleted\n", slot_id);
    }
}

/*
 * spage_dump () - Dump an slotted page
 *   return: void
 *   pgptr(in): Pointer to slotted page
 *   isrecord_printed(in): If true, records are printed in ascii format,
 *                         otherwise, the records are not printed.
 *
 * Note: The records are printed only when the value of isrecord_printed is
 *       true. This function is used for debugging purposes.
 */
void
spage_dump (THREAD_ENTRY * thread_p, FILE * fp, PAGE_PTR page_p,
	    int is_record_printed)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int i;

  assert (page_p != NULL);

  (void) fprintf (fp,
		  "\n*** Dumping pageid = %d of volume = %s ***\n",
		  pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p));

  page_header_p = (SPAGE_HEADER *) page_p;
  spage_dump_header (fp, page_header_p);

  /* Dump each slot and its corresponding record */
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  spage_dump_slots (fp, slot_p, page_header_p->num_slots,
		    page_header_p->alignment);

  if (is_record_printed)
    {
      (void) fprintf (fp, "\nRecords in ascii follow ...\n");
      for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
	{
	  spage_dump_record (fp, page_p, i, slot_p);
	}
    }

  spage_dump_saved_spaces_by_other_trans (thread_p, fp,
					  pgbuf_get_vpid_ptr (page_p));
#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif
}

#if !defined(NDEBUG)
/*
 * spage_check_num_slots () - Check consistency of page. This function is used for
 *               debugging purposes
 *   return: true/false
 *   ppage_p(in): Pointer to slotted page
 */
bool
spage_check_num_slots (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int i, nrecs;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, 0, false);

  nrecs = 0;
  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record != SPAGE_EMPTY_OFFSET)
	{
	  nrecs++;
	}
    }
  assert (page_header_p->num_records == nrecs);

  return (page_header_p->num_records == nrecs) ? true : false;
}
#endif

/*
 * spage_check () - Check consistency of page. This function is used for
 *               debugging purposes
 *   return: void
 *   pgptr(in): Pointer to slotted page
 */
int
spage_check (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  char err_msg[1024];
  int used_length = 0;
  int i, nrecs;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  used_length = (sizeof (SPAGE_HEADER)
		 + sizeof (SPAGE_SLOT) * page_header_p->num_slots);

  nrecs = 0;
  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record != SPAGE_EMPTY_OFFSET)
	{
	  used_length += DB_ALIGN (slot_p->record_length,
				   page_header_p->alignment);
	  nrecs++;
	}
    }
  assert (page_header_p->num_records == nrecs);

  if (used_length + page_header_p->total_free > DB_PAGESIZE)
    {
      snprintf (err_msg, sizeof (err_msg),
		"spage_check: Inconsistent page = %d of volume = %s.\n"
		"(Used_space + tfree > DB_PAGESIZE\n (%d + %d) > %d \n "
		" %d > %d\n",
		pgbuf_get_page_id (page_p),
		pgbuf_get_volume_label (page_p), used_length,
		page_header_p->total_free, DB_PAGESIZE,
		used_length + page_header_p->total_free, DB_PAGESIZE);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
	      3, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p), err_msg);

      assert (false);
      return ER_SP_INVALID_HEADER;
    }

  if ((page_header_p->cont_free + page_header_p->offset_to_free_area +
       SSIZEOF (SPAGE_SLOT) * page_header_p->num_slots) > DB_PAGESIZE)
    {
      snprintf (err_msg, sizeof (err_msg),
		"spage_check: Inconsistent page = %d of volume = %s.\n"
		" (cfree + foffset + SIZEOF(SPAGE_SLOT) * nslots) > "
		" DB_PAGESIZE\n (%d + %d + (%d * %d)) > %d\n %d > %d\n",
		pgbuf_get_page_id (page_p),
		pgbuf_get_volume_label (page_p), page_header_p->cont_free,
		page_header_p->offset_to_free_area, sizeof (SPAGE_SLOT),
		page_header_p->num_slots, DB_PAGESIZE,
		(page_header_p->cont_free +
		 page_header_p->offset_to_free_area +
		 sizeof (SPAGE_SLOT) * page_header_p->num_slots),
		DB_PAGESIZE);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
	      3, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p), err_msg);

      assert (false);
      return ER_SP_INVALID_HEADER;
    }

  if (page_header_p->cont_free <= (int) -(page_header_p->alignment - 1))
    {
      snprintf (err_msg, sizeof (err_msg),
		"spage_check: Cfree %d is inconsistent in page = %d"
		" of volume = %s. Cannot be < -%d\n",
		page_header_p->cont_free, pgbuf_get_page_id (page_p),
		pgbuf_get_volume_label (page_p), page_header_p->alignment);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
	      3, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p), err_msg);

      assert (false);
      return ER_SP_INVALID_HEADER;
    }

  /* Update any savings, before we check for any incosistencies */
  if (page_header_p->is_saving)
    {
      int other_saved_spaces = 0;
      int total_saved =
	spage_get_saved_spaces (thread_p, page_header_p, page_p,
				&other_saved_spaces);
#if 1
      if (other_saved_spaces < 0)
#else
      if (other_saved_spaces < 0 || total_saved > page_header_p->total_free)
#endif

	{
	  snprintf (err_msg, sizeof (err_msg),
		    "spage_check: Other savings of %d is inconsistent in page = %d"
		    " of volume = %s.\n",
		    other_saved_spaces,
		    pgbuf_get_page_id (page_p),
		    pgbuf_get_volume_label (page_p));

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
		  3, pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p), err_msg);

	  assert (false);
	  return ER_SP_INVALID_HEADER;
	}

      if (total_saved < 0)
	{
	  snprintf (err_msg, sizeof (err_msg),
		    "spage_check: Total savings of %d is inconsistent in page = %d"
		    " of volume = %s. Cannot be < 0\n",
		    total_saved,
		    pgbuf_get_page_id (page_p),
		    pgbuf_get_volume_label (page_p));

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_HEADER,
		  3, pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p), err_msg);

	  assert (false);
	  return ER_SP_INVALID_HEADER;
	}
    }

  return NO_ERROR;
}

/*
 * spage_check_slot_owner () -
 *   return:
 *
 *   page_p(in):
 *   slot_id(in):
 */
int
spage_check_slot_owner (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  TRANID tranid;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  tranid = logtb_find_current_tranid (thread_p);
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);
  if (slot_p == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return 0;
    }

  return (*(TRANID *) (page_p + slot_p->offset_to_record) == tranid);
}

/*
 * spage_is_unknown_slot () -
 *   return:
 *
 *   slot_id(in): Slot identifier of desired record
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_p(in): Pointer to slotted page pointer array
 */
static bool
spage_is_unknown_slot (PGSLOTID slot_id, SPAGE_HEADER * page_header_p,
		       SPAGE_SLOT * slot_p)
{
  unsigned int max_offset;

  assert (slot_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  max_offset = DB_PAGESIZE - page_header_p->num_slots * sizeof (SPAGE_SLOT);

  assert_release (slot_p->offset_to_record >= sizeof (SPAGE_HEADER) ||
		  slot_p->offset_to_record == SPAGE_EMPTY_OFFSET);
  assert_release (slot_p->offset_to_record <= max_offset);

  return (slot_id < 0 || slot_id >= page_header_p->num_slots
	  || slot_p->offset_to_record == SPAGE_EMPTY_OFFSET
	  || slot_p->offset_to_record < sizeof (SPAGE_HEADER)
	  || slot_p->offset_to_record > max_offset);
}

/*
 * spage_find_slot () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   slot_id(in): Slot identifier of desired record
 *   is_unknown_slot_check(in):
 */
static SPAGE_SLOT *
spage_find_slot (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		 PGSLOTID slot_id, bool is_unknown_slot_check)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = (SPAGE_SLOT *) (page_p + DB_PAGESIZE - sizeof (SPAGE_SLOT));
  slot_p -= slot_id;

  if (is_unknown_slot_check)
    {
      if (spage_is_unknown_slot (slot_id, page_header_p, slot_p))
	{
	  return NULL;
	}
    }

  return slot_p;
}

/*
 * spage_has_enough_total_space () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   space(in):
 */
static bool
spage_has_enough_total_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			      SPAGE_HEADER * page_header_p, int space)
{
  TRANID tranid;
  int total_saved = 0;

  assert (page_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  if (space <= 0)
    {
      return true;
    }

  if (page_header_p->is_saving)
    {
      tranid = logtb_find_current_tranid (thread_p);

      if (logtb_is_active (thread_p, tranid))
	{
	  total_saved = spage_get_total_saved_spaces (thread_p, page_header_p,
						      page_p);
	}
      assert (total_saved >= 0);

      return (space <= (page_header_p->total_free - total_saved));
    }
  else
    {
      return (space <= page_header_p->total_free);
    }
}

/*
 * spage_has_enough_contiguous_space () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   page_header_p(in): Pointer to header of slotted page
 *   space(in)
 */
static bool
spage_has_enough_contiguous_space (PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p, int space)
{
  assert (page_p != NULL);
  SPAGE_VERIFY_HEADER (page_header_p);

  return (space <= page_header_p->cont_free
	  || spage_compact (page_p) == NO_ERROR);
}

/*
 * spage_add_contiguous_free_space () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   space(in):
 */
static void
spage_add_contiguous_free_space (PAGE_PTR page_p, int space)
{
  SPAGE_HEADER *page_header_p;

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  page_header_p->total_free += space;
  page_header_p->cont_free += space;
  page_header_p->offset_to_free_area -= space;

  spage_verify_header (page_p);
}

/*
 * spage_next_record_dont_skip_empty () - Get next slot without skipping
 *					  empty records.
 *
 * return		    : 
 * page_p (in)		    :
 * out_slot_id_p (in)	    :
 * record_descriptor_p (in) :
 * is_peeking (in)	    :
 */
SCAN_CODE
spage_next_record_dont_skip_empty (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
				   RECDES * record_descriptor_p,
				   int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_NEXT, false);
}

/*
 * spage_previous_record_dont_skip_empty () - Get previous slot without
 *					      skipping empty records.
 *
 * return		    :
 * page_p (in)		    :
 * out_slot_id_p (in)	    :
 * record_descriptor_p (in) :
 * is_peeking (in)	    :
 */
SCAN_CODE
spage_previous_record_dont_skip_empty (PAGE_PTR page_p,
				       PGSLOTID * out_slot_id_p,
				       RECDES * record_descriptor_p,
				       int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_PREV, false);
}

/*
 * spage_get_page_header_info () - Obtain page information for spage_header.
 *
 * return		 : 
 * page_p (in)		 :
 * page_header_info (in) :
 */
SCAN_CODE
spage_get_page_header_info (PAGE_PTR page_p, DB_VALUE ** page_header_info)
{
  SPAGE_HEADER *page_header_p;

  if (page_p == NULL || page_header_info == NULL)
    {
      return S_SUCCESS;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_NUM_SLOTS],
	       page_header_p->num_slots);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_NUM_RECORDS],
	       page_header_p->num_records);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_ANCHOR_TYPE],
	       page_header_p->anchor_type);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_ALIGNMENT],
	       page_header_p->alignment);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_TOTAL_FREE],
	       page_header_p->total_free);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_CONT_FREE],
	       page_header_p->cont_free);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_OFFSET_TO_FREE_AREA],
	       page_header_p->offset_to_free_area);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_IS_SAVING],
	       page_header_p->is_saving);
  DB_MAKE_INT (page_header_info[HEAP_PAGE_INFO_UPDATE_BEST],
	       page_header_p->need_update_best_hint);

  return S_SUCCESS;
}

/*
 * spage_get_record_offset () - Find the offset of the record associated with
 *                              the given slot on the given page
 *
 * return     : Record offset.
 * page_p (in): Pointer to slotted page.
 * slotid (in): Slot identifier of record.
 */
int
spage_get_record_offset (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return -1;
    }

  return slot_p->offset_to_record;
}

/*
 * spage_get_slot () - Looks for the slot with slot_id identifier in page_p
 *		       and returns a SPAGE_SLOT. Does not check for unknown
 *		       slots.
 *
 * return	: SPAGE_SLOT.
 * page_p (in)  : Pointer to slotted page.
 * slot_id (in) : Slot identifier.
 */
SPAGE_SLOT *
spage_get_slot (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_HEADER *page_header_p = NULL;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  if (slot_id < 0 || slot_id >= page_header_p->num_slots)
    {
      return NULL;
    }

  return spage_find_slot (page_p, page_header_p, slot_id, false);
}

/*
 * spage_vacuum_slot () - Used by vacuum to mark records as
 *					deleted after they have been
 *					completely removed from indexes.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * page_p (in)	 : Page pointer.
 * slotid (in)	 : Record slot id.
 */
int
spage_vacuum_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slotid,
		   OID * next_version, bool reusable)
{
  SPAGE_HEADER *page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_SLOT *slot_p = NULL;
  RECDES forward_recdes;
  int space_left;
  int waste;
  int free_space;

  SPAGE_VERIFY_HEADER (page_header_p);

  assert (spage_is_valid_anchor_type (page_header_p->anchor_type));

  slot_p = spage_find_slot (page_p, page_header_p, slotid, false);
  if (slot_p->record_type == REC_MVCC_NEXT_VERSION
      || slot_p->record_type == REC_MARKDELETED
      || slot_p->record_type == REC_DELETED_WILL_REUSE)
    {
      /* Vacuum error log */
      vacuum_er_log (VACUUM_ER_LOG_ERROR,
		     "VACUUM: Object (%d, %d, %d) was already vacuumed",
		     pgbuf_get_vpid_ptr (page_p)->volid,
		     pgbuf_get_vpid_ptr (page_p)->pageid, slotid);
      return NO_ERROR;
    }
  if (next_version != NULL && !OID_ISNULL (next_version))
    {
      /* Replace current record with an REC_MVCC_NEXT_VERSION which will
       * point to next_version.
       */
      assert (slot_p->record_type == REC_HOME
	      || slot_p->record_type == REC_BIGONE
	      || slot_p->record_type == REC_NEWHOME
	      || slot_p->record_type == REC_MVCC_NEXT_VERSION
	      || slot_p->record_type == REC_RELOCATION);
      forward_recdes.length = OR_OID_SIZE;
      forward_recdes.data = (char *) next_version;
      space_left = slot_p->record_length - OR_OID_SIZE;
      assert (space_left >= 0);

      if (spage_update_record_in_place (page_p, page_header_p, slot_p,
					&forward_recdes, -space_left)
	  != SP_SUCCESS)
	{
	  assert (0);
	  return ER_FAILED;
	}
      spage_set_slot (slot_p, slot_p->offset_to_record, OR_OID_SIZE,
		      REC_MVCC_NEXT_VERSION);

      SPAGE_VERIFY_HEADER (page_header_p);
      return NO_ERROR;
    }

  /* Slot is deleted */
  page_header_p->num_records--;
  waste = DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment);
  free_space = slot_p->record_length + waste;
  page_header_p->total_free += free_space;

  slot_p->offset_to_record = SPAGE_EMPTY_OFFSET;
  if (reusable)
    {
      slot_p->record_type = REC_DELETED_WILL_REUSE;
    }
  else
    {
      slot_p->record_type = REC_MARKDELETED;
    }
  SPAGE_VERIFY_HEADER (page_header_p);

#ifdef SPAGE_DEBUG
  spage_check (thread_p, page_p);
#endif

  return NO_ERROR;
}

/*
 * spage_reduce_contiguous_free_space () -
 *   return:
 *
 *   page_p(in): Pointer to slotted page
 *   space(in):
 */
static void
spage_reduce_contiguous_free_space (PAGE_PTR page_p, int space)
{
  SPAGE_HEADER *page_header_p;

  page_header_p = (SPAGE_HEADER *) page_p;
  SPAGE_VERIFY_HEADER (page_header_p);

  page_header_p->total_free -= space;
  page_header_p->cont_free -= space;
  page_header_p->offset_to_free_area += space;

  spage_verify_header (page_p);
}


/*
 * spage_header_start_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(out): allocate new context. should free by end_scan() function
 */
int
spage_header_start_scan (THREAD_ENTRY * thread_p, int show_type,
			 DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  DB_VALUE *arg_val0, *arg_val1;
  SPAGE_HEADER_CONTEXT *ctx;
  PAGE_PTR pgptr;
  PAGE_TYPE ptype;

  *ptr = NULL;

  arg_val0 = arg_values[0];
  arg_val1 = arg_values[1];

  assert (arg_val0 != NULL && DB_VALUE_TYPE (arg_val0) == DB_TYPE_INTEGER);
  assert (arg_val1 != NULL && DB_VALUE_TYPE (arg_val1) == DB_TYPE_INTEGER);

  ctx = (SPAGE_HEADER_CONTEXT *)
    db_private_alloc (thread_p, sizeof (SPAGE_HEADER_CONTEXT));

  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  ctx->vpid.volid = db_get_int (arg_val0);
  ctx->vpid.pageid = db_get_int (arg_val1);

  if (pgbuf_is_valid_page (thread_p, &ctx->vpid, true, NULL, NULL) !=
      DISK_VALID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_PAGE_NOT_FOUND, 2,
	      ctx->vpid.pageid, ctx->vpid.volid);
      error = ER_DIAG_PAGE_NOT_FOUND;
      goto exit_on_error;
    }

  pgptr = pgbuf_fix (thread_p, &ctx->vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  ptype = pgbuf_get_page_ptype (thread_p, pgptr);
  if (!spage_is_slotted_page_type (ptype))
    {
      pgbuf_unfix_and_init (thread_p, pgptr);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_NOT_SPAGE, 2,
	      ctx->vpid.pageid, ctx->vpid.volid);
      error = ER_DIAG_NOT_SPAGE;
      goto exit_on_error;
    }

  memcpy (&ctx->header, pgptr, sizeof (ctx->header));
  pgbuf_unfix_and_init (thread_p, pgptr);

  *ptr = ctx;

  return NO_ERROR;

exit_on_error:
  if (ctx != NULL)
    {
      db_private_free_and_init (thread_p, ctx);
    }

  return error;
}

/*
 * spage_header_next_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   cursor(in):
 *   out_values(in):
 *   out_cnt(in):
 *   ptr(in): context pointer
 */
SCAN_CODE
spage_header_next_scan (THREAD_ENTRY * thread_p, int cursor,
			DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  int idx = 0;
  SPAGE_HEADER_CONTEXT *ctx = (SPAGE_HEADER_CONTEXT *) ptr;
  SPAGE_HEADER *header = (SPAGE_HEADER *) (&ctx->header);

  if (cursor >= 1)
    {
      return S_END;
    }

  db_make_int (out_values[idx], ctx->vpid.volid);
  idx++;

  db_make_int (out_values[idx], ctx->vpid.pageid);
  idx++;

  db_make_int (out_values[idx], header->num_slots);
  idx++;

  db_make_int (out_values[idx], header->num_records);
  idx++;

  db_make_string (out_values[idx],
		  spage_anchor_flag_string (header->anchor_type));
  idx++;

  db_make_string (out_values[idx],
		  spage_alignment_string (header->alignment));
  idx++;

  db_make_int (out_values[idx], header->total_free);
  idx++;

  db_make_int (out_values[idx], header->cont_free);
  idx++;

  db_make_int (out_values[idx], header->offset_to_free_area);
  idx++;

  db_make_int (out_values[idx], header->need_update_best_hint);
  idx++;

  db_make_int (out_values[idx], header->is_saving);
  idx++;

  assert (idx == out_cnt);

  return S_SUCCESS;
}

/*
 * spage_header_end_scan () - free the context
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   ptr(in): context pointer
 */
int
spage_header_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  if (*ptr != NULL)
    {
      db_private_free_and_init (thread_p, *ptr);
    }

  return NO_ERROR;
}

/*
 * spage_slots_start_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(out): allocate new context. should free by end_scan() function
 */
int
spage_slots_start_scan (THREAD_ENTRY * thread_p, int show_type,
			DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  SPAGE_HEADER *header = NULL;
  DB_VALUE *arg_val0, *arg_val1;
  SPAGE_SLOTS_CONTEXT *ctx;
  PAGE_PTR pgptr;
  PAGE_TYPE ptype;

  *ptr = NULL;

  arg_val0 = arg_values[0];
  arg_val1 = arg_values[1];

  assert (arg_val0 != NULL && DB_VALUE_TYPE (arg_val0) == DB_TYPE_INTEGER);
  assert (arg_val1 != NULL && DB_VALUE_TYPE (arg_val1) == DB_TYPE_INTEGER);

  ctx = (SPAGE_SLOTS_CONTEXT *)
    db_private_alloc (thread_p, sizeof (SPAGE_SLOTS_CONTEXT));

  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  memset (ctx, 0, sizeof (SPAGE_SLOTS_CONTEXT));

  ctx->vpid.volid = db_get_int (arg_val0);
  ctx->vpid.pageid = db_get_int (arg_val1);

  if (pgbuf_is_valid_page (thread_p, &ctx->vpid, true, NULL, NULL) !=
      DISK_VALID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_PAGE_NOT_FOUND, 2,
	      ctx->vpid.pageid, ctx->vpid.volid);
      error = ER_DIAG_PAGE_NOT_FOUND;
      goto exit_on_error;
    }

  ctx->pgptr = (PAGE_PTR) db_private_alloc (thread_p, DB_PAGESIZE);
  if (ctx->pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  pgptr = pgbuf_fix (thread_p, &ctx->vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  ptype = pgbuf_get_page_ptype (thread_p, pgptr);
  if (!spage_is_slotted_page_type (ptype))
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_NOT_SPAGE, 2,
	      ctx->vpid.pageid, ctx->vpid.volid);
      error = ER_DIAG_NOT_SPAGE;
      goto exit_on_error;
    }

  memcpy (ctx->pgptr, pgptr, DB_PAGESIZE);
  pgbuf_unfix_and_init (thread_p, pgptr);

  header = (SPAGE_HEADER *) ctx->pgptr;
  ctx->slot = spage_find_slot ((PAGE_PTR) header, header, 0, false);

  *ptr = ctx;

  return NO_ERROR;

exit_on_error:
  if (ctx != NULL)
    {
      if (ctx->pgptr != NULL)
	{
	  db_private_free (thread_p, ctx->pgptr);
	}

      db_private_free_and_init (thread_p, ctx);
    }

  return error;
}

/*
 * spage_slots_next_scan () - 
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   cursor(in):
 *   out_values(in):
 *   out_cnt(in):
 *   ptr(in): context pointer
 */
SCAN_CODE
spage_slots_next_scan (THREAD_ENTRY * thread_p, int cursor,
		       DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  int idx = 0;
  SPAGE_SLOTS_CONTEXT *ctx = (SPAGE_SLOTS_CONTEXT *) ptr;
  SPAGE_HEADER *header = (SPAGE_HEADER *) ctx->pgptr;

  if (cursor >= header->num_slots)
    {
      return S_END;
    }

  /*
   * In the case of read a arbitrary specified page as slotted page,
   * num_slots of SPAGE_HEADER is meaningless data.
   * num_slots maybe too big to cause slot header address out of the page range.
   */
  if ((char *) ctx->slot < ((char *) header) + sizeof (SPAGE_HEADER))
    {
      return S_END;
    }

  db_make_int (out_values[idx], ctx->vpid.volid);
  idx++;

  db_make_int (out_values[idx], ctx->vpid.pageid);
  idx++;

  db_make_int (out_values[idx], cursor);
  idx++;

  db_make_int (out_values[idx], ctx->slot->offset_to_record);
  idx++;

  db_make_string (out_values[idx],
		  spage_record_type_string (ctx->slot->record_type));
  idx++;

  db_make_int (out_values[idx], ctx->slot->record_length);
  idx++;

  db_make_int (out_values[idx],
	       DB_WASTED_ALIGN (ctx->slot->record_length, header->alignment));
  idx++;

  assert (idx == out_cnt);

  ctx->slot--;

  return S_SUCCESS;
}

/*
 * spage_slots_end_scan () - free the context
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in): 
 *   ptr(in): context pointer
 */
int
spage_slots_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  SPAGE_SLOTS_CONTEXT *ctx = (SPAGE_SLOTS_CONTEXT *) * ptr;

  if (ctx != NULL)
    {
      if (ctx->pgptr != NULL)
	{
	  db_private_free (thread_p, ctx->pgptr);
	}

      db_private_free (thread_p, ctx);
      *ptr = NULL;
    }

  return NO_ERROR;
}
