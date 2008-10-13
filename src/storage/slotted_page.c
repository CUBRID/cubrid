/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * slotted_page.c - SLOTTED PAGE MANAGEMENT MODULE (AT THE SERVER)
 *
 * The slotted page manager manages insertions, deletions, and modifications  
 * of records on pages. The length of a record must be smaller than the size  
 * of a page minus a few bytes for header information. Each record in a       
 * slotted page has an associated slot identifier which can be thought of as  
 * an indirect pointer to the actual location of the record. Since references 
 * to records are always made through their slot identifier, this module is   
 * free to move the record within the page when either the record is updated  
 * (grows in size), or when the page is compacted. The following figure shows 
 * the format of a slotted page:                                              
 *                                                                            
 * --------------------------------------------------------------             
 * |Header| R0 | R1| ... | Rn | Free space | Sn | ... | S1 | S0 |             
 * --------------------------------------------------------------             
 *          ^    ^         ^                 |          |    |                
 *          |    |         |                 |          |    |                
 *          |    |         +-----------------+          |    |                
 *          |    +--------------------------------------+    |                
 *          +------------------------------------------------+                
 *                                                                            
 * NOTE: The records may not be physically ordered as it is presented above.  
 *                                                                              
 * The header of a slotted page contains information such as the number of    
 * records and slots, the amount of total and contiguous free space, and the  
 * type of slot management. Ro through Rn are data records, and S0 through Sn 
 * are the corresponding slots. Slots are stored from the end of the page     
 * toward the beginning of the page, and the data records are stored from the 
 * beginning to the end of the page. This layout eliminates shifting          
 * operations as much as possible during additions, deletions, and updates.   
 * When a data record is inserted, a slot identifier which describes the      
 * location of the record is allocated and returned.                          
 *                                                                            
 * There are several methods that control the way records and slots are       
 * maintained during insertion and deletion operations. In the simple case,   
 * records are either anchored or unanchored.                                 
 *                                                                            
 * The slot identifier of an anchored record cannot be changed. When an       
 * anchored record is deleted, there are two methods for handling the slots.  
 * In the first case, the slot identifier is marked for reuse; in the second  
 * case, the slot identifier remains unused unless the reuse is forced by the 
 * caller. The last technique can be used to store objects since they are     
 * anchored and its OIDs cannot be reused once the object is committed (i.e., 
 * heap management implementation). An OID can be reused if the object was    
 * created by an aborted transaction.                                         
 *                                                                            
 * If a record is unanchored, the slot identifiers can be changed. In this    
 * case there are two slot management schemes available.  In the first        
 * unanchored slot management scheme, slotids of records can be automatically 
 * changed during deletion of records or can be manually changed during       
 * insertion of records. When a record other than the last record on the page 
 * is deleted, the last record of the page is given the slot identifier of the
 * deleted record. When a record is inserted into an already occupied slot,   
 * the current record at the occupied slot is moved to the highest available  
 * slot. This method avoids the copy and shift operations required in         
 * traditional compression techniques. This method is good for the layout of 
 * extendible hash pages.                                                     
 *                                                                            
 * Example 1: Assume a page with the records A(slot 0), D(slot 1), E(slot 2), 
 *            and F(slot 3). When record A is removed, the slotids of the     
 *            records are changed as follow: F(slot 0), D(slot 1), and        
 *            E(slot 2).                                                      
 * Example 2: Assume a page with the records A(slot 0), D(slot 1), E(slot 2), 
 *            and F(slot 3). When record B is inserted at slot 1, the slotids 
 *            of the records are changed as follow: A(slot 0), B(slot 1),    
 *            E(slot 2), F(slot 3), and D(slot 4).                            
 *                                                                            
 * In the second unanchored scheme, slotids of records can be automatically   
 * changed during deletion of records or can be manually changed during       
 * insertion of records. However, the ordering of slots is preserved. A small 
 * performance penalty is incurred for compressing and expanding the slot     
 * pointer/offset array. The method is good for the layout of B+tree pages    
 * which need ordering of records.                                            
 *                                                                            
 * Example 1: Assume a page with the records A(slot 0), D(slot 1), E(slot 2), 
 *            and F(slot 3). When record A is removed, the slotids of the     
 *            records are changed as follow: D(slot 0), E(slot 1), and        
 *            F(slot 2).                                                      
 * Example 2: Assume a page with the records A(slot 0), D(slot 1), E(slot 2), 
 *            and F(slot 3). When record B is inserted at slot 1, the slotids 
 *            of the records are changed as follow: A(slot 0), B(slot 1),     
 *            C(slot 2), F(slot 3), and D (slot 4).                           
 *                                                                            
 * In short the slot types are:                                               
 *                                                                            
 * ANCHORED: The slotid of a record cannot be changed. Slot-ids of deleted    
 *           records are reused.                                              
 *                                                                            
 * ANCHORED_DONT_REUSE_SLOTS: Same as ANCHORED, however slotids of deleted    
 *           records are not reused, unless a deletion is forced by the an    
 *           caller. Heap implementation.                                     
 *                                                                            
 * UNANCHORED_ANY_SEQUENCE: Slotids of records can be automatically changed by
 *           this module during deletion of records or can be manually changed
 *           by caller request during insertions of records. The records may  
 *           not be kept in the same sequence. Extendible hash implementation.
 *                                                                            
 * UNANCHORED_KEEP_SEQUENCE: Same as UNANCHORED_ANY_SEQUENCE except the       
 *           original ordering of records is preserved. B+tree implementation.
 *                                                                            
 *                                                                            
 * This module provides a flexible facility to deal with space released by a  
 * transaction during deletions or updates on slotted pages. For example, the 
 * the space released by a transaction during deletions or updates of objects 
 * should not be used by another transaction until the space-releasing        
 * transaction is committed. However, for indices or hash entries we do not   
 * want to prevent the space released by one transaction from being consumed  
 * by another transaction before the commit of the first transaction. The     
 * slotted page module provides the flexibility for both needs. The following 
 * technique is used when deleted space is saved until the end of the         
 * transaction.                                                               
 * A memory hash table is used to keep saved space of several transactions:   
 *                                                                            
 * VPID | header + entries                                                    
 * -----|------------------                                                   
 *      |                                                                     
 *      |                                                                     
 *      |                                                                     
 *                                                                            
 * The header contains information of returned space from already finished    
 * transactions, and the header to entries. Each entry contains the           
 * transaction saving space and the amount of saved space by the transaction. 
 * The entry of the last transaction saving space is kept on the actual header
 * of the slotted page to avoid hashing as much as possible and larger hash   
 * tables. This entry is not replicated on the hash table. The following tasks
 * are done for the page operations.                                          
 *                                                                            
 * Note that if only one transaction is updating the page, there is not a hash
 * table entry for the page... We only have overhead when there are multiple  
 * concurrent transactions updating the page.                                 
 *                                                                            
 * For DELETIONS AND UPDATE SHRINKS                                           
 * 1. Delete: Space_to_save = old_length                                      
 *    Shrink: Space_to_save = old_length - new_length                         
 * 2. Execute normal operation                                                
 * 3. Execute saving operation.. Give space_to_save                           
 *                                                                            
 * For INSERTIONS AND EXPANSIONS                                              
 * 1. Insert: Needed_space = New_length                                       
 *    Expand: Needed_space = New_length - old_length                          
 * 2. If needed_space < total_free_space - total_saved_space_by_others        
 *       THEN                                                                 
 *         Execute Normal Operation                                           
 *       ELSE                                                                 
 *         Does not fit                                                       
 *                                                                            
 * For UNDO/REDOS,                                                            
 *     Execute the operation there is always space.                           
 * For COMMIT/ABORT                                                           
 *     Scan the transaction saving space table removing reclaiming the saved  
 *     space and removing the entries.                                        
 *     Header of hash entry.. Space_reclaimed  += saved_space                 
 *     NOTE: We don't fetch the page to fix the total_space_saved. This value 
 *           is fixed next time the saving or save_space_by_other function is 
 *           invoked on the page.                                             
 *                                                                            
 * SAVING OPERATION (Need pageptr and space to save/expend)                   
 *    1. For crash recovery:                                                  
 *       Do nothing.. initialize last transaction information with            
 *                Last TRANID = NULL_TRANID                                   
 *                   Last SAVED_SPACE = 0                                     
 *                    TOTAL SAVED_SPACE = 0                                   
 *                                                                            
 *     2. IF TRANID != Last TRANID                                            
 *           THEN                                                             
 *            2.1 Swap them...                                                
 *              a) If Last TRANID is still running (including during abort    
 *                 or commit)                                                 
 *                    THEN                                                    
 *                     Define its hash entry from page header information and 
 *                            add it onto the table                           
 *                    ELSE                                                    
 *                     Last TRANID has already finished. (Its saved_space has 
 *                            already being returned/spent)                   
 *                          TOTAL_SAVED_SPACE -= LAST SAVED_SPACE             
 *              b) Last TRANID = TRANID                                       
 *                 If current TRANID is stored on header table                
 *                    THEN                                                    
 *                      Last SAVED_SPACE = Saved_space value entry of TRANID  
 *                      Remove hash table entry                               
 *                    ELSE                                                    
 *                      Last SAVED_SPACE = 0                                  
 *            2.2 Fix total saved space...                                    
 *                TOTAL_SAVED_SPACE -= Header space_returned ... hash entry   
 *                Header space_returned = 0                                   
 *                                                                            
 *     3. Last SAVED_SPACE += space to save                                   
 *        TOTAL SAVED_SPACE += space to save                                  
 *                                                                            
 * FIND SAVE_SPACE_BY_OTHERS                                                  
 * -------------------------                                                  
 * 1. Step 1 of saving operation                                              
 * 2. Step 2 of saving operation                                              
 * 3. Total Saved_space - Last Saved space                                    
 *                                                                            
 *                                                                            
 * MODULE INTERFACE                                                           
 *                                                                            
 * At least the following modules are called by the slotted page module:      
 *                                                                            
 *      Page buffer Manager:         To dirty pages                           
 *      Log Manager:                 To find state of transactions and        
 *                                      transaction identifiers               
 *                                                                            
 * At least the following modules call the slotted page module:               
 *      B+tree, extendible hash, catalog manager, heap file object manager,   
 *        long data manager, query file manager to manage slotted pages       
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "slotted_page.h"
#include "common.h"
#include "memory_manager_2.h"
#include "error_manager.h"
#include "memory_hash.h"
#include "page_buffer.h"
#include "log.h"
#include "critical_section.h"
#if defined(SERVER_MODE)
#include "thread_impl.h"
#include "csserror.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#define pgbuf_lock_save_mutex(a)
#define pgbuf_unlock_save_mutex(a)
#endif /* !SERVER_MODE */

#define SPAGE_SEARCH_NEXT       1
#define SPAGE_SEARCH_PREV       -1

/* Find the total space saved by other transactions */
#define SPAGE_GET_SAVED_SPACES_BY_OTHER_TRANS(thread_p, sphdr, pgptr) \
  (((sphdr)->is_saving) \
    ? spage_get_saved_spaces_by_other_trans((thread_p), (sphdr), (pgptr)) : 0)

typedef struct spage_header SPAGE_HEADER;
struct spage_header
{
  PGNSLOTS num_slots;		/* Number of allocated slots for the page */
  PGNSLOTS num_records;		/* Number of records on page */
  INT16 anchor_type;		/* Valid ANCHORED, ANCHORED_DONT_REUSE_SLOTS
				   UNANCHORED_ANY_SEQUENCE,
				   UNANCHORED_KEEP_SEQUENCE */
  unsigned short alignment;	/* Alignment for records: Valid values sizeof
				   char, short, int, double */
  int waste_align;		/* Number of bytes wasted because of alignment */
  int total_free;		/* Total free space on page */
  int cont_free;		/* Contiguous free space on page */
  int offset_to_free_area;	/* Byte offset from the beginning of the page
				   to the first free byte area on the page. */
  bool is_saving;		/* True if saving is need for recovery (undo) */
  TRANID last_tranid;		/* Last tranid saving space for recovery
				   purposes */
  int saved;			/* Saved space of last transaction */
  int total_saved;		/* Total saved space by all transactions */
};

