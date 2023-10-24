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
#include "vacuum.h"

enum
{
  ANCHORED = 1,
  ANCHORED_DONT_REUSE_SLOTS = 2,
  UNANCHORED_ANY_SEQUENCE = 3,
  UNANCHORED_KEEP_SEQUENCE = 4
};

/* Some platform like windows used their own SP_ERROR. */
#ifdef SP_ERROR
#undef SP_ERROR
#endif

#define SP_ERROR      (-1)
#define SP_SUCCESS     (1)
#define SP_DOESNT_FIT  (3)

#define SAFEGUARD_RVSPACE      true
#define DONT_SAFEGUARD_RVSPACE false

/* Slotted page header flags */
#define SPAGE_HEADER_FLAG_NONE		0x0	/* No flags */
#define SPAGE_HEADER_FLAG_ALL_VISIBLE	0x1	/* All records are visible */	/* unused */

#define SPAGE_SLOT_SIZE   (sizeof(SPAGE_SLOT))
#define SPAGE_HEADER_SIZE (sizeof(SPAGE_HEADER))

typedef struct spage_header SPAGE_HEADER;
struct spage_header
{
  PGNSLOTS num_slots;		/* Number of allocated slots for the page */
  PGNSLOTS num_records;		/* Number of records on page */
  INT16 anchor_type;		/* Valid ANCHORED, ANCHORED_DONT_REUSE_SLOTS UNANCHORED_ANY_SEQUENCE,
				 * UNANCHORED_KEEP_SEQUENCE */
  unsigned short alignment;	/* Alignment for records: Valid values sizeof char, short, int, double */
  int total_free;		/* Total free space on page */
  int cont_free;		/* Contiguous free space on page */
  int offset_to_free_area;	/* Byte offset from the beginning of the page to the first free byte area on the page. */
  int reserved1;
  int flags;			/* Page flags: Always SPAGE_HEADER_FLAG_NONE, not currently used */
  unsigned int is_saving:1;	/* True if saving is need for recovery (undo) */
  unsigned int need_update_best_hint:1;	/* True if we should update best pages hint for this page. See
					 * heap_stats_update. */

  /* The followings are reserved for future use. */
  /* SPAGE_HEADER should be 8 bytes aligned. Packing of bit fields depends on compiler's behavior. It's better to use
   * 4-bytes type in order not to be affected by the compiler. */
  unsigned int reserved_bits:30;
};

/* 4-byte disk storage slot design */
typedef struct spage_slot SPAGE_SLOT;
struct spage_slot
{
  unsigned int offset_to_record:14;	/* Byte Offset from the beginning of the page to the beginning of the record */
  unsigned int record_length:14;	/* Length of record */
  unsigned int record_type:4;	/* Record type (REC_HOME, REC_NEWHOME, ...) described by slot. */
};

extern void spage_boot (THREAD_ENTRY * thread_p);
extern void spage_finalize (THREAD_ENTRY * thread_p);
extern void spage_free_saved_spaces (THREAD_ENTRY * thread_p, void *first_save_entry);

