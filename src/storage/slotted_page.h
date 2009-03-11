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
 * slotted_page.h - SLOTTED PAGE MANAGEMENT MODULE (AT SERVER)
 */

#ifndef _SLOTTED_PAGE_H_
#define _SLOTTED_PAGE_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "log_manager.h"

#define PEEK          true	/* Peek for a slotted record */
#define COPY          false	/* Don't peek, but copy a slotted record */
#define DONT_PEEK     COPY	/* Same than copy */

#define ANCHORED                  1
#define ANCHORED_DONT_REUSE_SLOTS 2
#define UNANCHORED_ANY_SEQUENCE   3
#define UNANCHORED_KEEP_SEQUENCE  4

#define REC_UNKNOWN           -1	/* Unknown record type */
#define REC_ASSIGN_ADDRESS     1	/* Record without content, just the address */
#define REC_HOME               2	/* Home of record */
#define REC_NEWHOME            3	/* No the original home of record.
					   part of relocation process */
#define REC_RELOCATION         4	/* Record describe new home of record */
#define REC_BIGONE             5	/* Record describe location of big record */
#define REC_MARKDELETED        6	/* Slot does not describe any record.
					   A record was stored in this slot. Slot
					   cannot be reused. */
#define REC_DELETED_WILL_REUSE 7	/* Slot does not describe any record. A
					   record was stored in this slot. Slot
					   will be reused. */

/* Some platform like windows used their own SP_ERROR. */
#ifdef SP_ERROR
#undef SP_ERROR
#endif

#define SP_ERROR      1
#define SP_DOESNT_FIT 2
#define SP_SUCCESS    3

#define SAFEGUARD_RVSPACE      true
#define DONT_SAFEGUARD_RVSPACE false

typedef struct spage_slot SPAGE_SLOT;
struct spage_slot
{
  int offset_to_record;		/* Byte Offset from the beginning of the page
				   to the beginning of the record */
  int record_length;		/* Length of record */
  INT16 record_type;		/* Record type (REC_HOME, REC_NEWHOME,
				   REC_RELOCATION, REC_BIGONE, REC_MARKDELETED)
				   described by slot. */
};

extern int spage_boot (THREAD_ENTRY * thread_p);
extern void spage_finalize (THREAD_ENTRY * thread_p);
extern void spage_free_saved_spaces (THREAD_ENTRY * thread_p, TRANID tranid);
extern int spage_slot_size (void);
extern int spage_header_size (void);
extern int spage_get_free_space (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern PGNSLOTS spage_number_of_records (PAGE_PTR pgptr);
extern void spage_initialize (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			      INT16 slots_type, unsigned short alignment,
			      bool safeguard_rvspace);
extern int spage_insert (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			 RECDES * recdes, PGSLOTID * slotid);
extern int spage_find_slot_for_insert (THREAD_ENTRY * thread_p,
				       PAGE_PTR pgptr, RECDES * recdes,
				       PGSLOTID * slotid, void **slotptr,
				       int *used_space);
extern int spage_insert_data (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			      RECDES * recdes, void *slotptr, int used_space);
extern int spage_insert_at (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			    PGSLOTID slotid, RECDES * recdes);
extern int spage_insert_for_recovery (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				      PGSLOTID slotid, RECDES * recdes);
extern PGSLOTID spage_delete (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			      PGSLOTID slotid);
extern PGSLOTID spage_delete_for_recovery (THREAD_ENTRY * thread_p,
					   PAGE_PTR pgptr, PGSLOTID slotid);
extern int spage_update (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			 PGSLOTID slotid, const RECDES * recdes);
extern void spage_update_record_type (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				      PGSLOTID slotid, INT16 type);
extern bool spage_is_updatable (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				PGSLOTID slotid, const RECDES * recdes);
extern bool spage_reclaim (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern int spage_split (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			PGSLOTID slotid, int offset, PGSLOTID * new_slotid);
extern int spage_append (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			 PGSLOTID slotid, const RECDES * recdes);
extern int spage_take_out (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			   PGSLOTID slotid, int takeout_offset,
			   int takeout_length);
extern int spage_put (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
		      PGSLOTID slotid, int offset, const RECDES * recdes);
extern int spage_overwrite (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			    PGSLOTID slotid, int overwrite_offset,
			    const RECDES * recdes);
extern int spage_merge (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			PGSLOTID slotid1, PGSLOTID slotid2);
extern SCAN_CODE spage_next_record (PAGE_PTR pgptr, PGSLOTID * slotid,
				    RECDES * recdes, int ispeeking);
extern SCAN_CODE spage_previous_record (PAGE_PTR pgptr, PGSLOTID * slotid,
					RECDES * recdes, int ispeeking);
extern SCAN_CODE spage_get_record (PAGE_PTR pgptr, PGSLOTID slotid,
				   RECDES * recdes, int ispeeking);
extern bool spage_is_slot_exist (PAGE_PTR pgptr, PGSLOTID slotid);
extern void spage_dump (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			int isrecord_printed);
extern int spage_get_record_length (PAGE_PTR pgptr, PGSLOTID slotid);
extern INT16 spage_get_record_type (PAGE_PTR pgptr, PGSLOTID slotid);
extern int spage_max_space_for_new_record (THREAD_ENTRY * thread_p,
					   PAGE_PTR pgptr);
extern int spage_count_pages (PAGE_PTR pgptr,
			      const PGSLOTID dont_count_slotid);
extern int spage_count_records (PAGE_PTR pgptr,
				const PGSLOTID dont_count_slotid);
extern int spage_sum_length_of_records (PAGE_PTR pgptr,
					const PGSLOTID dont_count_slotid);
extern int spage_max_record_size (void);
extern int spage_check_slot_owner (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				   PGSLOTID slotid);

#endif /* _SLOTTED_PAGE_H_ */