typedef struct spage_save_entry SPAGE_SAVE_ENTRY;
struct spage_save_entry
{				/* A hash entry to save space for future undos */
  TRANID tranid;		/* Transaction identifier */
  int saved;			/* Amount of space saved  */
  SPAGE_SAVE_ENTRY *next;	/* Next save */
};

typedef struct spage_save_head SPAGE_SAVE_HEAD;
struct spage_save_head
{				/* Head of a saving space */
  VPID vpid;			/* Page and volume where the space is saved */
  int return_savings;		/* Returned/free saved space */
  SPAGE_SAVE_ENTRY *first;	/* First saving space entry */
};

static MHT_TABLE *spage_Mht_saving;	/* Memory hash table for savings */

static int spage_save_space (THREAD_ENTRY * thread_p, SPAGE_HEADER * sphdr,
			     PAGE_PTR pgptr, int save);
static int spage_get_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
						  SPAGE_HEADER * sphdr,
						  PAGE_PTR pgptr);
static int spage_free_saved_spaces_helper (const void *vpid_key, void *ent,
					   void *tid);
static void spage_dump_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
						    VPID * vpid);
static int spage_compare_slot_offset (const void *arg1, const void *arg2);
static int spage_compact (PAGE_PTR pgptr);

static PGSLOTID spage_find_free_slot (PAGE_PTR page_p,
				      SPAGE_HEADER * page_header_p,
				      SPAGE_SLOT ** out_slot_p);
static int spage_check_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			      SPAGE_HEADER * page_header_p, int space);
static void spage_set_slot (SPAGE_SLOT * slot_p, int offset, int length,
			    INT16 type);
static int spage_find_empty_slot (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				  int length, INT16 type, SPAGE_SLOT ** sptr,
				  int *space, PGSLOTID * slotid);

static void spage_shift_slot_up (PAGE_PTR page_p,
				 SPAGE_HEADER * page_header_p,
				 SPAGE_SLOT * slot_p);
static void spage_shift_slot_down (PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   SPAGE_SLOT * slot_p);
static int spage_add_new_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			       SPAGE_HEADER * page_header_p, PGSLOTID slot_id,
			       SPAGE_SLOT * slot_p, int *out_space_p);
static int spage_take_slot_in_use (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   PGSLOTID slot_id, SPAGE_SLOT * slot_p,
				   int *out_space_p);
static int spage_find_empty_slot_at (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				     PGSLOTID slotid, int length, INT16 type,
				     SPAGE_SLOT ** sptr, int *space);

static int spage_check_record_for_insert (RECDES * record_descriptor_p);
static bool spage_is_record_located_at_end (SPAGE_HEADER * page_header_p,
					    SPAGE_SLOT * slot_p);
static void spage_reduce_a_slot (SPAGE_HEADER * page_header_p);

static int spage_check_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				  PGSLOTID slot_id,
				  const RECDES * record_descriptor_p,
				  SPAGE_SLOT ** out_slot_p, int *out_space_p,
				  int *out_old_waste_p, int *out_new_waste_p);
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
				      int is_peeking, int direction);

static const char *spage_record_type_string (INT16 record_type);
static const char *spage_anchor_flag_string (INT16 anchor_type);
static const char *spage_alignment_string (unsigned short alignment);

static void spage_dump_header (const SPAGE_HEADER * sphdr);
static void spage_dump_slots (const SPAGE_SLOT * sptr, PGNSLOTS nslots,
			      unsigned short alignment);
static void spage_dump_record (PAGE_PTR page_p, PGSLOTID slot_id,
			       SPAGE_SLOT * slot_p);
static void spage_check (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);

static bool spage_is_unknown_slot (PGSLOTID slotid, SPAGE_HEADER * sphdr,
				   SPAGE_SLOT * sptr);
static SPAGE_SLOT *spage_find_slot (PAGE_PTR pgptr, SPAGE_HEADER * sphdr,
				    PGSLOTID slotid, bool is_error_check);
static SCAN_CODE spage_get_record_data (PAGE_PTR pgptr, SPAGE_SLOT * sptr,
					RECDES * recdes, int ispeeking);
static bool spage_is_not_enough_total_space (THREAD_ENTRY * thread_p,
					     PAGE_PTR pgptr,
					     SPAGE_HEADER * sphdr, int space);
static bool spage_is_not_enough_contiguous_space (PAGE_PTR pgptr,
						  SPAGE_HEADER * sphdr,
						  int space);
static int spage_put_helper (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			     PGSLOTID slotid, int offset,
			     const RECDES * recdes, bool is_append);
static void spage_add_contiguous_free_space (SPAGE_HEADER * sphdr, int space);
static void spage_reduce_contiguous_free_space (SPAGE_HEADER * sphdr,
						int space);

/*
 * spage_free_saved_spaces () - Release the savings of transaction
 *   return: void
 *   tranid(in): Transaction from where the savings are release
 *
 * Note: This function could be called once a tranid has finished. 
 *       This is optional, it does not need to be done.
 */
void
spage_free_saved_spaces (THREAD_ENTRY * thread_p, TRANID tranid)
{
  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) == NO_ERROR)
    {
      (void) mht_map (spage_Mht_saving, spage_free_saved_spaces_helper,
		      &tranid);
      csect_exit (CSECT_SPAGE_SAVESPACE);
    }
  else
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
    }
}

/*
 * spage_free_saved_spaces_helper () - Remove the entries associated with the given
 *                      transaction identifier
 *   return: true
 *   vpid_key(in):  Volume and page identifier
 *   ent(in): Head entry information
 *   tid(in): Transaction identifier or NULL_TRANID 
 * 
 * Note: All entries are removed if the transaction identifier is NULL_TRANID.
 */
static int
spage_free_saved_spaces_helper (const void *vpid_key, void *ent, void *tid)
{
  SPAGE_SAVE_ENTRY *entry, *pv_entry, *tmp_entry;
  SPAGE_SAVE_HEAD *head;
  TRANID tranid;

  head = (SPAGE_SAVE_HEAD *) ent;
  tranid = *(TRANID *) tid;

  entry = head->first;
  pv_entry = NULL;

  while (entry != NULL)
    {
      if (tranid == NULL_TRANID || tranid == entry->tranid)
	{
	  /* If there is a save, return it since transaction will not be undone
	     any longer */
	  if (entry->saved > 0)
	    {
	      head->return_savings += entry->saved;
	    }

	  if (pv_entry != NULL)
	    {
	      pv_entry->next = entry->next;
	    }
	  else
	    {
	      head->first = entry->next;
	    }

	  tmp_entry = entry;
	  entry = entry->next;
	  free_and_init (tmp_entry);
	}
      else
	{
	  pv_entry = entry;
	  entry = entry->next;
	}
    }

  if (head->first == NULL)
    {
      (void) mht_rem (spage_Mht_saving, vpid_key, NULL, NULL);
      free_and_init (ent);
    }

  return true;
}

/*
 * spage_save_space () - Save some space for recovery (undo) purposes
 *   return: 
 *   sphdr(in): Pointer to header of slotted page 
 *   pgptr(in): Pointer to slotted page
 *   space(in): Amount of space to save
 * 
 * Note: The current transaction saving information is kept on the page and
 *       whatever was on the page is stored on the saving space hash table.
 *
 *       We only save for what we need to recovery (i.e., undo),savings should
 *       never go negative. That is, we could use some of our savings, but we
 *       could never go negative otherwise, transactions may steal needed space
 *       from each other.
 *
 *       The same algorithm as that of ARIES [MOHAN TODS92], DB2, IMS [OBER80].
 *       Algorithm:
 *             mysavings     += save;
 *             total_savings += save;
 *
 *       We cannot go negative during normal process since we must make sure
 *       that there is space for undoes..even under the following situation:
 *
 *         a) new object of 100 bytes 
 *         b) reduce length of object to 50 bytes
 *            Need to save 50 bytes for undo purposes. Nobody else but the
 *            current transaction should use those 50 bytes.
 */
static int
spage_save_space (THREAD_ENTRY * thread_p, SPAGE_HEADER * page_header_p,
		  PAGE_PTR page_p, int space)
{
  SPAGE_SAVE_HEAD *head_p;
  SPAGE_SAVE_ENTRY *entry_p, *prev_entry_p;
  VPID *vpid_p;
  TRANID tranid;

  assert (page_p != NULL);
  assert (page_header_p != NULL);

  if (space == 0 || log_is_in_crash_recovery ())
    {
      return NO_ERROR;
    }

  tranid = logtb_find_current_tranid (thread_p);

  /*
   * Saving takes effect only when the transaction is active, that is if it is
   * in recovery (undoing or redoing), there is nothing to save. However, we
   * could be expending our savings.
   */
  if (!logtb_is_active (thread_p, tranid) && space > 0)
    {
      return NO_ERROR;
    }

  /* Is there any transaction saving space on this page */
  if (page_header_p->last_tranid == NULL_TRANID)
    {
      /*
       * There is not any body saving.
       * There is not need to create a hash entry.
       *
       * We cannot go negative in our savings, otherwise, someone can use
       * the space that it is needed for undo.
       */
      if (space > 0)
	{
	  page_header_p->last_tranid = tranid;
	  page_header_p->saved = space;
	  page_header_p->total_saved = space;
	}
      return NO_ERROR;
    }
  else if (logtb_istran_finished (thread_p, page_header_p->last_tranid) ==
	   false)
    {
      /* There is already a transaction saving space on this page */
      if (tranid == page_header_p->last_tranid)
	{
	  /*
	   * Current transaction is the last one saving space
	   * 
	   * We cannot go over current savings, otherwise, someone can use
	   * the space that it is needed for undo. Use all savings.
	   */
	  if ((page_header_p->saved + space) < 0)
	    {
	      space = -page_header_p->saved;
	    }

	  page_header_p->saved += space;
	  page_header_p->total_saved += space;
	  return NO_ERROR;
	}
    }
  else
    {
      /* Last transaction has finished */
      page_header_p->last_tranid = NULL_TRANID;
      page_header_p->total_saved -= page_header_p->saved;
      page_header_p->saved = 0;
    }

  /* Swap the entries */

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
      /*
       * The only transaction that can be saving space is the one on the page,
       * and curent transaction cannot be the one since it has been checked
       * already
       */
      if (space <= 0)
	{
	  /* We cannot go negative in our savings, otherwise, someone can use
	     the space that it is needed for undo. */
	  csect_exit (CSECT_SPAGE_SAVESPACE);
	  return NO_ERROR;
	}

      if (page_header_p->last_tranid == NULL_TRANID)
	{
	  /* there is not any transaction saving.. there is not need to create
	     a hash table entry at this moment. */
	  page_header_p->last_tranid = tranid;
	  page_header_p->saved = space;
	  page_header_p->total_saved = space;
	}
      else
	{
	  /* There is a transaction saving, swap it */

	  head_p = (SPAGE_SAVE_HEAD *) malloc (sizeof (*head_p));
	  if (head_p == NULL)
	    {
	      csect_exit (CSECT_SPAGE_SAVESPACE);
	      return ER_FAILED;
	    }

	  entry_p = (SPAGE_SAVE_ENTRY *) malloc (sizeof (*entry_p));
	  if (entry_p == NULL)
	    {
	      free_and_init (head_p);
	      csect_exit (CSECT_SPAGE_SAVESPACE);
	      return ER_FAILED;
	    }

	  /*
	   * Form the head and the first entry with information of the page
	   * header, modify the header with current transaction saving, and
	   * add first entry into hash
	   */

	  head_p->vpid.volid = vpid_p->volid;
	  head_p->vpid.pageid = vpid_p->pageid;
	  head_p->return_savings = 0;
	  head_p->first = entry_p;

	  entry_p->tranid = page_header_p->last_tranid;
	  entry_p->saved = page_header_p->saved;
	  entry_p->next = NULL;

	  page_header_p->last_tranid = tranid;
	  page_header_p->saved = space;

	  page_header_p->total_saved += space;

	  (void) mht_put (spage_Mht_saving, &(head_p->vpid), head_p);
	}

      csect_exit (CSECT_SPAGE_SAVESPACE);
      return NO_ERROR;
    }

  /*
   * Check if the current transaction is in the list. If it is, swap its
   * values with the last saving transaction (the one on the page),
   * otherwise, create a new entry for the last saving transaction
   */

  /* Add the reclaimed space of committed/aborted transactions */

  page_header_p->total_saved -= head_p->return_savings;
  head_p->return_savings = 0;

  for (entry_p = head_p->first, prev_entry_p = NULL; entry_p != NULL;
       prev_entry_p = entry_p, entry_p = entry_p->next)
    {
      if (tranid == entry_p->tranid)
	{
	  /*
	   * The transaction was found
	   * 
	   * We cannot over current savings, otherwise, someone can use
	   * the space that it is needed for undo. Use all savings.
	   */
	  if ((entry_p->saved + space) < 0)
	    {
	      space = -entry_p->saved;
	    }

	  entry_p->saved += space;
	  page_header_p->total_saved += space;

	  /* Now swap the entries */

	  if (page_header_p->last_tranid == NULL_TRANID)
	    {
	      /* Remove the entry */
	      page_header_p->last_tranid = tranid;
	      page_header_p->saved = entry_p->saved;
	      if (prev_entry_p != NULL)
		{
		  prev_entry_p->next = entry_p->next;
		}
	      else
		{
		  head_p->first = entry_p->next;
		}

	      free_and_init (entry_p);

	      if (head_p->first == NULL)
		{
		  (void) mht_rem (spage_Mht_saving, &(head_p->vpid), NULL,
				  NULL);
		  free_and_init (head_p);
		}
	    }
	  else
	    {
	      space = entry_p->saved;
	      entry_p->tranid = page_header_p->last_tranid;
	      entry_p->saved = page_header_p->saved;

	      page_header_p->last_tranid = tranid;
	      page_header_p->saved = space;
	    }

	  csect_exit (CSECT_SPAGE_SAVESPACE);
	  return NO_ERROR;
	}
    }

  /* Current transaction is not in the list */

  if (space < 0)
    {
      /*
       * We cannot go negative in our savings, otherwise, someone can use
       * the space that it is needed for undo.
       *
       * We do not need to add a hash entry, but we may need to add us to
       * the header (sphdr) since when there is a hash the header is required,
       * and the header at this moment can be negative.
       */
      space = 0;
    }

  if (page_header_p->last_tranid == NULL_TRANID)
    {
      /* There is not need to create a hash entry */
      page_header_p->last_tranid = tranid;
      page_header_p->saved = space;
      page_header_p->total_saved += space;
    }
  else
    {
      if (space > 0)
	{
	  /* Need to allocate an entry */

	  entry_p = malloc (sizeof (*entry_p));
	  if (entry_p == NULL)
	    {
	      csect_exit (CSECT_SPAGE_SAVESPACE);
	      return ER_FAILED;
	    }

	  /* Swap the entries */
	  entry_p->tranid = page_header_p->last_tranid;
	  entry_p->saved = page_header_p->saved;
	  entry_p->next = head_p->first;
	  head_p->first = entry_p;

	  page_header_p->last_tranid = tranid;
	  page_header_p->saved = space;
	  page_header_p->total_saved += space;
	}
    }

  csect_exit (CSECT_SPAGE_SAVESPACE);
  return NO_ERROR;
}