extern int spage_get_free_space (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern int spage_get_free_space_without_saving (THREAD_ENTRY * thread_p, PAGE_PTR page_p, bool * need_update);
extern void spage_set_need_update_best_hint (THREAD_ENTRY * thread_p, PAGE_PTR page_p, bool need_update);
extern PGNSLOTS spage_number_of_records (PAGE_PTR pgptr);
extern PGNSLOTS spage_number_of_slots (PAGE_PTR pgptr);
extern void spage_initialize (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, INT16 slots_type, unsigned short alignment,
			      bool safeguard_rvspace);
extern int spage_insert (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const RECDES * recdes, PGSLOTID * slotid);
extern int spage_insert_at (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, const RECDES * recdes);
extern int spage_insert_for_recovery (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, const RECDES * recdes);
extern PGSLOTID spage_delete (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid);
extern PGSLOTID spage_delete_for_recovery (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid);
extern int spage_update (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, const RECDES * recdes);
extern void spage_update_record_type (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, INT16 type);
extern bool spage_is_updatable (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, int recdes_length);
extern bool spage_is_mvcc_updatable (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id,
				     int delete_record_length, int insert_record_length);
extern bool spage_reclaim (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern int spage_split (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, int offset, PGSLOTID * new_slotid);
extern int spage_append (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, const RECDES * recdes);
extern int spage_take_out (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, int takeout_offset,
			   int takeout_length);
extern int spage_put (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, int offset, const RECDES * recdes);
extern int spage_overwrite (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, int overwrite_offset,
			    const RECDES * recdes);
extern int spage_merge (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid1, PGSLOTID slotid2);
extern SCAN_CODE spage_next_record (PAGE_PTR pgptr, PGSLOTID * slotid, RECDES * recdes, int ispeeking);
extern SCAN_CODE spage_next_record_dont_skip_empty (PAGE_PTR pgptr, PGSLOTID * slotid, RECDES * recdes, int ispeeking);
extern SCAN_CODE spage_previous_record (PAGE_PTR pgptr, PGSLOTID * slotid, RECDES * recdes, int ispeeking);
extern SCAN_CODE spage_previous_record_dont_skip_empty (PAGE_PTR pgptr, PGSLOTID * slotid, RECDES * recdes,
							int ispeeking);
extern SCAN_CODE spage_get_page_header_info (PAGE_PTR page_p, DB_VALUE ** page_header_info);
extern SCAN_CODE spage_get_record (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid, RECDES * recdes,
				   int ispeeking);
extern bool spage_is_slot_exist (PAGE_PTR pgptr, PGSLOTID slotid);
extern void spage_dump (THREAD_ENTRY * thread_p, FILE * fp, PAGE_PTR pgptr, int isrecord_printed);
extern SPAGE_SLOT *spage_get_slot (PAGE_PTR page_p, PGSLOTID slot_id);
#if !defined(NDEBUG)
extern bool spage_check_num_slots (THREAD_ENTRY * thread_p, PAGE_PTR page_p);
#endif
extern int spage_check (THREAD_ENTRY * thread_p, PAGE_PTR page_p);
extern int spage_get_record_length (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid);
extern int spage_get_record_offset (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id);
extern int spage_get_space_for_record (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id);
extern INT16 spage_get_record_type (PAGE_PTR pgptr, PGSLOTID slotid);
extern int spage_max_space_for_new_record (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern void spage_collect_statistics (PAGE_PTR pgptr, int *npages, int *nrecords, int *rec_length);
extern int spage_max_record_size (void);
extern int spage_check_slot_owner (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGSLOTID slotid);
extern bool spage_is_slotted_page_type (PAGE_TYPE ptype);
extern int spage_compact (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern bool spage_is_valid_anchor_type (const INT16 anchor_type);
extern const char *spage_anchor_flag_string (const INT16 anchor_type);
extern const char *spage_alignment_string (unsigned short alignment);
extern int spage_mark_deleted_slot_as_reusable (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slot_id);

extern PGSLOTID spage_find_free_slot (PAGE_PTR page_p, SPAGE_SLOT ** out_slot_p, PGSLOTID start_id);


extern int spage_header_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				    void **ctx);
extern SCAN_CODE spage_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
					 void *ctx);
extern int spage_header_end_scan (THREAD_ENTRY * thread_p, void **ctx);

extern int spage_slots_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				   void **ctx);
extern SCAN_CODE spage_slots_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
					void *ctx);
extern int spage_slots_end_scan (THREAD_ENTRY * thread_p, void **ctx);

extern void spage_vacuum_slot (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slotid, bool reusable);
extern bool spage_need_compact (THREAD_ENTRY * thread_p, PAGE_PTR page_p);
#endif /* _SLOTTED_PAGE_H_ */