/*
 * spage_get_saved_spaces_by_other_trans () - Find the total saved space by 
 *                                            other transactions
 *   return: 
 *   sphdr(in): Pointer to header of slotted page
 *   pgptr(in): Pointer to slotted page
 */
static int
spage_get_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
				       SPAGE_HEADER * page_header_p,
				       PAGE_PTR page_p)
{
  SPAGE_SAVE_HEAD *head_p;
  SPAGE_SAVE_ENTRY *entry_p, *prev_entry_p;
  VPID *vpid_p;
  TRANID tranid;
  int save;
  LOG_DATA_ADDR log_data_addr;
  int size;

  assert (page_p != NULL);
  assert (page_header_p != NULL);

  pgbuf_lock_save_mutex (page_p);

  /* Collect any saved space that has been freed */

  if (page_header_p->last_tranid == NULL_TRANID)
    {
      if (page_header_p->total_saved != 0)
	{
	  page_header_p->total_saved = 0;
	}

      pgbuf_unlock_save_mutex (page_p);
      return 0;
    }

  log_data_addr.vfid = NULL;
  log_data_addr.pgptr = page_p;
  log_data_addr.offset = 0;

  tranid = logtb_find_current_tranid (thread_p);

  /* If we are running crash recovery, we are not saving at all */
  if (log_is_in_crash_recovery () ||
      logtb_istran_finished (thread_p, page_header_p->last_tranid) == true)
    {
      /*
       * Last transaction has completely finished (including commit and
       * rollback) or we are in crash recovery. That is, it is not alive
       * any longer.
       */
      page_header_p->total_saved -= page_header_p->saved;
      page_header_p->saved = 0;
      page_header_p->last_tranid = NULL_TRANID;
      log_skip_logging (thread_p, &log_data_addr);
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

  /* Add any reclaimed/freed space */

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      /* TODO: missing to return ?? */
    }

  vpid_p = pgbuf_get_vpid_ptr (page_p);
  head_p = (SPAGE_SAVE_HEAD *) mht_get (spage_Mht_saving, vpid_p);
  if (head_p != NULL)
    {
      if (head_p->return_savings != 0)
	{
	  /* Add this savings to the page header */
	  page_header_p->total_saved -= head_p->return_savings;
	  head_p->return_savings = 0;
	  log_skip_logging (thread_p, &log_data_addr);
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	}

      /*
       * If the current transaction is the last one saving space, return.
       * Otherwise, check if the current transaction is in the list.
       * If it is, swap its values with the last saving transaction (i.e., the
       * one on the page).
       */

      if (tranid != page_header_p->last_tranid)
	{
	  /* Swap the entries if current transaction is in the list */
	  for (entry_p = head_p->first, prev_entry_p = NULL; entry_p != NULL;
	       prev_entry_p = entry_p, entry_p = entry_p->next)
	    {
	      if (tranid == entry_p->tranid)
		{
		  /* Swap the entry */
		  save = entry_p->saved;
		  if (page_header_p->last_tranid == NULL_TRANID)
		    {
		      /* Remove the entry */
		      if (prev_entry_p != NULL)
			{
			  prev_entry_p->next = entry_p->next;
			}
		      else
			{
			  head_p->first = entry_p->next;
			}

		      free_and_init (entry_p);

		      if (head_p->first == NULL)
			{
			  (void) mht_rem (spage_Mht_saving, &(head_p->vpid),
					  NULL, NULL);
			  free_and_init (head_p);
			}
		    }
		  else
		    {
		      entry_p->tranid = page_header_p->last_tranid;
		      entry_p->saved = page_header_p->saved;
		    }

		  page_header_p->last_tranid = tranid;
		  page_header_p->saved = save;
		  log_skip_logging (thread_p, &log_data_addr);
		  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

		  break;
		}
	    }
	}

      /* Current transaction is not in the list, add an entry only if there is
         not any one on the head */

      if (page_header_p->last_tranid == NULL_TRANID)
	{
	  page_header_p->last_tranid = tranid;
	  page_header_p->saved = 0;
	  log_skip_logging (thread_p, &log_data_addr);
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	}
    }
  else
    {
      /* There is not an entry in hash. If nobody is saving, then make sure
         that total saving is zero */
      if (page_header_p->last_tranid == NULL_TRANID
	  && page_header_p->total_saved != 0)
	{
	  page_header_p->total_saved = 0;
	  log_skip_logging (thread_p, &log_data_addr);
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	}
      else if (tranid == page_header_p->last_tranid
	       && page_header_p->saved > 0)
	{
	  page_header_p->total_saved = page_header_p->saved;
	}
    }

  if (tranid == page_header_p->last_tranid && page_header_p->saved > 0)
    {
      size = page_header_p->total_saved - page_header_p->saved;
    }
  else
    {
      size = page_header_p->total_saved;
    }

  csect_exit (CSECT_SPAGE_SAVESPACE);
  pgbuf_unlock_save_mutex (page_p);

  return size;
}

/*
 * spage_dump_saved_spaces_by_other_trans () - Dump the savings associated with 
 *                                             the given page that are part of
 *                                             the hash table
 *   return: void
 *   vpid(in):  Volume and page identifier
 */
static void
spage_dump_saved_spaces_by_other_trans (THREAD_ENTRY * thread_p,
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
      fprintf (stdout,
	       "Other savings of VPID = %d|%d returned savings = %d\n",
	       head_p->vpid.volid, head_p->vpid.pageid,
	       head_p->return_savings);

      /* Go over the linked list of entries */
      entry_p = head_p->first;

      while (entry_p != NULL)
	{
	  fprintf (stdout, "   Tranid = %d save = %d\n",
		   entry_p->tranid, entry_p->saved);
	  entry_p = entry_p->next;
	}
    }

  csect_exit (CSECT_SPAGE_SAVESPACE);
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

  if (csect_enter (thread_p, CSECT_SPAGE_SAVESPACE, INF_WAIT) != NO_ERROR)
    {
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (spage_Mht_saving != NULL)
    {
      (void) mht_clear (spage_Mht_saving);
    }
  else
    {
      spage_Mht_saving =
	mht_create ("Page space saving table", 50, pgbuf_hash_vpid,
		    pgbuf_compare_vpid);
    }

  r = ((spage_Mht_saving != NULL) ? NO_ERROR : ER_FAILED);

  csect_exit (CSECT_SPAGE_SAVESPACE);

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
      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      /* TODO: missing to return ?? */
    }

  if (spage_Mht_saving != NULL)
    {
      (void) mht_map (spage_Mht_saving, spage_free_saved_spaces_helper,
		      &tranid);
      mht_destroy (spage_Mht_saving);
      spage_Mht_saving = NULL;
    }

  csect_exit (CSECT_SPAGE_SAVESPACE);
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
 *   pgptr(in): Pointer to slotted page
 */
PGNSLOTS
spage_number_of_records (PAGE_PTR page_p)
{
  assert (page_p != NULL);
  return ((SPAGE_HEADER *) page_p)->num_records;
}

/*
 * spage_get_free_space () - Returns total free area
 *   return: Total free space
 *   pgptr(in): Pointer to slotted page
 */
int
spage_get_free_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;

  return (page_header_p->total_free -
	  SPAGE_GET_SAVED_SPACES_BY_OTHER_TRANS (thread_p, page_header_p,
						 page_p));
}

/*
 * spage_max_space_for_new_record () - Find the maximum free space for a new
 *                                     insertion
 *   return: Maximum free length for an insertion
 *   pgptr(in): Pointer to slotted page
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
  total_free =
    page_header_p->total_free -
    SPAGE_GET_SAVED_SPACES_BY_OTHER_TRANS (thread_p, page_header_p, page_p);

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
      total_free -= sizeof (SPAGE_SLOT);
      if (total_free < sizeof (SPAGE_SLOT))
	{
	  total_free = 0;
	}
    }

  DB_ALIGN_BELOW (total_free, page_header_p->alignment);
  return total_free;
}

/*
 * spage_count_pages () - Count the number of pages
 *   return: the number of pages
 *   pgptr(in): Pointer to slotted page
 *   dont_count_slotid(in): Ignore record stored in this slot identifier
 */
int
spage_count_pages (PAGE_PTR page_p, const PGSLOTID dont_count_slotid)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int pages, i;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  pages = 1;

  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record == NULL_OFFSET || dont_count_slotid == i)
	{
	  continue;
	}

      if (slot_p->record_type == REC_BIGONE)
	{
	  pages += 2;
	}
    }

  return pages;
}

/*
 * spage_count_records () - Count the number of records
 *   return: the number of records
 *   pgptr(in): Pointer to slotted page
 *   dont_count_slotid(in): Ignore record stored in this slot identifier
 */
int
spage_count_records (PAGE_PTR page_p, const PGSLOTID dont_count_slotid)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int records, i;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  records = 0;

  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record == NULL_OFFSET || dont_count_slotid == i)
	{
	  continue;
	}

      if (slot_p->record_type != REC_NEWHOME)
	{
	  records++;
	}
    }

  return records;
}

/*
 * spage_sum_length_of_records () - Sum total length of records
 *   return: total length of records
 *   pgptr(in): Pointer to slotted page
 *   dont_count_slotid(in): Ignore record stored in this slot identifier
 */
int
spage_sum_length_of_records (PAGE_PTR page_p,
			     const PGSLOTID dont_count_slotid)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int length, i;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  length = 0;

  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record == NULL_OFFSET || dont_count_slotid == i)
	{
	  continue;
	}

      if (slot_p->record_type == REC_HOME
	  || slot_p->record_type == REC_NEWHOME)
	{
	  length += (float) slot_p->record_length;
	}
      else if (slot_p->record_type == REC_BIGONE)
	{
	  length += (float) (DB_PAGESIZE * 2);	/* Assume two pages */
	}
    }

  return length;
}

/*
 * spage_initialize () - Initialize a slotted page
 *   return: void
 *   pgptr(in): Pointer to slotted page
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
  assert (slot_type == ANCHORED || slot_type == ANCHORED_DONT_REUSE_SLOTS ||
	  slot_type == UNANCHORED_ANY_SEQUENCE ||
	  slot_type == UNANCHORED_KEEP_SEQUENCE);
  assert (alignment == CHAR_ALIGNMENT || alignment == SHORT_ALIGNMENT ||
	  alignment == INT_ALIGNMENT || alignment == LONG_ALIGNMENT ||
	  alignment == FLOAT_ALIGNMENT || alignment == DOUBLE_ALIGNMENT);

  page_header_p = (SPAGE_HEADER *) page_p;

  page_header_p->num_slots = 0;
  page_header_p->num_records = 0;
  page_header_p->is_saving = is_saving;
  page_header_p->last_tranid = NULL_TRANID;
  page_header_p->saved = 0;
  page_header_p->total_saved = 0;

  page_header_p->anchor_type = slot_type;
  page_header_p->total_free = DB_PAGESIZE - sizeof (SPAGE_HEADER);
  DB_ALIGN (page_header_p->total_free, alignment);

  page_header_p->cont_free = page_header_p->total_free;
  page_header_p->offset_to_free_area = sizeof (SPAGE_HEADER);
  DB_ALIGN (page_header_p->offset_to_free_area, alignment);

  page_header_p->alignment = alignment;
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
}

/*
 * sp_offcmp () - Compare the location (offset) of slots 
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
 * sp_compact () -  Compact an slotted page
 *   return: 
 *   pgptr(in): Pointer to slotted page
 * 
 * Note: Only the records are compacted, the slots are not compacted.
 */
static int
spage_compact (PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  SPAGE_SLOT **slot_array = NULL;
  int to_offset;
  int i, j;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  if (page_header_p->num_records > 0)
    {
      slot_array = (SPAGE_SLOT **)
	calloc ((unsigned int) (page_header_p->num_records),
		sizeof (SPAGE_SLOT *));
      if (slot_array == NULL)
	{
	  return ER_FAILED;
	}

      /* Create an array of sorted offsets... actually pointers to slots */

      slot_p = spage_find_slot (page_p, page_header_p, 0, false);
      for (j = 0, i = 0; i < page_header_p->num_slots; slot_p--, i++)
	{
	  if (slot_p->offset_to_record != NULL_OFFSET)
	    {
	      slot_array[j++] = slot_p;
	    }
	}

      qsort ((void *) slot_array, page_header_p->num_records,
	     sizeof (SPAGE_SLOT *), spage_compare_slot_offset);

      /* Now start compacting the page */
      to_offset = sizeof (SPAGE_HEADER);
      for (i = 0; i < page_header_p->num_records; i++)
	{
	  /* Make sure that the offset is aligned */
	  DB_ALIGN (to_offset, page_header_p->alignment);
	  if (to_offset == slot_array[i]->offset_to_record)
	    {
	      /* Record slot is already in place */
	      to_offset += slot_array[i]->record_length;
	    }
	  else
	    {
	      /* Move the record */
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
  DB_ALIGN (to_offset, page_header_p->alignment);
  page_header_p->cont_free = page_header_p->total_free =
    (DB_PAGESIZE - to_offset -
     page_header_p->num_slots * sizeof (SPAGE_SLOT));
  page_header_p->offset_to_free_area = to_offset;

  /* The page is set dirty somewhere else */
  return NO_ERROR;
}

static PGSLOTID
spage_find_free_slot (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		      SPAGE_SLOT ** out_slot_p)
{
  PGSLOTID slot_id;
  SPAGE_SLOT *slot_p;

  slot_p = spage_find_slot (page_p, page_header_p, 0, false);

  if (page_header_p->num_slots == page_header_p->num_records)
    {
      slot_id = page_header_p->num_slots;
      slot_p -= slot_id;
    }
  else
    {
      for (slot_id = 0;
	   slot_id < page_header_p->num_slots
	   && slot_p->record_type != REC_DELETED_WILL_REUSE;
	   slot_p--, slot_id++)
	{
	  ;
	}
    }

  *out_slot_p = slot_p;
  return slot_id;
}

static int
spage_check_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		   SPAGE_HEADER * page_header_p, int space)
{
  if (spage_is_not_enough_total_space
      (thread_p, page_p, page_header_p, space))
    {
      return SP_DOESNT_FIT;
    }
  else
    if (spage_is_not_enough_contiguous_space (page_p, page_header_p, space))
    {
      return SP_ERROR;
    }

  return SP_SUCCESS;
}

static void
spage_set_slot (SPAGE_SLOT * slot_p, int offset, int length, INT16 type)
{
  slot_p->offset_to_record = offset;
  slot_p->record_length = length;
  slot_p->record_type = type;
}

/*
 * sp_empty () - Find a free area/slot where a record of the given length can
 *               be inserted onto the given slotted page
 *   return: either SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   sptr(out): Pointer to slotted page array pointer 
 *   length(in): Length of area/record
 *   type(in): Type of record to be inserted
 *   space(out): Space used/defined
 *   slotid(out): Allocated slot or NULL_SLOTID
 * 
 * Note: If there is not enough space on the page, an error condition is
 *       returned and slotid is set to NULL_SLOTID.
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

  /* Calculate the wasted space that this record will introduce. We need to
     take in consideration the wasted space when there is space saved */
  DB_WASTED_ALIGN (record_length, page_header_p->alignment, waste);
  space = record_length + waste;

  /* Quickly check for available space. We may need to check again if a slot
     is created (instead of reused) */
  if (spage_is_not_enough_total_space
      (thread_p, page_p, page_header_p, space))
    {
      return SP_DOESNT_FIT;
    }

  /* Find a free slot. Try to reuse an unused slotid, instead of allocating a
     new one */
  slot_id = spage_find_free_slot (page_p, page_header_p, &slot_p);

  /* Make sure that there is enough space for the record and the slot */

  if (slot_id >= page_header_p->num_slots)
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
      if (spage_is_not_enough_contiguous_space (page_p, page_header_p, space))
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
  page_header_p->offset_to_free_area += record_length + waste;

  *out_slot_p = slot_p;
  *out_space_p = space;
  *out_slot_id_p = slot_id;

  /* The page is set dirty somewhere else */
  return SP_SUCCESS;
}

static void
spage_shift_slot_up (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		     SPAGE_SLOT * slot_p)
{
  SPAGE_SLOT *last_slot_p;

  assert (page_p != NULL);
  assert (page_header_p != NULL);
  assert (slot_p != NULL);

  last_slot_p =
    spage_find_slot (page_p, page_header_p, page_header_p->num_slots, false);

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
    }
}

static void
spage_shift_slot_down (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		       SPAGE_SLOT * slot_p)
{
  SPAGE_SLOT *last_slot_p;

  assert (page_p != NULL);
  assert (page_header_p != NULL);
  assert (slot_p != NULL);

  last_slot_p =
    spage_find_slot (page_p, page_header_p, page_header_p->num_slots - 1,
		     false);

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
}

static int
spage_add_new_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		    SPAGE_HEADER * page_header_p, PGSLOTID slot_id,
		    SPAGE_SLOT * slot_p, int *out_space_p)
{
  PGNSLOTS nslots;
  int status, i;

  /*
   * New slots (likely one) are being allocated. The slotid may be located
   * farther away (i.e., more than one) from the number of slots. This
   * situation may happen during UNDOs and/or REDOs.
   */
  nslots = slot_id + 1 - page_header_p->num_slots;
  *out_space_p += sizeof (SPAGE_SLOT) * nslots;

  status = spage_check_space (thread_p, page_p, page_header_p, *out_space_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* Adjust the pointer array for the new slots */
  if (nslots > 1)
    {
      for (i = 0; i < nslots - 1; i++)
	{
	  slot_p++;
	  slot_p->offset_to_record = NULL_OFFSET;
	  slot_p->record_type = REC_DELETED_WILL_REUSE;
	}
    }

  /* Adjust the space for creation of new slots. The space for the record
     is adjusted later on */
  page_header_p->num_slots += nslots;

  return SP_SUCCESS;
}

static int
spage_take_slot_in_use (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			SPAGE_HEADER * page_header_p, PGSLOTID slot_id,
			SPAGE_SLOT * slot_p, int *out_space_p)
{
  int status;

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
      status =
	spage_check_space (thread_p, page_p, page_header_p, *out_space_p);
      if (status != SP_SUCCESS)
	{
	  return status;
	}
    }
  else
    {
      /* Slot is in use. The pointer array must be shifted
         (i.e., addresses of records are modified). */
      if (page_header_p->anchor_type == ANCHORED ||
	  page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SP_BAD_INSERTION_SLOT, 3, slot_id,
		  pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p));
	  return SP_ERROR;
	}

      /* Make sure that there is enough space for the record and the slot */

      *out_space_p += sizeof (SPAGE_SLOT);
      status =
	spage_check_space (thread_p, page_p, page_header_p, *out_space_p);
      if (status != SP_SUCCESS)
	{
	  return status;
	}

      spage_shift_slot_up (page_p, page_header_p, slot_p);
      /* Adjust the header for the newly created slot */
      page_header_p->num_slots += 1;
    }

  return SP_SUCCESS;
}

/*
 * sp_atempty () - Find a free area where a record of the given length can be
 *                 inserted onto the given slotted page
 *   return: either SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   sptr(out): Pointer to slotted page array pointer
 *   slotid(in): Requested slotid
 *   length(in): Length of area/record
 *   type(in): Type of record to be inserted
 *   space(out): Space used/defined
 * 
 * Note: The record is inserted using the given slotid. If the slotid is
 *       currently being used by another record, the following scheme is used
 *       depending upon the page type:
 *  
 *       1) UNANCHORED_ANY_SEQUENCE:                                   
 *          A new slot is defined at the end of the pointer array. The
 *          record currently being held by slotid is moved to the new  
 *          slot (i.e., the address of the record is modified). Then,  
 *          the desired record is stored as part of the given slotid.  
 *       2) UNANCHORED_KEEP_SEQUENCE:                                  
 *          A new slot is defined at the end of the pointer array.     
 *          Then, the slots starting at slotid are shifted by one slot 
 *          (i.e., the addresses of the records being referenced by    
 *          these slots are modified). Then, the desired record is     
 *          stored as part of the given slotid.                        
 *       3) ANCHORED or ANCHORED_DONT_REUSE_SLOTS:                     
 *          Operation fails since the address of the record cannot be  
 *          modified. An error condition is and NULL_SLOTID is returned.
 * 
 *       If there is not enough space on the page, an error condition is
 *       returned and slotid is set to NULL_SLOTID.
 */
static int
spage_find_empty_slot_at (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			  PGSLOTID slot_id, int record_length,
			  INT16 record_type, SPAGE_SLOT ** out_slot_p,
			  int *out_space_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int waste, space, status;

  assert (page_p != NULL);
  assert (out_slot_p != NULL);
  assert (out_space_p != NULL);

  *out_slot_p = NULL;
  *out_space_p = 0;

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);

  /* Calculate the wasted space that this record will introduce. We need to
     take in consideration the wasted space when there is space saved */
  DB_WASTED_ALIGN (record_length, page_header_p->alignment, waste);
  space = record_length + waste;

  if (slot_id >= page_header_p->num_slots)
    {
      status =
	spage_add_new_slot (thread_p, page_p, page_header_p, slot_id, slot_p,
			    &space);
    }
  else
    {
      status =
	spage_take_slot_in_use (thread_p, page_p, page_header_p, slot_id,
				slot_p, &space);
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
  page_header_p->offset_to_free_area += record_length + waste;

  *out_slot_p = slot_p;
  *out_space_p = space;

  /* The page is set dirty somewhere else */
  return SP_SUCCESS;
}

static int
spage_check_record_for_insert (RECDES * record_descriptor_p)
{
  if (record_descriptor_p->length > spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  if (record_descriptor_p->type == REC_MARKDELETED ||
      record_descriptor_p->type == REC_DELETED_WILL_REUSE)
    {
      record_descriptor_p->type = REC_HOME;
    }

  return SP_SUCCESS;
}

/*
 * spage_insert () - Insert a record
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   recdes(in): Pointer to a record descriptor
 *   slotid(out): Slot identifier
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

  status =
    spage_find_slot_for_insert (thread_p, page_p, record_descriptor_p,
				out_slot_id_p, (void **) &slot_p,
				&used_space);
  if (status == SP_SUCCESS)
    {
      status =
	spage_insert_data (thread_p, page_p, record_descriptor_p, slot_p,
			   used_space) == NO_ERROR ? SP_SUCCESS : SP_ERROR;
    }

  return status;
}

/*
 * spage_find_slot_for_insert () - Find a slot id and related information in the 
 *                          given page
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   recdes(in): Pointer to a record descriptor
 *   slotid(out): Slot identifier
 *   slotptr(out): Pointer to slotted array 
 *   used_space(out): Pointer to int
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

  status =
    spage_find_empty_slot (thread_p, page_p, record_descriptor_p->length,
			   record_descriptor_p->type, &slot_p,
			   out_used_space_p, out_slot_id_p);
  *out_slot_p = (void *) slot_p;

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return status;
}

/*
 * spage_insert_data () - Copy the contents of a record into the given page/slot
 *   return: NO_ERROR
 *   pgptr(in): Pointer to slotted page
 *   recdes(in): Pointer to a record descriptor
 *   slotptr(in): Slot identifier
 *   used_space(in):  Pointer to slotted array 
 */
int
spage_insert_data (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		   RECDES * record_descriptor_p, void *slot_p, int used_space)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *tmp_slot_p;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);
  assert (slot_p != NULL);

  tmp_slot_p = (SPAGE_SLOT *) slot_p;
  if (record_descriptor_p->type != REC_ASSIGN_ADDRESS)
    {
      memcpy (((char *) page_p + tmp_slot_p->offset_to_record),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  else
    {
      *(TRANID *) (page_p + tmp_slot_p->offset_to_record) =
	logtb_find_current_tranid (thread_p);
    }

  /* Indicate that we are spending our savings */
  page_header_p = (SPAGE_HEADER *) page_p;
  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   -used_space) != NO_ERROR)
    {
      return ER_FAILED;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return NO_ERROR;
}

/*
 * spage_insert_at () - Insert a record onto the given slotted page at the given
 *                  slotid
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slotid for newly inserted record 
 *   recdes(in): Pointer to a record descriptor
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
  int used_space;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  status = spage_check_record_for_insert (record_descriptor_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  page_header_p = (SPAGE_HEADER *) page_p;

  assert (page_header_p->anchor_type != ANCHORED &&
	  page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS);

  if (slot_id > page_header_p->num_slots)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  status =
    spage_find_empty_slot_at (thread_p, page_p, slot_id,
			      record_descriptor_p->length,
			      record_descriptor_p->type, &slot_p,
			      &used_space);
  if (status == SP_SUCCESS)
    {
      memcpy (((char *) page_p + slot_p->offset_to_record),
	      record_descriptor_p->data, record_descriptor_p->length);
      /* Indicate that we are spending our savings */
      if (page_header_p->is_saving
	  && spage_save_space (thread_p, page_header_p, page_p,
			       -used_space) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return SP_ERROR;
	}
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return status;
}

/*
 * spage_insert_for_recovery () - Insert a record onto the given slotted page at
 *                                the given slotid (only for recovery)
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slotid for insertion
 *   recdes(in): Pointer to a record descriptor
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
  int waste;
  int used_space;
  int total_free_save;
  int status;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  if (page_header_p->anchor_type != ANCHORED &&
      page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS)
    {
      return spage_insert_at (thread_p, page_p, slot_id, record_descriptor_p);
    }

  status = spage_check_record_for_insert (record_descriptor_p);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* If there is a record located at the given slot, the record is removed */

  total_free_save = page_header_p->total_free;
  if (slot_id < page_header_p->num_slots)
    {
      slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);
      if (slot_p->offset_to_record != NULL_OFFSET)
	{
	  page_header_p->num_records--;
	  DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
			   waste);
	  page_header_p->total_free += slot_p->record_length + waste;
	  slot_p->offset_to_record = NULL_OFFSET;
	}
      slot_p->record_type = REC_DELETED_WILL_REUSE;
    }

  status =
    spage_find_empty_slot_at (thread_p, page_p, slot_id,
			      record_descriptor_p->length,
			      record_descriptor_p->type, &slot_p,
			      &used_space);
  if (status == SP_SUCCESS)
    {
      if (record_descriptor_p->type != REC_ASSIGN_ADDRESS)
	{
	  memcpy (((char *) page_p + slot_p->offset_to_record),
		  record_descriptor_p->data, record_descriptor_p->length);
	}

      if (page_header_p->is_saving
	  && spage_save_space (thread_p, page_header_p, page_p,
			       page_header_p->total_free - total_free_save) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return SP_ERROR;
	}
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */
  return status;
}

static bool
spage_is_record_located_at_end (SPAGE_HEADER * page_header_p,
				SPAGE_SLOT * slot_p)
{
  int waste;

  DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment, waste);

  return (slot_p->offset_to_record + slot_p->record_length + waste) ==
    page_header_p->offset_to_free_area;
}

static void
spage_reduce_a_slot (SPAGE_HEADER * page_header_p)
{
  page_header_p->num_slots--;
  page_header_p->total_free += sizeof (SPAGE_SLOT);
  page_header_p->cont_free += sizeof (SPAGE_SLOT);
}

/*
 * spage_delete () - Delete the record located at given slot on the given page
 *   return: slotid on success and NULL_SLOTID on failure
 *   pgptr(in): Pointer to slotted page 
 *   slotid(in): Slot identifier of record to delete
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
  assert (page_header_p->anchor_type == ANCHORED
	  || page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS
	  || page_header_p->anchor_type == UNANCHORED_ANY_SEQUENCE
	  || page_header_p->anchor_type == UNANCHORED_KEEP_SEQUENCE);

  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return NULL_SLOTID;
    }

  if (page_header_p->num_records == 1 && page_header_p->is_saving != true &&
      page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS)
    {
      /* Initialize the page to avoid future compactions */
      spage_initialize (thread_p, page_p, page_header_p->anchor_type,
			page_header_p->alignment, page_header_p->is_saving);
    }
  else
    {
      page_header_p->num_records--;
      DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
		       waste);
      free_space = slot_p->record_length + waste;
      page_header_p->total_free += free_space;

      /* If it is the last slot in the page, the contiguous free space can be
         adjusted. Avoid future compactions as much as possible */
      if (spage_is_record_located_at_end (page_header_p, slot_p))
	{
	  page_header_p->cont_free += free_space;
	  page_header_p->offset_to_free_area -= free_space;
	}

      /* If this is the last slotid, it can be removed. Otherwise, leave it as
         unused, it will be reused when a new record is inserted */
      if (page_header_p->anchor_type != ANCHORED_DONT_REUSE_SLOTS &&
	  (slot_id + 1) == page_header_p->num_slots)
	{
	  spage_reduce_a_slot (page_header_p);
	  free_space += sizeof (SPAGE_SLOT);
	}
      else
	{
	  switch (page_header_p->anchor_type)
	    {
	    case ANCHORED:
	      slot_p->offset_to_record = NULL_OFFSET;
	      slot_p->record_type = REC_DELETED_WILL_REUSE;
	      break;
	    case ANCHORED_DONT_REUSE_SLOTS:
	      slot_p->offset_to_record = NULL_OFFSET;
	      slot_p->record_type = REC_MARKDELETED;
	      break;
	    case UNANCHORED_ANY_SEQUENCE:
	    case UNANCHORED_KEEP_SEQUENCE:
	      spage_shift_slot_down (page_p, page_header_p, slot_p);

	      spage_reduce_a_slot (page_header_p);
	      free_space += sizeof (SPAGE_SLOT);
	      break;
	    default:
	      return NULL_SLOTID;
	    }
	}

      /* Indicate that we are savings */
      if (page_header_p->is_saving
	  && spage_save_space (thread_p, page_header_p, page_p,
			       free_space) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return NULL_SLOTID;
	}
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return slot_id;
}

/*
 * spage_delete_for_recovery () - Delete the record located at given slot on the given page
 *                  (only for recovery)
 *   return: slotid on success and NULL_SLOTID on failure
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record to delete
 * 
 * Note: The slot is always reused even in anchored_dont_reuse_slots pages
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

  if (page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS)
    {
      slot_p = spage_find_slot (page_p, page_header_p, slot_id, false);
      if (slot_p->offset_to_record == NULL_OFFSET
	  && slot_p->record_type == REC_MARKDELETED)
	{
	  if ((slot_id + 1) == page_header_p->num_slots)
	    {
	      spage_reduce_a_slot (page_header_p);
	      if (page_header_p->is_saving
		  && spage_save_space (thread_p, page_header_p, page_p,
				       sizeof (SPAGE_SLOT)) != NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			  0);
		  return NULL_SLOTID;
		}
	    }
	  else
	    {
	      slot_p->record_type = REC_DELETED_WILL_REUSE;
	    }

	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	}
    }

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return slot_id;
}

static int
spage_check_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		       PGSLOTID slot_id, const RECDES * record_descriptor_p,
		       SPAGE_SLOT ** out_slot_p, int *out_space_p,
		       int *out_old_waste_p, int *out_new_waste_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int new_waste, old_waste;
  int space;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  if (record_descriptor_p->length > spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
		   old_waste);
  DB_WASTED_ALIGN (record_descriptor_p->length, page_header_p->alignment,
		   new_waste);
  space =
    record_descriptor_p->length + new_waste - slot_p->record_length -
    old_waste;

  if (spage_is_not_enough_total_space
      (thread_p, page_p, page_header_p, space))
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

static int
spage_update_record_in_place (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
			      SPAGE_SLOT * slot_p,
			      const RECDES * record_descriptor_p, int space)
{
  bool is_located_end;

  /* Update the record in place. Same area */
  is_located_end = spage_is_record_located_at_end (page_header_p, slot_p);

  slot_p->record_length = record_descriptor_p->length;
  memcpy (((char *) page_p + slot_p->offset_to_record),
	  record_descriptor_p->data, record_descriptor_p->length);
  page_header_p->total_free -= space;

  /* If the record was located at the end, we can execute a simple
     compaction */
  if (is_located_end)
    {
      page_header_p->cont_free -= space;
      page_header_p->offset_to_free_area += space;
    }

  return SP_SUCCESS;
}

static int
spage_update_record_after_compact (PAGE_PTR page_p,
				   SPAGE_HEADER * page_header_p,
				   SPAGE_SLOT * slot_p,
				   const RECDES * record_descriptor_p,
				   int space, int old_waste, int new_waste)
{
  int old_offset;

  /*
   * If record does not fit in the contiguous free area, compress the page
   * leaving the desired record at the end of the free area.
   * 
   * If the record is at the end and there is free space. Do a simple
   * compaction
   */
  if (spage_is_record_located_at_end (page_header_p, slot_p) &&
      (record_descriptor_p->length - slot_p->record_length - old_waste) <=
      page_header_p->cont_free)
    {
      old_waste += slot_p->record_length;
      spage_add_contiguous_free_space (page_header_p, old_waste);
      space = record_descriptor_p->length + new_waste;
      old_waste = 0;
    }
  else if (record_descriptor_p->length > page_header_p->cont_free)
    {
      /*
       * Full compaction: eliminate record from compaction (like a quick
       * delete). Compaction always finish with the correct amount of free
       * space.
       */
      old_offset = slot_p->offset_to_record;
      slot_p->offset_to_record = NULL_OFFSET;
      page_header_p->total_free += old_waste + slot_p->record_length;
      page_header_p->num_records--;

      if (spage_compact (page_p) != NO_ERROR)
	{
	  slot_p->offset_to_record = old_offset;
	  page_header_p->total_free -= old_waste + slot_p->record_length;
	  page_header_p->num_records++;
	  return SP_ERROR;
	}

      page_header_p->num_records++;
      space = record_descriptor_p->length + new_waste;
    }

  /* Now update the record */
  spage_set_slot (slot_p, page_header_p->offset_to_free_area,
		  record_descriptor_p->length, slot_p->record_type);
  memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	  record_descriptor_p->data, record_descriptor_p->length);

  /* Adjust the header */
  page_header_p->total_free -= space;
  page_header_p->cont_free -= record_descriptor_p->length + new_waste;
  page_header_p->offset_to_free_area +=
    record_descriptor_p->length + new_waste;

  return SP_SUCCESS;
}

/*
 * spage_update () - Update the record located at the given slot with the data
 *                described by the given record descriptor
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page 
 *   slotid(in): Slot identifier of record to update
 *   recdes(in): Pointer to a record descriptor
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

  page_header_p = (SPAGE_HEADER *) page_p;
  total_free_save = page_header_p->total_free;

  status =
    spage_check_updatable (thread_p, page_p, slot_id, record_descriptor_p,
			   &slot_p, &space, &old_waste, &new_waste);
  if (status != SP_SUCCESS)
    {
      return status;
    }

  /* If the new representation fits onto the area of the old representation,
     execute the update in place */

  if (record_descriptor_p->length <= slot_p->record_length)
    {
      status =
	spage_update_record_in_place (page_p, page_header_p, slot_p,
				      record_descriptor_p, space);
    }
  else
    {
      status =
	spage_update_record_after_compact (page_p, page_header_p, slot_p,
					   record_descriptor_p, space,
					   old_waste, new_waste);
    }

  if (status != SP_SUCCESS)
    {
      return status;
    }

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_is_updatable () - Find if there is enough area to update the record with
 *                   the given data
 *   return: true if there is enough area to update, or false
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record to update
 *   recdes(in): Pointer to a record descriptor
 */
bool
spage_is_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		    PGSLOTID slot_id, const RECDES * record_descriptor_p)
{
  if (spage_check_updatable (thread_p, page_p, slot_id, record_descriptor_p,
			     NULL, NULL, NULL, NULL) != SP_SUCCESS)
    {
      return false;
    }

  return true;
}

/*
 * spage_update_record_type () - Update the type of the record located at the 
 *                               given slot
 *   return: void
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record to update
 *   type(in): record type
 */
void
spage_update_record_type (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			  PGSLOTID slot_id, INT16 record_type)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  slot_p = spage_find_slot (page_p, NULL, slot_id, true);
  if (slot_p == NULL)
    {
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
 *                 don't resuse slots pages
 *   return: true if anything was reclaimed and false if nothing was reclaimed
 *   pgptr(in): Pointer to slotted page
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

  if (page_header_p->anchor_type == ANCHORED_DONT_REUSE_SLOTS
      && page_header_p->num_slots > 0)
    {
      first_slot_p = spage_find_slot (page_p, page_header_p, 0, false);

      /* Start backwards so we can reuse space easily */
      for (slot_id = page_header_p->num_slots - 1; slot_id >= 0; slot_id--)
	{
	  slot_p = first_slot_p - slot_id;
	  if (slot_p->offset_to_record == NULL_OFFSET &&
	      (slot_p->record_type == REC_MARKDELETED ||
	       slot_p->record_type == REC_DELETED_WILL_REUSE))
	    {
	      if ((slot_id + 1) == page_header_p->num_slots)
		{
		  spage_reduce_a_slot (page_header_p);
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
      if (page_header_p->num_slots == 0)
	{
	  /* Initialize the page to avoid future compactions */
	  spage_initialize (thread_p, page_p, page_header_p->anchor_type,
			    page_header_p->alignment,
			    page_header_p->is_saving);
	}
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return is_reclaim;
}

/*
 * spage_split () - Split the record stored at given slotid at offset location
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page 
 *   slotid(in): Slot identifier of record to update
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

  assert (page_p != NULL);
  assert (out_new_slot_id_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if (offset < 0 || offset > slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_SPLIT_WRONG_OFFSET, 3, offset, slot_p->record_length,
	      slot_id);
      /* Offset is wrong */
      *out_new_slot_id_p = NULL_SLOTID;
      return SP_ERROR;
    }


  if (spage_find_empty_slot
      (thread_p, page_p, 0, slot_p->record_type, &new_slot_p, &new_waste,
       out_new_slot_id_p) != SP_SUCCESS)
    {
      return SP_ERROR;
    }

  /* Do we need to worry about wasted space */
  remain_length = slot_p->record_length - offset;
  DB_WASTED_ALIGN (offset, page_header_p->alignment, remain_waste);
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
      DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
		       old_waste);
      DB_WASTED_ALIGN (offset, page_header_p->alignment, remain_waste);
      DB_WASTED_ALIGN (remain_length, page_header_p->alignment, new_waste);
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
	  && spage_is_not_enough_total_space (thread_p, page_p, page_header_p,
					      space))
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
	      return SP_ERROR;
	    }

	  memcpy (copyarea,
		  (char *) page_p + slot_p->offset_to_record + offset,
		  remain_length);

	  /* For now indicate that it has an empty slot */
	  new_slot_p->offset_to_record = NULL_OFFSET;
	  new_slot_p->record_length = remain_length;
	  /* New length for first part of split. */
	  slot_p->record_length = offset;

	  /* Adjust some of the space for the compaction, then return the
	     space back. That is, second part is gone for now. */
	  page_header_p->total_free += space + remain_length + new_waste;
	  page_header_p->num_records--;

	  if (spage_compact (page_p) != NO_ERROR)
	    {
	      slot_p->record_length += remain_length;
	      page_header_p->total_free -= space + remain_length + new_waste;
	      (void) spage_delete_for_recovery (thread_p, page_p,
						*out_new_slot_id_p);
	      *out_new_slot_id_p = NULL_SLOTID;

	      free_and_init (copyarea);
	      return SP_ERROR;
	    }
	  page_header_p->num_records++;

	  /* Now update the record */
	  new_slot_p->offset_to_record = page_header_p->offset_to_free_area;
	  new_slot_p->record_length = remain_length;
	  memcpy (((char *) page_p + new_slot_p->offset_to_record), copyarea,
		  remain_length);
	  /* Adjust the header */
	  spage_reduce_contiguous_free_space (page_header_p,
					      remain_length + new_waste);
	  free_and_init (copyarea);
	}
      else
	{
	  /* We can just move the second part to the end of the page */
	  memcpy (((char *) page_p + new_slot_p->offset_to_record),
		  (char *) page_p + slot_p->offset_to_record + offset,
		  remain_length);
	  new_slot_p->record_length = remain_length;
	  slot_p->record_length = offset;
	  /* Adjust the header */
	  spage_reduce_contiguous_free_space (page_header_p, space);
	}

      if (page_header_p->is_saving
	  && spage_save_space (thread_p, page_header_p, page_p,
			       page_header_p->total_free - total_free_save) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return SP_ERROR;
	}
    }

  /* set page dirty */
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */
  return SP_SUCCESS;
}

/*
 * spage_take_out () - REMOVE A PORTION OF A RECORD
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of desired record 
 *   takeout_offset(in): Location where to remove a portion of the data
 *   takeout_length(in): Length of data to rmove starting at takeout_offset
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
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((takeout_offset + takeout_length) > slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_TAKEOUT_WRONG_OFFSET, 4,
	      takeout_offset, takeout_length, slot_p->record_length, slot_id);
      return SP_ERROR;
    }

  total_free_save = page_header_p->total_free;
  DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
		   old_waste);
  DB_WASTED_ALIGN (slot_p->record_length - takeout_offset,
		   page_header_p->alignment, new_waste);
  /*
   * How to shift: The left portion to the right or
   *               the right portion to the left ?
   *
   * We shift the left portion to the right only when the left portion is
   * smaller than the right portion and we will end up with the record
   * aligned without moving the right protion (is left aligned when we
   * shifted "takeout_length" ?).
   */
  /* Check alignment of second part */
  DB_WASTED_ALIGN (slot_p->offset_to_record + takeout_length,
		   page_header_p->alignment, mayshift_left);
  if (mayshift_left == 0
      && (takeout_offset <
	  slot_p->record_length - takeout_offset - takeout_length))
    {
      /*
       * Move left part to right since we can achive alignment by moving left
       * part "takeout_length" spaces and teh left part is smaller than right
       * part.
       */
      if (takeout_offset == 0)
	{
	  /* Don't need to move anything we are choping the record from the
	     left */
	  ;
	}
      else
	{
	  memmove ((char *) page_p + slot_p->offset_to_record +
		   takeout_length, (char *) page_p + slot_p->offset_to_record,
		   takeout_offset);
	}
      slot_p->offset_to_record += takeout_length;
    }
  else
    {
      /* Move right part "takeout_length" positions to the left */
      if ((slot_p->record_length - takeout_offset - takeout_length) > 0)
	{
	  /* We are removing a portion of the record from the middle. That is, we
	     remove a portion of the record and glue the remaining two pieces */
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
  page_header_p->total_free += takeout_length + old_waste - new_waste;

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */
  return SP_SUCCESS;
}

/*
 * spage_append () - Append the data described by the given record descriptor
 *                into the record located at the given slot
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of desired record 
 *   recdes(in): Pointer to a record descriptor
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
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of desired record 
 *   offset(in): Location where to add the portion of the data
 *   recdes(in): Pointer to a record descriptor
 */
int
spage_put (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
	   int offset, const RECDES * record_descriptor_p)
{
  return spage_put_helper (thread_p, page_p, slot_id, offset,
			   record_descriptor_p, false);
}

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

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((record_descriptor_p->length + slot_p->record_length) >
      spage_max_record_size ())
    {
      return SP_DOESNT_FIT;
    }

  total_free_save = page_header_p->total_free;
  DB_WASTED_ALIGN (slot_p->record_length, page_header_p->alignment,
		   old_waste);
  DB_WASTED_ALIGN (record_descriptor_p->length + slot_p->record_length,
		   page_header_p->alignment, new_waste);
  space = record_descriptor_p->length + new_waste - old_waste;
  if (space > 0
      && spage_is_not_enough_total_space (thread_p, page_p, page_header_p,
					  space))
    {
      return SP_DOESNT_FIT;
    }

  /*
   * How to append:
   * 1) If the record is located at the end of the page and there is free
   *    space to append the new data,  do it.
   * 2) If there is space on the contiguous area to hold the old data and
   *    the new data to append. The old data is moved to the end, and the
   *    new data is appended.
   * 3) Compress the page leaving the desired record at the end of the free
   *    area and then append new data.
   */

  if (spage_is_record_located_at_end (page_header_p, slot_p)
      && space <= page_header_p->cont_free)
    {
      /*
       * The record is at the end of the page (just before contiguous free
       * space), and there is space on the contiguous free are to put in the
       * new data.
       */

      spage_add_contiguous_free_space (page_header_p, old_waste);
      if (!is_append)
	{
	  /* Move anything after offset, so we can insert the desired data */
	  memmove ((char *) page_p + slot_p->offset_to_record + offset +
		   record_descriptor_p->length,
		   (char *) page_p + slot_p->offset_to_record + offset,
		   slot_p->record_length - offset);
	}
    }
  else if (record_descriptor_p->length + slot_p->record_length <=
	   page_header_p->cont_free)
    {
      /* Move the old data to the end and remove wasted space from the old
         data, so we can append at the right place. */
      if (is_append)
	{
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  (char *) page_p + slot_p->offset_to_record,
		  slot_p->record_length);
	}
      else
	{
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  (char *) page_p + slot_p->offset_to_record, offset);
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
	  return SP_ERROR;
	}

      memcpy (copyarea, (char *) page_p + slot_p->offset_to_record,
	      slot_p->record_length);
      /* For now indicate that it has an empty slot */
      old_offset = slot_p->offset_to_record;
      slot_p->offset_to_record = NULL_OFFSET;
      page_header_p->total_free += slot_p->record_length + old_waste;
      page_header_p->num_records--;

      if (spage_compact (page_p) != NO_ERROR)
	{
	  slot_p->offset_to_record = old_offset;
	  page_header_p->total_free -= old_waste + slot_p->record_length;
	  page_header_p->num_records++;
	  free_and_init (copyarea);
	  return SP_ERROR;
	}
      page_header_p->num_records++;
      /* Move the old data to the end */
      if (is_append)
	{
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  copyarea, slot_p->record_length);
	}
      else
	{
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area,
		  copyarea, offset);
	  memcpy ((char *) page_p + page_header_p->offset_to_free_area +
		  offset + record_descriptor_p->length, copyarea + offset,
		  slot_p->record_length - offset);
	}
      free_and_init (copyarea);
      slot_p->offset_to_record = page_header_p->offset_to_free_area;
      spage_reduce_contiguous_free_space (page_header_p,
					  slot_p->record_length);
    }

  /* Now perform the put operation. */
  if (is_append)
    {
      memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  else
    {
      memcpy (((char *) page_p + slot_p->offset_to_record + offset),
	      record_descriptor_p->data, record_descriptor_p->length);
    }
  slot_p->record_length += record_descriptor_p->length;
  /* Note that we have already eliminated the old waste, so do not take it
     in consideration right now. */
  spage_reduce_contiguous_free_space (page_header_p,
				      record_descriptor_p->length +
				      new_waste);
  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */
  return SP_SUCCESS;
}

/*
 * spage_overwrite () - Overwrite a portion of the record stored at given slotid
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record to overwrite 
 *   overwrite_offset(in): Offset on the record to start the overwrite process
 *   recdes(in): New replacement data
 * 
 * Note: overwrite_offset + recdes->length must be <= length of record stored
 *       on slot.
 *       If this is not the case, you must use a combination of overwrite and
 *       append.
 */
int
spage_overwrite (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
		 int overwrite_offset, const RECDES * record_descriptor_p)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  slot_p = spage_find_slot (page_p, NULL, slot_id, true);
  if (slot_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_UNKNOWN_SLOTID, 3,
	      slot_id, pgbuf_get_page_id (page_p),
	      pgbuf_get_volume_label (page_p));
      return SP_ERROR;
    }

  if ((overwrite_offset + record_descriptor_p->length) >
      slot_p->record_length)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_SP_OVERWRITE_WRONG_OFFSET, 4,
	      overwrite_offset, record_descriptor_p->length,
	      slot_p->record_length, slot_id);
      return SP_ERROR;
    }

  memcpy (((char *) page_p + slot_p->offset_to_record + overwrite_offset),
	  record_descriptor_p->data, record_descriptor_p->length);

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */

  return SP_SUCCESS;
}

/*
 * spage_merge () - Merge the record of the second slot onto the record of the 
 *               first slot
 *   return: either of SP_ERROR, SP_DOESNT_FIT, SP_SUCCESS
 *   pgptr(in): Pointer to slotted page
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
  DB_WASTED_ALIGN (first_slot_p->record_length, page_header_p->alignment,
		   first_old_waste);
  DB_WASTED_ALIGN (second_slot_p->record_length, page_header_p->alignment,
		   second_old_waste);
  DB_WASTED_ALIGN (first_slot_p->record_length + second_slot_p->record_length,
		   page_header_p->alignment, new_waste);
  /*
   * How to append:
   * 1) If the record one is located at the end of the page and there is free
   *    space to append the second record,  do it.
   * 2) If there is space on the contiguous area to hold the first and second
   *    record. The first record is moved to the end, and then the second
   *    record is appended to it.
   * 3) Compress the page leaving the first record at the end of the free
   *    area and then append the new record.
   */
  if (spage_is_record_located_at_end (page_header_p, first_slot_p) &&
      second_slot_p->record_length <= page_header_p->cont_free)
    {
      /*
       * The first record is at the end of the page (just before contiguous free
       * space), and there is space on the contiguous free area to append the
       * second record.
       *
       * Remove the wasted space from the free spaces, so we can append at
       * the right place.
       */

      spage_add_contiguous_free_space (page_header_p, first_old_waste);
      first_old_waste = 0;
    }
  else if (first_slot_p->record_length + second_slot_p->record_length <=
	   page_header_p->cont_free)
    {
      /* Move the first data to the end and remove wasted space from the first
         record, so we can append at the right place. */
      memcpy ((char *) page_p + page_header_p->offset_to_free_area,
	      (char *) page_p + first_slot_p->offset_to_record,
	      first_slot_p->record_length);
      first_slot_p->offset_to_record = page_header_p->offset_to_free_area;
      page_header_p->offset_to_free_area += first_slot_p->record_length;

      /* Don't increase waste here */
      page_header_p->total_free -=
	first_slot_p->record_length - first_old_waste;
      page_header_p->cont_free -=
	first_slot_p->record_length - first_old_waste;
      first_old_waste = 0;
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

      memcpy (copyarea, (char *) page_p + first_slot_p->offset_to_record,
	      first_slot_p->record_length);
      memcpy (copyarea + first_slot_p->record_length,
	      (char *) page_p + second_slot_p->offset_to_record,
	      second_slot_p->record_length);

      /* Now indicate empty slots. */
      first_old_offset = first_slot_p->offset_to_record;
      second_old_offset = second_slot_p->offset_to_record;
      first_slot_p->offset_to_record = NULL_OFFSET;
      second_slot_p->offset_to_record = NULL_OFFSET;
      page_header_p->total_free +=
	first_slot_p->record_length + second_slot_p->record_length +
	first_old_waste + second_old_waste;
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
	  return SP_ERROR;
	}
      page_header_p->num_records += 2;

      /* Move the old data to the end */
      memcpy ((char *) page_p + page_header_p->offset_to_free_area, copyarea,
	      first_slot_p->record_length + second_slot_p->record_length);
      free_and_init (copyarea);

      first_slot_p->offset_to_record = page_header_p->offset_to_free_area;
      first_slot_p->record_length += second_slot_p->record_length;
      second_slot_p->record_length = 0;
      second_slot_p->offset_to_record = 0;

      spage_reduce_contiguous_free_space (page_header_p,
					  first_slot_p->record_length);
      first_old_waste = 0;
      second_old_waste = 0;
    }

  /* Now perform the append operation if needed */
  if (second_slot_p->record_length != 0)
    {
      memcpy (((char *) page_p + page_header_p->offset_to_free_area),
	      (char *) page_p + second_slot_p->offset_to_record,
	      second_slot_p->record_length);
      first_slot_p->record_length += second_slot_p->record_length;
      second_slot_p->record_length = 0;
    }

  /* Note that we have already eliminated the old waste, so do not take it
     in consideration right now. */

  spage_reduce_contiguous_free_space (page_header_p,
				      new_waste - first_old_waste -
				      second_old_waste);
  (void) spage_delete (thread_p, page_p, second_slot_id);

  if (page_header_p->is_saving
      && spage_save_space (thread_p, page_header_p, page_p,
			   page_header_p->total_free - total_free_save) !=
      NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return SP_ERROR;
    }
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
#ifdef SP_DEBUG
  spage_check (page_p);
#endif /* SP_DEBUG */
  return SP_SUCCESS;
}

static SCAN_CODE
spage_search_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		     RECDES * record_descriptor_p, int is_peeking,
		     int direction)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  PGSLOTID slot_id;

  assert (page_p != NULL);
  assert (out_slot_id_p != NULL);
  assert (record_descriptor_p != NULL);

  slot_id = *out_slot_id_p;
  page_header_p = (SPAGE_HEADER *) page_p;

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
	 && slot_p->offset_to_record == NULL_OFFSET)
    {
      slot_id += direction;
      slot_p -= direction;
    }

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
 *   pgptr(in): Pointer to slotted page 
 *   slotid(in/out): Slot identifier of current record
 *   recdes(out): Pointer to a record descriptor
 *   ispeeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 * 
 * Note: When ispeeking is PEEK, the next available record is peeked onto the
 *       page. The address of the record descriptor is set to the portion of
 *       the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted. 
 *
 *       When ispeeking is DONT_PEEK (COPY), the next available record is read
 *       onto the area pointed by the record descriptor. If the record does not
 *       fit in such an area, the length of the record is returned as a
 *       negative value in recdes->length and an error is indicated in the
 *       return value. 
 * 
 *       If the current value of slotid is negative, the first record on the
 *       page is retrieved.
 */
SCAN_CODE
spage_next_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		   RECDES * record_descriptor_p, int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_NEXT);
}

/*
 * spage_previous_record () - Get previous record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *   pgptr(in): Pointer to slotted page 
 *   slotid(out): Slot identifier of current record
 *   recdes(out): Pointer to a record descriptor
 *   ispeeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 * 
 * Note: When ispeeking is PEEK, the previous available record is peeked onto
 *       the page. The address of the record descriptor is set to the portion
 *       of the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted. 
 *
 *       When ispeeking is DONT_PEEK (COPY), the previous available record is
 *       read onto the area pointed by the record descriptor. If the record
 *       does not fit in such an area, the length of the record is returned
 *       as a negative value in recdes->length and an error is indicated in the
 *       return value. 
 * 
 *       If the current value of slotid is negative, the first record on the
 *       page is retrieved.
 */
SCAN_CODE
spage_previous_record (PAGE_PTR page_p, PGSLOTID * out_slot_id_p,
		       RECDES * record_descriptor_p, int is_peeking)
{
  return spage_search_record (page_p, out_slot_id_p, record_descriptor_p,
			      is_peeking, SPAGE_SEARCH_PREV);
}

/*
 * spage_get_record () - Get specified record
 *   return: Either of S_SUCCESS, S_DOESNT_FIT, S_END
 *   pgptr(in): Pointer to slotted page 
 *   slotid(in): Slot identifier of current record
 *   recdes(out): Pointer to a record descriptor
 *   ispeeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer)
 * 
 * Note: When ispeeking is PEEK, the desired available record is peeked onto
 *       the page. The address of the record descriptor is set to the portion
 *       of the buffer where the record is stored. Peeking a record should be
 *       executed with caution since the slotted module may decide to move
 *       the record around. In general, no other operation should be executed
 *       on the page until the peeking of the record is done. The page should
 *       be fixed and locked to avoid any funny behavior. RECORD should NEVER
 *       be MODIFIED DIRECTLY. Only reads should be performed, otherwise
 *       header information and other records may be corrupted. 
 *
 *       When ispeeking is DONT_PEEK (COPY), the desired available record is
 *       read onto the area pointed by the record descriptor. If the record
 *       does not fit in such an area, the length of the record is returned
 *       as a negative value in recdes->length and an error is indicated in the
 *       return value. 
 */
SCAN_CODE
spage_get_record (PAGE_PTR page_p, PGSLOTID slot_id,
		  RECDES * record_descriptor_p, int is_peeking)
{
  SPAGE_SLOT *sptr;

  assert (page_p != NULL);
  assert (record_descriptor_p != NULL);

  sptr = spage_find_slot (page_p, NULL, slot_id, true);
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
      if (slot_p->record_length > record_descriptor_p->area_size)
	{
	  /*
	   * DOES NOT FIT
	   * Give a hint to the user of the needed length. Hint is given as a
	   * negative value
	   */
	  record_descriptor_p->length = -slot_p->record_length;
	  return S_DOESNT_FIT;
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
 * spage_get_record_length () - Find the length of the record associated with 
 *                              the given slot on the given page
 *   return: Length of the record or -1 in case of error
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record
 */
int
spage_get_record_length (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  slot_p = spage_find_slot (page_p, NULL, slot_id, true);
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
 * spage_get_record_type () - Find the type of the record associated with the given slot
 *                 on the given page
 *   return: record type, or -1 if the given slot is invalid
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record 
 */
INT16
spage_get_record_type (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  slot_p = spage_find_slot (page_p, NULL, slot_id, true);
  if (slot_p == NULL || slot_p->record_type == REC_MARKDELETED)
    {
      return REC_UNKNOWN;
    }

  return slot_p->record_type;
}

/*
 * spage_is_slot_exist () - Find if there is a valid record in given slot
 *   return: true/false
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of record
 */
bool
spage_is_slot_exist (PAGE_PTR page_p, PGSLOTID slot_id)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  slot_p = spage_find_slot (page_p, NULL, slot_id, true);
  return (slot_p == NULL
	  || slot_p->record_type == REC_MARKDELETED) ? false : true;
}

static const char *
spage_record_type_string (INT16 record_type)
{
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
    default:
      return "UNKNOWN";
    }
}

static const char *
spage_anchor_flag_string (INT16 anchor_type)
{
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
      return "UNKNOWN";
    }
}

static const char *
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
 * sp_dump_hdr () - Dump an slotted page header
 *   return: void
 *   sphdr(in): Pointer to header of slotted page 
 * 
 * Note: This function is used for debugging purposes. 
 */
static void
spage_dump_header (const SPAGE_HEADER * page_header_p)
{
  assert (page_header_p != NULL);

  /* Dump header information */
  (void) fprintf (stdout,
		  "NUM SLOTS = %d, NUM RECS = %d, TYPE OF SLOTS = %s,\n",
		  page_header_p->num_slots, page_header_p->num_records,
		  spage_anchor_flag_string (page_header_p->anchor_type));
  (void) fprintf (stdout,
		  "ALIGNMENT-TO = %s, WASTED AREA FOR ALIGNMENT = %d,\n",
		  spage_alignment_string (page_header_p->alignment),
		  page_header_p->waste_align);
  (void) fprintf (stdout,
		  "TOTAL FREE AREA = %d, CONTIGUOUS FREE AREA = %d,"
		  " FREE SPACE OFFSET = %d,\n", page_header_p->total_free,
		  page_header_p->cont_free,
		  page_header_p->offset_to_free_area);
  (void) fprintf (stdout,
		  "IS_SAVING = %d, LAST TRANID SAVING = %d,"
		  " LOCAL TRANSACTION_SAVINGS = %d\n",
		  page_header_p->is_saving, page_header_p->last_tranid,
		  page_header_p->saved);
  (void) fprintf (stdout, "TOTAL_SAVED = %d\n", page_header_p->total_saved);
}

/*
 * sp_dump_sptr () - Dump the slotted page array
 *   return: void
 *   sptr(in): Pointer to slotted page pointer array
 *   nslots(in): Number of slots
 *   alignment(in): Alignment for records
 * 
 * Note: The content of the record is not dumped by this function.
 *       This function is used for debugging purposes.
 */
static void
spage_dump_slots (const SPAGE_SLOT * slot_p, PGNSLOTS num_slots,
		  unsigned short alignment)
{
  int i;
  unsigned int waste;

  assert (slot_p != NULL);

  for (i = 0; i < num_slots; slot_p--, i++)
    {
      (void) fprintf (stdout, "\nSlot-id = %2d, offset = %4d, type = %s",
		      i, slot_p->offset_to_record,
		      spage_record_type_string (slot_p->record_type));
      if (slot_p->offset_to_record != NULL_OFFSET)
	{
	  DB_WASTED_ALIGN (slot_p->record_length, alignment, waste);
	  (void) fprintf (stdout, ", length = %4d, waste = %u",
			  slot_p->record_length, waste);
	}
      (void) fprintf (stdout, "\n");
    }
}

static void
spage_dump_record (PAGE_PTR page_p, PGSLOTID slot_id, SPAGE_SLOT * slot_p)
{
  VFID *vfid;
  OID *oid;
  char *record_p;
  int i;

  if (slot_p->offset_to_record != NULL_OFFSET)
    {
      (void) fprintf (stdout, "\nSlot-id = %2d\n", slot_id);
      switch (slot_p->record_type)
	{
	case REC_BIGONE:
	  vfid = (VFID *) (page_p + slot_p->offset_to_record);
	  fprintf (stdout, "VFID = %d|%d\n", vfid->volid, vfid->fileid);
	  break;

	case REC_RELOCATION:
	  oid = (OID *) (page_p + slot_p->offset_to_record);
	  fprintf (stdout, "OID = %d|%d|%d\n",
		   oid->volid, oid->pageid, oid->slotid);
	  break;

	default:
	  record_p = (char *) page_p + slot_p->offset_to_record;
	  for (i = 0; i < slot_p->record_length; i++)
	    {
	      (void) fputc (*record_p++, stdout);
	    }
	  (void) fprintf (stdout, "\n");
	}
    }
  else
    {
      (void) fprintf (stdout, "\nSlot-id = %2d has been deleted\n", slot_id);
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
spage_dump (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int is_record_printed)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int i;

  assert (page_p != NULL);

  (void) fprintf (stdout,
		  "\n*** Dumping pageid = %d of volume = %s ***\n",
		  pgbuf_get_page_id (page_p),
		  pgbuf_get_volume_label (page_p));

  page_header_p = (SPAGE_HEADER *) page_p;
  spage_dump_header (page_header_p);

  /* Dump each slot and its corresponding record */
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  spage_dump_slots (slot_p, page_header_p->num_slots,
		    page_header_p->alignment);

  if (is_record_printed)
    {
      (void) fprintf (stdout, "\nRecords in ascii follow ...\n");
      for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
	{
	  spage_dump_record (page_p, i, slot_p);
	}
    }

  spage_dump_saved_spaces_by_other_trans (thread_p,
					  pgbuf_get_vpid_ptr (page_p));
  spage_check (thread_p, page_p);
}

/*
 * sp_check () - Check consistency of page. This function is used for
 *               debugging purposes
 *   return: void
 *   pgptr(in): Pointer to slotted page
 */
static void
spage_check (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  SPAGE_HEADER *page_header_p;
  SPAGE_SLOT *slot_p;
  int used_length = 0;
  int i;

  assert (page_p != NULL);

  page_header_p = (SPAGE_HEADER *) page_p;
  slot_p = spage_find_slot (page_p, page_header_p, 0, false);
  used_length =
    (sizeof (SPAGE_HEADER) + sizeof (SPAGE_SLOT) * page_header_p->num_slots);

  for (i = 0; i < page_header_p->num_slots; slot_p--, i++)
    {
      if (slot_p->offset_to_record != NULL_OFFSET)
	{
	  used_length += slot_p->record_length;
	  DB_ALIGN (used_length, page_header_p->alignment);
	}
    }

  if (used_length + page_header_p->total_free > DB_PAGESIZE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "sp_check: Inconsistent page = %d of volume = %s.\n"
		    "(Used_space + tfree > DB_PAGESIZE\n (%d + %d) > %d \n "
		    " %d > %d\n",
		    pgbuf_get_page_id (page_p),
		    pgbuf_get_volume_label (page_p), used_length,
		    page_header_p->total_free, DB_PAGESIZE,
		    used_length + page_header_p->total_free, DB_PAGESIZE);
    }

  if ((page_header_p->cont_free + page_header_p->offset_to_free_area +
       sizeof (SPAGE_SLOT) * page_header_p->num_slots) > DB_PAGESIZE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "sp_check: Inconsistent page = %d of volume = %s.\n"
		    " (cfree + foffset + SIZEOF(SP_SLOT) * nslots) > "
		    " DB_PAGESIZE\n (%d + %d + (%d * %d)) > %d\n %d > %d\n",
		    pgbuf_get_page_id (page_p),
		    pgbuf_get_volume_label (page_p), page_header_p->cont_free,
		    page_header_p->offset_to_free_area, sizeof (SPAGE_SLOT),
		    page_header_p->num_slots, DB_PAGESIZE,
		    (page_header_p->cont_free +
		     page_header_p->offset_to_free_area +
		     sizeof (SPAGE_SLOT) * page_header_p->num_slots),
		    DB_PAGESIZE);
    }

  if (page_header_p->cont_free <= (int) -(page_header_p->alignment - 1))
    {
      er_log_debug (ARG_FILE_LINE,
		    "sp_check: Cfree %d is inconsistent in page = %d"
		    " of volume = %s. Cannot be < -%d\n",
		    page_header_p->cont_free, pgbuf_get_page_id (page_p),
		    pgbuf_get_volume_label (page_p),
		    page_header_p->alignment);
    }

  /* Update any savings, before we check for any incosistencies */
  if ((page_header_p)->is_saving)
    {
      if (spage_get_saved_spaces_by_other_trans
	  (thread_p, page_header_p, page_p) < 0
	  || page_header_p->total_saved > page_header_p->total_free)
	{
	  er_log_debug (ARG_FILE_LINE,
			"sp_check: Total savings of %d is inconsistent in page = %d"
			" of volume = %s. Cannot be > total free (i.e., %d)\n",
			page_header_p->total_saved,
			pgbuf_get_page_id (page_p),
			pgbuf_get_volume_label (page_p),
			page_header_p->total_free);
	}

      if (page_header_p->total_saved < 0)
	{
	  er_log_debug (ARG_FILE_LINE,
			"sp_check: Total savings of %d is inconsistent in page = %d"
			" of volume = %s. Cannot be < 0\n",
			page_header_p->total_saved,
			pgbuf_get_page_id (page_p),
			pgbuf_get_volume_label (page_p));
	}
    }
}

/*
 * spage_check_slot_owner () -
 *   return: 
 *   pgptr(in):
 *   slotid(in):
 */
int
spage_check_slot_owner (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			PGSLOTID slot_id)
{
  SPAGE_SLOT *slot_p;
  TRANID tranid;

  assert (page_p != NULL);

  tranid = logtb_find_current_tranid (thread_p);
  slot_p = spage_find_slot (page_p, NULL, slot_id, false);

  return (*(TRANID *) (page_p + slot_p->offset_to_record) == tranid);
}

static bool
spage_is_unknown_slot (PGSLOTID slot_id, SPAGE_HEADER * page_header_p,
		       SPAGE_SLOT * slot_p)
{
  assert (page_header_p != NULL);
  assert (slot_p != NULL);

  return (slot_id < 0 || slot_id >= page_header_p->num_slots
	  || slot_p->offset_to_record == NULL_OFFSET);
}

static SPAGE_SLOT *
spage_find_slot (PAGE_PTR page_p, SPAGE_HEADER * page_header_p,
		 PGSLOTID slot_id, bool is_unknown_slot_check)
{
  SPAGE_SLOT *slot_p;

  assert (page_p != NULL);

  if (is_unknown_slot_check && page_header_p == NULL)
    {
      page_header_p = (SPAGE_HEADER *) page_p;
    }

  slot_p = (SPAGE_SLOT *) (page_p + DB_PAGESIZE - sizeof (SPAGE_SLOT));
  slot_p -= slot_id;

  if (is_unknown_slot_check
      && spage_is_unknown_slot (slot_id, page_header_p, slot_p))
    {
      return NULL;
    }

  return slot_p;
}

static bool
spage_is_not_enough_total_space (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				 SPAGE_HEADER * page_header_p, int space)
{
  assert (page_p != NULL);
  assert (page_header_p != NULL);

  return ((space > page_header_p->total_free - page_header_p->total_saved) &&
	  (space >
	   page_header_p->total_free -
	   SPAGE_GET_SAVED_SPACES_BY_OTHER_TRANS (thread_p, page_header_p,
						  page_p)));
}

static bool
spage_is_not_enough_contiguous_space (PAGE_PTR page_p,
				      SPAGE_HEADER * page_header_p, int space)
{
  assert (page_p != NULL);
  assert (page_header_p != NULL);

  return (space > page_header_p->cont_free
	  && spage_compact (page_p) != NO_ERROR);
}

static void
spage_add_contiguous_free_space (SPAGE_HEADER * page_header_p, int space)
{
  assert (page_header_p != NULL);

  page_header_p->total_free += space;
  page_header_p->cont_free += space;
  page_header_p->offset_to_free_area -= space;
}

static void
spage_reduce_contiguous_free_space (SPAGE_HEADER * page_header_p, int space)
{
  assert (page_header_p != NULL);

  page_header_p->total_free -= space;
  page_header_p->cont_free -= space;
  page_header_p->offset_to_free_area += space;
}
