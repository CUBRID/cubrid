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
 * extendible_hash.c - Extendible hash manager
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#if defined(sun) || defined(HPUX)
#include <sys/types.h>
#include <netinet/in.h>
#endif
#if defined(_AIX)
#include <net/nh.h>
#endif

#include "chartype.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "error_manager.h"
#include "xserver_interface.h"
#include "log_manager.h"
#include "extendible_hash.h"
#include "page_buffer.h"
#include "lock_manager.h"
#include "slotted_page.h"
#include "file_manager.h"
#include "overflow_file.h"
#include "memory_hash.h"	/* For hash functions */
#include "tz_support.h"
#include "db_date.h"
#include "thread_compat.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#ifdef EHASH_DEBUG
#define EHASH_BALANCE_FACTOR     4	/* Threshold rate of no. of directory pointers over no. of bucket pages. If
					 * this threshold is exceeded, a warning is issued in the debugging mode */
#endif /* EHASH_DEBUG */

  /*
   * NOTICE: The constants EHASH_OVERFLOW_RATE and UNDERFLOW_RATE must be
   * less than 1. The following values are very appropriate. Avoid changing
   * them unless absolutely necessary.
   */

#define EHASH_OVERFLOW_RATE     0.9	/* UPPER THRESHOLD for a merge operation. */

  /*
   * After a bucket merge operation, up to what percent of the sibling bucket
   * space (i.e. DB_PAGESIZE) can be full. If it is found that during a merge
   * the sibling bucket will become too full the merge is delayed to avoid an
   * immediate split on the sibling bucket. (Exception: if the bucket becomes
   * completely empty, the merge operation is performed no matter how full the
   * sibling bucket is). That is we try to avoid a yoyo behaviour (split,
   * merge).
   */

#define EHASH_UNDERFLOW_RATE     0.4	/* LOWER THRESHOLD for a merge operation. */

  /*
   * After a deletion operation, if the remaining records occupy less than
   * this rate of bucket space (i.e. DB_PAGESIZE) then the bucket is tried to
   * be merged with its sibling bucket.
   */

/* Conversion of these rates into absolute values */
#define EHASH_OVERFLOW_THRESHOLD   (EHASH_OVERFLOW_RATE * DB_PAGESIZE)
#define EHASH_UNDERFLOW_THRESHOLD  (EHASH_UNDERFLOW_RATE * DB_PAGESIZE)

/* Number of bits pseudo key consists of */
#define EHASH_HASH_KEY_BITS           (sizeof(EHASH_HASH_KEY) * 8)

/* Number of bits the short data type consists of */
#define EHASH_SHORT_BITS  (sizeof(short) * 8)

typedef unsigned int EHASH_HASH_KEY;	/* Pseudo_key type */

/* Extendible hashing directory header */
typedef struct ehash_dir_header EHASH_DIR_HEADER;
struct ehash_dir_header
{
  /* Fields should be ordered according to their sizes */
  VFID bucket_file;		/* bucket file identifier */
  VFID overflow_file;		/* overflow (buckets) file identifier */

  /* Each one keeps the number of buckets having a local depth equal to the index value of counter. Used for noticing
   * directory shrink condition */
  int local_depth_count[EHASH_HASH_KEY_BITS + 1];

  DB_TYPE key_type;		/* type of the keys */
  short depth;			/* global depth of the directory */
  char alignment;		/* alignment value used on slots of bucket pages */
};

/* Directory elements : Pointer to the bucket */
typedef struct ehash_dir_record EHASH_DIR_RECORD;
struct ehash_dir_record
{
  VPID bucket_vpid;		/* bucket pointer */
};

/* Each bucket has a header area to store its local depth */
typedef struct ehash_bucket_header EHASH_BUCKET_HEADER;
struct ehash_bucket_header
{
  char local_depth;		/* The local depth of the bucket */
};

typedef enum
{
  EHASH_SUCCESSFUL_COMPLETION,
  EHASH_BUCKET_FULL,		/* Bucket full condition; used in insertion */
  EHASH_BUCKET_UNDERFLOW,	/* Bucket underflow condition; used in deletion */
  EHASH_BUCKET_EMPTY,		/* Bucket empty condition; used in deletion */
  EHASH_FULL_SIBLING_BUCKET,
  EHASH_NO_SIBLING_BUCKET,
  EHASH_ERROR_OCCURRED
} EHASH_RESULT;

/* Recovery structures */
typedef struct ehash_repetition EHASH_REPETITION;
struct ehash_repetition
{
  /* The "vpid" is repeated "count" times */
  VPID vpid;
  int count;
};

/* Definitions about long strings */
#define EHASH_LONG_STRING_PREFIX_SIZE   10

/* Directory header size is aligned to size of integer */
#define EHASH_DIR_HEADER_SIZE \
  ((ssize_t) (((sizeof(EHASH_DIR_HEADER) + sizeof(int) - 1 ) / sizeof(int) ) * sizeof(int)))

/* Maximum size of a string key; 16 is for two slot indices */
#define EHASH_MAX_STRING_SIZE \
  (DB_PAGESIZE - (SSIZEOF(EHASH_BUCKET_HEADER) + 16))

/* Number of pointers the first page of the directory contains */
#define EHASH_NUM_FIRST_PAGES \
  ((DB_PAGESIZE - EHASH_DIR_HEADER_SIZE) / SSIZEOF (EHASH_DIR_RECORD))

/* Offset of the last pointer in the first directory page */
#define EHASH_LAST_OFFSET_IN_FIRST_PAGE \
  (EHASH_DIR_HEADER_SIZE + (EHASH_NUM_FIRST_PAGES - 1) * sizeof(EHASH_DIR_RECORD))

/* Number of pointers for each directory page (other than the first one)  */
#define EHASH_NUM_NON_FIRST_PAGES \
  (DB_PAGESIZE / sizeof(EHASH_DIR_RECORD))

/* Offset of the last pointer in the other directory pages */
#define EHASH_LAST_OFFSET_IN_NON_FIRST_PAGE \
  ((EHASH_NUM_NON_FIRST_PAGES - 1) * sizeof(EHASH_DIR_RECORD))

/*
 * GETBITS
 *
 *   value: value from which bits are extracted; its type should be
 *          "unsigned int"
 *	 pos: first bit position (left adjusted) of the field
 *	 n: length of the field to be extracted
 *
 * Note: Returns the n-bit field of key that begins at left adjusted
 * position pos. The type of key is supposed to be unsigned
 * so that when it is right-shifted vacated bits will be filled
 * with zeros, not sign bits.
 */
/* TODO: ~0: M2 64-bit */
#define GETBITS(value, pos, n) \
  ( ((value) >> ( EHASH_HASH_KEY_BITS - (pos) - (n) + 1)) & (~(~0UL << (n))) )
  /* Plus 1 since bits are numbered from 1 to 32 */

/*
 * FIND_OFFSET
 *
 *   hash_key: pseudo key to map to a directory pointer
 *	 depth: depth of directory: tells how many bits to use
 *
 * Note: This macro maps a hash_key to an entry in a directory of one
 * area. It extracts first "depth" bits from the left side of
 *       "hash_key" and returns this value.
 *
 */

#define FIND_OFFSET(hash_key, depth) (GETBITS((hash_key), 1, (depth)))

/*
 * GETBIT
 *   return: int
 *   word: computer word from which the bit is extracted.
 *         Its type should be "unsigned int".
 *   pos: bit position (left adjusted) of word to return
 *
 *
 * Note: Returns the value of the bit (in "pos" position) of a "word".
 *
 */

#define GETBIT(word, pos) (GETBITS((word), (pos), 1))


/*
 * SETBIT
 *
 *   word: computer word whose bit is set
 *   pos: bit position (left adjusted) of word to be set to
 *
 * Note: Returns a value identical to "word" except the "pos" bit
 * is set to one.
 */

#define SETBIT(word,  pos) ( (word) | (1 << (EHASH_HASH_KEY_BITS - (pos))) )

/*
 * CLEARBIT
 *
 *   word: computer word whose bit is to be cleared
 *         Nth_most_significant_bit: bit position of word to be set to
 *   key_side: which side of the word should be used
 *
 * Note: Returns a value identical to "word" except the "pos" bit
 * is cleared (zeroed).
 */

#define CLEARBIT(word, pos) ( (word) & ~(1 << (EHASH_HASH_KEY_BITS - (pos))) )

#if defined (ENABLE_UNUSED_FUNCTION)
static int ehash_ansi_sql_strncmp (const char *s, const char *t, int max);
#endif
static char *ehash_allocate_recdes (RECDES * recdes, int size, short type);
static void ehash_free_recdes (RECDES * recdes);
#if defined (ENABLE_UNUSED_FUNCTION)
static int ehash_compare_overflow (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, char *key, int *comp_result);
static char *ehash_compose_overflow (THREAD_ENTRY * thread_p, RECDES * recdes);
#endif
static int ehash_initialize_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *alignment_depth);
static int ehash_initialize_dir_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args);
static short ehash_get_key_size (DB_TYPE key_type);
static EHID *ehash_create_helper (THREAD_ENTRY * thread_p, EHID * ehid, DB_TYPE key_type, int exp_num_entries,
				  OID * class_oid, int attr_id, bool istmp);
static PAGE_PTR ehash_fix_old_page (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid,
				    PGBUF_LATCH_MODE mode);
static PAGE_PTR ehash_fix_ehid_page (THREAD_ENTRY * thread_p, EHID * ehid, PGBUF_LATCH_MODE latch_mode);
static PAGE_PTR ehash_fix_nth_page (THREAD_ENTRY * thread_p, const VFID * vfid, int offset, PGBUF_LATCH_MODE mode);
static void *ehash_insert_helper (THREAD_ENTRY * thread_p, EHID * ehid, void *key, OID * value_ptr, int lock_type,
				  VPID * existing_ovf_vpid);
static EHASH_RESULT ehash_insert_to_bucket (THREAD_ENTRY * thread_p, EHID * ehid, VFID * ovf_file, bool is_temp,
					    PAGE_PTR buc_pgptr, DB_TYPE key_type, void *key_ptr, OID * value_ptr,
					    VPID * existing_ovf_vpid);
static int ehash_compose_record (DB_TYPE key_type, void *key_ptr, OID * value_ptr, RECDES * recdes);
static bool ehash_locate_slot (THREAD_ENTRY * thread_p, PAGE_PTR page, DB_TYPE key_type, void *key,
			       PGSLOTID * position);
static PAGE_PTR ehash_split_bucket (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header, PAGE_PTR buc_pgptr,
				    void *key, int *old_local_depth, int *new_local_depth, VPID * sib_vpid,
				    bool is_temp);
static int ehash_expand_directory (THREAD_ENTRY * thread_p, EHID * ehid, int new_depth, bool is_temp);
static int ehash_connect_bucket (THREAD_ENTRY * thread_p, EHID * ehid, int local_depth, EHASH_HASH_KEY hash_key,
				 VPID * buc_vpid, bool is_temp);
static char ehash_find_depth (THREAD_ENTRY * thread_p, EHID * ehid, int location, VPID * vpid, VPID * sib_vpid);
static void ehash_merge (THREAD_ENTRY * thread_p, EHID * ehid, void *key, bool is_temp);
static void ehash_shrink_directory (THREAD_ENTRY * thread_p, EHID * ehid, int new_depth, bool is_temp);
static EHASH_HASH_KEY ehash_hash (void *orig_key, DB_TYPE key_type);
static int ehash_find_bucket_vpid (THREAD_ENTRY * thread_p, EHID * ehid, EHASH_DIR_HEADER * dir_header, int location,
				   PGBUF_LATCH_MODE latch, VPID * out_vpid);
static PAGE_PTR ehash_find_bucket_vpid_with_hash (THREAD_ENTRY * thread_p, EHID * ehid, void *key,
						  PGBUF_LATCH_MODE root_latch, PGBUF_LATCH_MODE bucket_latch,
						  VPID * out_vpid, EHASH_HASH_KEY * out_hash_key, int *out_location);
#if defined (ENABLE_UNUSED_FUNCTION)
static int ehash_create_overflow_file (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
				       EHASH_DIR_HEADER * dir_header, FILE_TYPE file_type);
#endif
static char *ehash_read_oid_from_record (char *rec_p, OID * oid_p);
static char *ehash_write_oid_to_record (char *rec_p, OID * oid_p);
static char *ehash_read_ehid_from_record (char *rec_p, EHID * ehid_p);
static char *ehash_write_ehid_to_record (char *rec_p, EHID * ehid_p);
static int ehash_rv_delete (THREAD_ENTRY * thread_p, EHID * ehid, void *key);
static void ehash_adjust_local_depth (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
				      EHASH_DIR_HEADER * dir_header, int depth, int delta, bool is_temp);
static int ehash_apply_each (THREAD_ENTRY * thread_p, EHID * ehid, RECDES * recdes, DB_TYPE key_type, char *bucrec_ptr,
			     OID * assoc_value, int *out_apply_error, int (*apply_function) (THREAD_ENTRY * thread_p,
											     void *key, void *data,
											     void *args), void *args);
static int ehash_merge_permanent (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
				  EHASH_DIR_HEADER * dir_header, PAGE_PTR buc_pgptr, PAGE_PTR sib_pgptr,
				  VPID * buc_vpid, VPID * sib_vpid, int num_recs, int loc, PGSLOTID first_slotid,
				  int *out_new_local_depth, bool is_temp);
static void ehash_shrink_directory_if_need (THREAD_ENTRY * thread_p, EHID * ehid, EHASH_DIR_HEADER * dir_header,
					    bool is_temp);
static EHASH_RESULT ehash_check_merge_possible (THREAD_ENTRY * thread_p, EHID * ehid, EHASH_DIR_HEADER * dir_header,
						VPID * buc_vpid, PAGE_PTR buc_pgptr, int location, int lock_type,
						int *old_local_depth, VPID * sib_vpid, PAGE_PTR * out_sib_pgptr,
						PGSLOTID * out_first_slotid, int *out_num_recs, int *out_loc);
static int ehash_distribute_records_into_two_bucket (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header,
						     PAGE_PTR buc_pgptr, EHASH_BUCKET_HEADER * buc_header, int num_recs,
						     PGSLOTID first_slotid, PAGE_PTR sib_pgptr);
static int ehash_find_first_bit_position (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header, PAGE_PTR buc_pgptr,
					  EHASH_BUCKET_HEADER * buc_header, void *key, int num_recs,
					  PGSLOTID first_slotid, int *old_local_depth, int *new_local_depth);
static int ehash_get_pseudo_key (THREAD_ENTRY * thread_p, RECDES * recdes, DB_TYPE key_type,
				 EHASH_HASH_KEY * out_hash_key);
static bool ehash_binary_search_bucket (THREAD_ENTRY * thread_p, PAGE_PTR buc_pgptr, PGSLOTID num_record,
					DB_TYPE key_type, void *key, PGSLOTID * position);
static int ehash_compare_key (THREAD_ENTRY * thread_p, char *bucrec_ptr, DB_TYPE key_type, void *key, INT16 rec_type,
			      int *out_compare_result);
static int ehash_write_key_to_record (RECDES * recdes, DB_TYPE key_type, void *key_ptr, short key_size, OID * value_ptr,
				      bool long_str);
static EHASH_RESULT ehash_insert_bucket_after_extend_if_need (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
							      EHASH_DIR_HEADER * dir_header, VPID * buc_vpid, void *key,
							      EHASH_HASH_KEY hash_key, int lock_type,
							      bool is_temp, OID * value_ptr, VPID * existing_ovf_vpid);
static PAGE_PTR ehash_extend_bucket (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
				     EHASH_DIR_HEADER * dir_header, PAGE_PTR buc_pgptr, void *key,
				     EHASH_HASH_KEY hash_key, int *new_bit, VPID * buc_vpid, bool is_temp);
static int ehash_insert_to_bucket_after_create (THREAD_ENTRY * thread_p, EHID * ehid, PAGE_PTR dir_Rpgptr,
						EHASH_DIR_HEADER * dir_header, VPID * buc_vpid, int location,
						EHASH_HASH_KEY hash_key, bool is_temp, void *key,
						OID * value_ptr, VPID * existing_ovf_vpid);

static EHASH_HASH_KEY ehash_hash_string_type (char *key, char *orig_key);
static EHASH_HASH_KEY ehash_hash_eight_bytes_type (char *key);
#if defined (ENABLE_UNUSED_FUNCTION)
static EHASH_HASH_KEY ehash_hash_four_bytes_type (char *key);
static EHASH_HASH_KEY ehash_hash_two_bytes_type (char *key);
#endif

/* For debugging purposes only */
#if defined(EHINSERTION_ORDER)
static void eh_dump_key (DB_TYPE key_type, void *key, OID * value_ptr);
#endif /* EHINSERTION_ORDER */
static void ehash_dump_bucket (THREAD_ENTRY * thread_p, PAGE_PTR buc_pgptr, DB_TYPE key_type);

/*
 * ehash_dir_locate()
 *
 *   page_no_var(in): The nth directory page containing the pointer
 *   offset_var(in): The offset into the directory just like if the directory
 *              where contained in one contiguous area.
 *              SET by this macro according to the page_no_var where the
 *              offset is located in this page.
 *
 * Note: This macro finds the location of a directory pointer
 * (i.e. the number of directory page containing this pointer and
 * the offset value to reach the pointer within that page).
 * The pointer number is given to macro with the
 * "offset_var" parameter.
 * Since this operation is performed very frequently, it is
 * coded as a macro. So, it must be used very carefully. Two
 * variables should be passed as parameters. The variable passed
 * to "offset_var" should have the number of pointer prior to
 * this macro call.
 * In the implementation of this macro, division operation
 * is performed twice; once for the remainder and once for the
 * quotient. This is preferred to the method of successive
 * subtractions since built-in "/" and "%" operators take
 * constant time, whereas the latter method has linear
 * characteristic (i.e., it would take longer for bigger
 * directory sizes).
 */
static void
ehash_dir_locate (int *out_page_no_p, int *out_offset_p)
{
  int page_no, offset;

  offset = *out_offset_p;

  if (offset < EHASH_NUM_FIRST_PAGES)
    {
      /* in the first page */
      offset = offset * sizeof (EHASH_DIR_RECORD) + EHASH_DIR_HEADER_SIZE;
      page_no = 0;		/* at least one page */
    }
  else
    {
      /* not in the first page */
      offset -= EHASH_NUM_FIRST_PAGES;
      page_no = offset / EHASH_NUM_NON_FIRST_PAGES + 1;
      offset = (offset % EHASH_NUM_NON_FIRST_PAGES) * sizeof (EHASH_DIR_RECORD);
    }

  *out_page_no_p = page_no;
  *out_offset_p = offset;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * Overflow page handling functions
 */

/*
 * ehash_ansi_sql_strncmp () -
 *   return:
 *   s(in):
 *   t(in):
 *   max(in):
 */
static int
ehash_ansi_sql_strncmp (const char *s, const char *t, int max)
{
  int i;

  for (i = 0; (*s == *t) && (i < max); s++, t++, i++)
    {
      if (*s == '\0')
	{
	  return 0;
	}
    }

  if (i == max)
    {
      return 0;
    }

  if (*s == '\0')
    {
      while (*t != '\0')
	{			/* consume space-char */
	  if (*t++ != ' ')
	    {
	      return -1;
	    }
	}
      return 0;
    }
  else if (*t == '\0')
    {
      while (*s != '\0')
	{			/* consume space-char */
	  if (*s++ != ' ')
	    {
	      return 1;
	    }
	}
      return 0;
    }


  return (*(unsigned const char *) s < *(unsigned const char *) t) ? -1 : +1;

}
#endif

/*
 * ehash_allocate_recdes () -
 *   return: char *, or NULL
 *   recdes(out) : Record descriptor
 *   size(in)    : Request size
 *   type(in)    : Request type
 */
static char *
ehash_allocate_recdes (RECDES * recdes_p, int size, short type)
{
  recdes_p->area_size = recdes_p->length = size;
  recdes_p->type = type;
  recdes_p->data = NULL;

  if (recdes_p->area_size > 0)
    {
      recdes_p->data = (char *) malloc (recdes_p->area_size);
      if (recdes_p->data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) recdes_p->area_size);
	}
    }

  return recdes_p->data;
}

/*
 * ehash_free_recdes () -
 *   return:
 *   recdes(in): Record descriptor
 */
static void
ehash_free_recdes (RECDES * recdes_p)
{
  if (recdes_p->data)
    {
      free_and_init (recdes_p->data);
    }
  recdes_p->area_size = recdes_p->length = 0;

}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ehash_compare_overflow () - get/retrieve the content of a multipage object from overflow
 *   return: NO_ERROR, or ER_FAILED
 *   ovf_vpid(in): Overflow address
 *   key(in):
 *   comp_result(out):
 *
 * Note: The content of a multipage object associated with the given
 * overflow address(oid) is placed into the area pointed to by
 * the record descriptor. If the content of the object does not
 * fit in such an area (i.e., recdes->area_size), an error is
 * returned and a hint of its length is returned as a negative
 * value in recdes->length. The length of the retrieved object is
 * set in the the record descriptor (i.e., recdes->length).
 */
static int
ehash_compare_overflow (THREAD_ENTRY * thread_p, const VPID * ovf_vpid_p, char *key_p, int *out_comp_result)
{
  RECDES ovf_recdes;
  int size;
  int er_code;

  size = overflow_get_length (thread_p, ovf_vpid_p);
  if (size == -1)
    {
      return ER_FAILED;
    }

  if (ehash_allocate_recdes (&ovf_recdes, size, REC_HOME) == NULL)
    {
      return ER_FAILED;
    }

  er_code = NO_ERROR;

  if (overflow_get (thread_p, ovf_vpid_p, &ovf_recdes, NULL) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  *out_comp_result = ansisql_strcmp (key_p, ovf_recdes.data);

end:
  ehash_free_recdes (&ovf_recdes);
  return er_code;

exit_on_error:
  er_code = ER_FAILED;
  goto end;
}

/*
 * ehash_compose_overflow () - get/retrieve the content of a multipage object from
 *                    overflow
 *   return:
 *   recdes(in): Record descriptor
 *
 * Note: The content of a multipage object associated with the given
 * overflow address(oid) is placed into the area pointed to by
 * the record descriptor. If the content of the object does not
 * fit in such an area (i.e., recdes->area_size), an error is
 * returned and a hint of its length is returned as a negative
 * value in recdes->length. The length of the retrieved object is
 * set in the the record descriptor (i.e., recdes->length).
 */
static char *
ehash_compose_overflow (THREAD_ENTRY * thread_p, RECDES * recdes_p)
{
  VPID *ovf_vpid_p;
  RECDES ovf_recdes;
  int size;
  char *bucket_p;

  /* Skip the OID field and point to ovf_vpid field */
  ovf_vpid_p = (VPID *) (recdes_p->data + sizeof (OID));

  /* Allocate the space for the whole key */
  size = overflow_get_length (thread_p, ovf_vpid_p);
  if (size == -1)
    {
      return NULL;
    }
  size += EHASH_LONG_STRING_PREFIX_SIZE;

  if (ehash_allocate_recdes (&ovf_recdes, size, REC_HOME) == NULL)
    {
      return NULL;
    }

  /* Copy the prefix key portion first */

  /* Skip the OID & ovf_vpid fields and point to the prefix key */
  bucket_p = recdes_p->data + sizeof (OID) + sizeof (VPID);

  memcpy (ovf_recdes.data, bucket_p, EHASH_LONG_STRING_PREFIX_SIZE);
  ovf_recdes.data += EHASH_LONG_STRING_PREFIX_SIZE;
  ovf_recdes.area_size -= EHASH_LONG_STRING_PREFIX_SIZE;

  /* Copy to the overflow portion of the key */
  if (overflow_get (thread_p, ovf_vpid_p, &ovf_recdes, NULL) != S_SUCCESS)
    {
      ehash_free_recdes (&ovf_recdes);
      return NULL;
    }

  return ovf_recdes.data;	/* key */
}
#endif

/*
 * ehash_initialize_bucket_new_page () - Initialize a newly allocated page
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry
 * page_p (in)	 : New ehash bucket page
 * args (in)     : Alignment, depth and is_temp. Used to initialize and log the page.
 */
static int
ehash_initialize_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args)
{
  char alignment;
  char depth;
  bool is_temp;
  int offset = 0;
  EHASH_BUCKET_HEADER bucket_header;
  RECDES bucket_recdes;
  PGSLOTID slot_id;
  int success = SP_SUCCESS;

  int error_code = NO_ERROR;

  alignment = *(char *) args;
  offset += sizeof (alignment);

  depth = *((char *) args + offset);
  offset += sizeof (depth);

  is_temp = *(bool *) ((char *) args + offset);

  /*
   * fetch and initialize the new page. The parameter UNANCHORED_KEEP_
   * SEQUENCE indicates that the order of records will be preserved
   * during insertions and deletions.
   */

  pgbuf_set_page_ptype (thread_p, page_p, PAGE_EHASH);

  /*
   * Initialize the bucket to contain variable-length records
   * on ordered slots.
   */
  spage_initialize (thread_p, page_p, UNANCHORED_KEEP_SEQUENCE, alignment, DONT_SAFEGUARD_RVSPACE);

  /* Initialize the bucket header */
  bucket_header.local_depth = depth;

  /* Set the record descriptor to the Bucket header */
  bucket_recdes.data = (char *) &bucket_header;
  bucket_recdes.area_size = bucket_recdes.length = sizeof (EHASH_BUCKET_HEADER);
  bucket_recdes.type = REC_HOME;

  /*
   * Insert the bucket header into the first slot (slot # 0)
   * on the bucket page
   */
  success = spage_insert (thread_p, page_p, &bucket_recdes, &slot_id);
  if (success != SP_SUCCESS)
    {
      /*
       * Slotted page module refuses to insert a short size record to an
       * empty page. This should never happen.
       */
      if (success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  error_code = ER_FAILED;
	}
      else
	{
	  ASSERT_ERROR_AND_SET (error_code);
	}
      return error_code;
    }

  if (!is_temp)
    {
      log_append_undoredo_data2 (thread_p, RVEH_INIT_BUCKET, NULL, page_p, -1, 0, 2, NULL, args);
    }
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * ehash_initialize_dir_new_pages () - Initialize new page used to expand extensible hash directory.
 *
 * return	    : Error code
 * thread_p (in)    : Thread entry
 * page_p (in)	    : New directory page
 * ignore_args (in) : (not used)
 */
static int
ehash_initialize_dir_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args)
{
  bool is_temp = *(bool *) args;

  pgbuf_set_page_ptype (thread_p, page_p, PAGE_EHASH);
  if (!is_temp)
    {
      log_append_undoredo_data2 (thread_p, RVEH_INIT_NEW_DIR_PAGE, NULL, page_p, -1, 0, 0, NULL, NULL);
    }
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * ehash_rv_init_dir_new_page_redo () - Redo initialize new page used to
 *					expand extensible hash table.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : No data.
 */
int
ehash_rv_init_dir_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_EHASH);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

#if defined(EHINSERTION_ORDER)
/*
 * eh_dump_key () -
 *   return:
 *   key_type(in):
 *   key(in):
 *   value_ptr(in):
 */
static void
eh_dump_key (DB_TYPE key_type, void *key, OID * value_ptr)
{
  int hour, minute, second, month, day, year;
  double d;

  switch (key_type)
    {
    case DB_TYPE_STRING:
      fprintf (stdout, "key:%s", (char *) key);
      break;

    case DB_TYPE_OBJECT:
      fprintf (stdout, "key:%d|%d|%d", ((OID *) key)->volid, ((OID *) key)->pageid, ((OID *) key)->slotid);
      break;

    case DB_TYPE_DOUBLE:
      fprintf (stdout, "key:%f", *(double *) key);
      break;

    case DB_TYPE_FLOAT:
      fprintf (stdout, "key:%f", *(float *) key);
      break;

    case DB_TYPE_INTEGER:
      fprintf (stdout, "key:%d", *(int *) key);
      break;

    case DB_TYPE_BIGINT:
      fprintf (stdout, "key:%lld", (long long) (*(DB_BIGINT *) key));
      break;

    case DB_TYPE_SHORT:
      fprintf (stdout, "key:%d", *(short *) key);
      break;

    case DB_TYPE_DATE:
      db_date_decode ((DB_DATE *) key, &month, &day, &year);
      fprintf (stdout, "key:%2d/%2d/%4d", month, day, year);
      break;

    case DB_TYPE_TIME:
      db_time_decode ((DB_TIME *) key, &hour, &minute, &second);
      fprintf (stdout, "key:%3d:%3d:%3d", hour, minute, second);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      fprintf (stdout, "key:%d", *(DB_UTIME *) key);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      fprintf (stdout, "key:%d", ((DB_TIMESTAMPTZ *) key)->timestamp, ((DB_TIMESTAMPTZ *) key)->tz_id);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      fprintf (stdout, "key:%d,%d", ((DB_DATETIME *) key)->date, ((DB_DATETIME *) key)->time);
      break;

    case DB_TYPE_DATETIMETZ:
      fprintf (stdout, "key:%d,%d", ((DB_DATETIMETZ *) key)->datetime.date, ((DB_DATETIMETZ *) key)->datetime.time,
	       ((DB_DATETIMETZ *) key)->tz_id);
      break;

    case DB_TYPE_MONETARY:
      OR_MOVE_DOUBLE (key, &d);
      fprintf (stdout, "key:%f type %d", d, ((DB_MONETARY *) key)->type);
      break;

    default:
      /* Unspecified key type: Directory header has been corrupted */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_INVALID_KEY_TYPE, 1, key_type);
      return;
    }

  fprintf (stdout, "  OID associated value:%5d,%5d,%5d\n", value_ptr->volid, value_ptr->pageid, value_ptr->slotid);

}
#endif /* EHINSERTION_ORDER */

/*
 * xehash_create () - Create an extendible hashing structure
 *   return: EHID * (NULL in case of error)
 *   ehid(in): identifier for the extendible hashing structure to
 *             create. The volid field should already be set; others
 *             are set by this function.
 *   key_type(in): key type for the extendible hashing structure
 *   exp_num_entries(in): expected number of entries (i.e., <key, oid> pairs).
 *                   This figure is used as a guide to estimate the number of
 *		     pages the extendible hashing structure will occupy.
 *		     The purpose of this estimate is to increase the locality
 *		     of reference on the disk.
 *		     If the number of entries is not known, a negative value
 *		     should be passed to this parameter.
 *   class_oid(in): OID of the class for which the index is created
 *   attr_id(in): Identifier of the attribute of the class for which the
 *                index is created.
 *   is_tmp(in): true, if the EHT will be based on temporary files.
 *
 * Note: Creates an extendible hashing structure for the particular
 * key type on the disk volume whose identifier is passed in
 * ehid->vfid.volid field. It creates two files on this volume: one
 * for the directory and one for the buckets. It also allocates
 * the first page of each file; the first bucket and the root
 * page of the directory.
 * The directory header area of the root page is
 * initialized by this function. Both the global depth of the
 * directory and the local depth of first bucket are set to 0.
 * The identifier of bucket file is kept in the directory header.
 * The directory file identifier and directory root page
 * identifier are loaded into the remaining fields of "Dir"
 * to be used as the complete identifier of this extendible
 * hashing structure for future reference.
 */
EHID *
xehash_create (THREAD_ENTRY * thread_p, EHID * ehid_p, DB_TYPE key_type, int exp_num_entries, OID * class_oid_p,
	       int attr_id, bool is_tmp)
{
  return ehash_create_helper (thread_p, ehid_p, key_type, exp_num_entries, class_oid_p, attr_id, is_tmp);
}

/*
 * ehash_get_key_size () - Return the size of keys in bytes;
 *                      Undeclared if key_type is "DB_TYPE_STRING".
 *   return: byte size of key_type, or -1 for error;
 *   key_type(in):
 */
static short
ehash_get_key_size (DB_TYPE key_type)
{
  short key_size;

  switch (key_type)
    {
    case DB_TYPE_STRING:
      key_size = 1;		/* Size of each key will vary */
      break;

    case DB_TYPE_OBJECT:
      key_size = sizeof (OID);
      break;

#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_DOUBLE:
      key_size = sizeof (double);
      break;

    case DB_TYPE_FLOAT:
      key_size = sizeof (float);
      break;

    case DB_TYPE_INTEGER:
      key_size = sizeof (int);
      break;

    case DB_TYPE_BIGINT:
      key_size = sizeof (DB_BIGINT);
      break;

    case DB_TYPE_SHORT:
      key_size = sizeof (short);
      break;

    case DB_TYPE_DATE:
      key_size = sizeof (DB_DATE);
      break;

    case DB_TYPE_TIME:
      key_size = sizeof (DB_TIME);
      break;

    case DB_TYPE_TIMESTAMP:
      key_size = sizeof (DB_UTIME);
      break;

    case DB_TYPE_DATETIME:
      key_size = sizeof (DB_DATETIME);
      break;

    case DB_TYPE_MONETARY:
      key_size = OR_MONETARY_SIZE;
      break;
#endif
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_INVALID_KEY_TYPE, 1, key_type);
      key_size = -1;
      break;
    }

  return key_size;
}

/*
 * ehash_create_helper () -
 *   return:
 *   ehid(in):
 *   key_type(in):
 *   exp_num_entries(in):
 *   class_oid(in):
 *   attr_id(in):
 *   is_tmp(in):
 */
static EHID *
ehash_create_helper (THREAD_ENTRY * thread_p, EHID * ehid_p, DB_TYPE key_type, int exp_num_entries, OID * class_oid_p,
		     int attr_id, bool is_tmp)
{
  EHASH_DIR_HEADER *dir_header_p = NULL;
  EHASH_DIR_RECORD *dir_record_p = NULL;
  VFID dir_vfid = VFID_INITIALIZER;
  VPID dir_vpid = VPID_INITIALIZER;
  PAGE_PTR dir_page_p = NULL;
  VFID bucket_vfid = VFID_INITIALIZER;
  VPID bucket_vpid = VPID_INITIALIZER;
  char init_bucket_data[3];
  DKNPAGES exp_bucket_pages;
  DKNPAGES exp_dir_pages;
  short key_size;
  char alignment;
  OID value;
  unsigned int i;
  FILE_EHASH_DES ehdes;
  PAGE_TYPE ptype = PAGE_EHASH;

  assert (key_type == DB_TYPE_STRING || key_type == DB_TYPE_OBJECT);

  if (ehid_p == NULL)
    {
      return NULL;
    }

  /* create a file descriptor */
  if (class_oid_p != NULL)
    {
      COPY_OID (&ehdes.class_oid, class_oid_p);
    }
  else
    {
      OID_SET_NULL (&ehdes.class_oid);
    }
  ehdes.attr_id = attr_id;

  /* Set the key size */
  key_size = ehash_get_key_size (key_type);
  if (key_size < 0)
    {
      return NULL;
    }

  /* Estimate number of bucket pages */
  if (exp_num_entries < 0)
    {
      /* Assume minimum size */
      exp_bucket_pages = 1;
    }
  else
    {
      if (key_type == DB_TYPE_STRING)
	{
	  exp_bucket_pages = exp_num_entries * (20 + sizeof (OID) + 4);
	}
      else
	{
	  exp_bucket_pages = exp_num_entries * (key_size + sizeof (OID) + 4);
	}

      exp_bucket_pages = CEIL_PTVDIV (exp_bucket_pages, DB_PAGESIZE);
    }

  /* Calculate alignment to use on slots of bucket pages */
  if (SSIZEOF (value.pageid) >= key_size)
    {
      alignment = sizeof (value.pageid);
    }
  else
    {
      alignment = (char) key_size;
    }

  /* TODO: M2 64-bit */
  /* May want to remove later on; not portable to 64-bit machines */
  if (alignment > SSIZEOF (int))
    {
      alignment = sizeof (int);
    }

  /* Create the first bucket and initialize its header */

  bucket_vfid.volid = ehid_p->vfid.volid;

  if (file_create_ehash (thread_p, exp_bucket_pages, is_tmp, &ehdes, &bucket_vfid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  /* Log the initialization of the first bucket page */

  init_bucket_data[0] = alignment;
  init_bucket_data[1] = 0;
  init_bucket_data[2] = is_tmp;

  if (file_alloc (thread_p, &bucket_vfid, ehash_initialize_bucket_new_page, init_bucket_data, &bucket_vpid, NULL)
      != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  /* Estimate number of directory pages */
  if (exp_num_entries < 0)
    {
      exp_dir_pages = 1;
    }
  else
    {
      /* Calculate how many directory pages will be used */
      ehash_dir_locate (&exp_dir_pages, &exp_bucket_pages);
      /* exp_dir_pages is actually an index. the number of pages should be +1 */
      exp_dir_pages++;
    }

  /* Create the directory (allocate the first page) and initialize its header */

  dir_vfid.volid = bucket_vfid.volid;

  /*
   * Create the file and allocate the first page
   *
   * We do not initialize the page during the allocation since the file is
   * new, and the file is going to be removed in the event of a crash.
   */

  if (file_create_ehash_dir (thread_p, exp_dir_pages, is_tmp, &ehdes, &dir_vfid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
  if (file_alloc (thread_p, &dir_vfid, is_tmp ? file_init_temp_page_type : file_init_page_type, &ptype, &dir_vpid,
		  &dir_page_p) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
  if (dir_page_p == NULL)
    {
      assert_release (false);
      goto exit_on_error;
    }
#if !defined (NDEBUG)
  pgbuf_check_page_ptype (thread_p, dir_page_p, PAGE_EHASH);
#endif /* !NDEBUG */

  dir_header_p = (EHASH_DIR_HEADER *) dir_page_p;

  /* Initialize the directory header */
  dir_header_p->depth = 0;
  dir_header_p->key_type = key_type;
  dir_header_p->alignment = alignment;
  VFID_COPY (&dir_header_p->bucket_file, &bucket_vfid);
  VFID_SET_NULL (&dir_header_p->overflow_file);

  /* Initialize local depth information */
  dir_header_p->local_depth_count[0] = 1;
  for (i = 1; i <= EHASH_HASH_KEY_BITS; i++)
    {
      dir_header_p->local_depth_count[i] = 0;
    }

  /*
   * Check if the key is of fixed size, and if so, store this information
   * in the directory header
   */

  dir_record_p = (EHASH_DIR_RECORD *) ((char *) dir_page_p + EHASH_DIR_HEADER_SIZE);
  dir_record_p->bucket_vpid = bucket_vpid;

  /*
   * Don't need UNDO since we are just creating the file. If we abort, the
   * file is removed.
   */

  /* Log the directory root page */
  if (!is_tmp)
    {
      log_append_redo_data2 (thread_p, RVEH_INIT_DIR, &dir_vfid, dir_page_p, 0,
			     EHASH_DIR_HEADER_SIZE + sizeof (EHASH_DIR_RECORD), dir_page_p);
    }

  /* Finishing up; release the pages and return directory file id */
  pgbuf_set_dirty (thread_p, dir_page_p, FREE);

  VFID_COPY (&ehid_p->vfid, &dir_vfid);
  ehid_p->pageid = dir_vpid.pageid;

  return ehid_p;

exit_on_error:

  if (!VFID_ISNULL (&bucket_vfid))
    {
      if (is_tmp)
	{
	  if (file_destroy (thread_p, &bucket_vfid, is_tmp) != NO_ERROR)
	    {
	      assert_release (false);
	    }
	}
      else
	{
	  (void) file_postpone_destroy (thread_p, &bucket_vfid);
	}
    }
  if (!VFID_ISNULL (&dir_vfid))
    {
      if (is_tmp)
	{
	  if (file_destroy (thread_p, &dir_vfid, is_tmp) != NO_ERROR)
	    {
	      assert_release (false);
	    }
	}
      else
	{
	  (void) file_postpone_destroy (thread_p, &dir_vfid);
	}
    }
  return NULL;
}

/*
 * ehash_fix_old_page () -
 *   return: specified page pointer, or NULL
 *   vfid_p(in): only for error reporting
 *   vpid_p(in):
 *   latch_mode(in): lock mode
 */
static PAGE_PTR
ehash_fix_old_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, const VPID * vpid_p, PGBUF_LATCH_MODE latch_mode)
{
  PAGE_PTR page_p;

  page_p = pgbuf_fix (thread_p, vpid_p, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_UNKNOWN_EXT_HASH, 4, vfid_p->volid, vfid_p->fileid,
		  vpid_p->volid, vpid_p->pageid);
	}

      return NULL;
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_EHASH);
#endif /* !NDEBUG */

  return page_p;
}

/*
 * ehash_fix_ehid_page () -
 *   return: specified page pointer, or NULL
 *   ehid(in): extendible hashing structure
 *   latch_mode(in): lock mode
 */
static PAGE_PTR
ehash_fix_ehid_page (THREAD_ENTRY * thread_p, EHID * ehid, PGBUF_LATCH_MODE latch_mode)
{
  VPID vpid;

  vpid.volid = ehid->vfid.volid;
  vpid.pageid = ehid->pageid;

  return ehash_fix_old_page (thread_p, &(ehid->vfid), &vpid, latch_mode);
}

/*
 * ehash_fix_nth_page () -
 *   return: specified page pointer, or NULL
 *   vfid(in):
 *   offset(in):
 *   lock(in): lock mode
 */
static PAGE_PTR
ehash_fix_nth_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, int offset, PGBUF_LATCH_MODE latch_mode)
{
  VPID vpid;

  if (file_numerable_find_nth (thread_p, vfid_p, offset, false, NULL, NULL, &vpid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  return ehash_fix_old_page (thread_p, vfid_p, &vpid, latch_mode);
}

/*
 * xehash_destroy () - destroy the extensible hash table
 *
 * return        : error code
 * thread_p (in) : thread entry
 * ehid_p (in)   : extensible hash identifier
 *
 * note: only temporary extensible hash tables can be destroyed.
 */
int
xehash_destroy (THREAD_ENTRY * thread_p, EHID * ehid_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_page_p;

  if (ehid_p == NULL)
    {
      return ER_FAILED;
    }

  dir_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_WRITE);
  if (dir_page_p == NULL)
    {
      return ER_FAILED;
    }

  log_sysop_start (thread_p);

  dir_header_p = (EHASH_DIR_HEADER *) dir_page_p;

  if (file_destroy (thread_p, &(dir_header_p->bucket_file), true) != NO_ERROR)
    {
      assert_release (false);
    }
  pgbuf_unfix (thread_p, dir_page_p);
  if (file_destroy (thread_p, &ehid_p->vfid, true) != NO_ERROR)
    {
      assert_release (false);
    }

  log_sysop_commit (thread_p);

  return NO_ERROR;
}

static int
ehash_find_bucket_vpid (THREAD_ENTRY * thread_p, EHID * ehid_p, EHASH_DIR_HEADER * dir_header_p, int location,
			PGBUF_LATCH_MODE latch, VPID * out_vpid_p)
{
  EHASH_DIR_RECORD *dir_record_p;
  PAGE_PTR dir_page_p;
  int dir_offset;

  ehash_dir_locate (&dir_offset, &location);

  if (dir_offset != 0)
    {
      /* The bucket pointer is not in the root (first) page of the directory */
      dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, dir_offset, latch);
      if (dir_page_p == NULL)
	{
	  return ER_FAILED;
	}

      /* Find the bucket page containing the key */
      dir_record_p = (EHASH_DIR_RECORD *) ((char *) dir_page_p + location);
      pgbuf_unfix_and_init (thread_p, dir_page_p);
    }
  else
    {
      dir_record_p = (EHASH_DIR_RECORD *) ((char *) dir_header_p + location);
    }

  *out_vpid_p = dir_record_p->bucket_vpid;
  return NO_ERROR;
}

static PAGE_PTR
ehash_find_bucket_vpid_with_hash (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p, PGBUF_LATCH_MODE root_latch,
				  PGBUF_LATCH_MODE bucket_latch, VPID * out_vpid_p, EHASH_HASH_KEY * out_hash_key_p,
				  int *out_location_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  EHASH_HASH_KEY hash_key;
  int location;

  dir_root_page_p = ehash_fix_ehid_page (thread_p, ehid_p, root_latch);
  if (dir_root_page_p == NULL)
    {
      return NULL;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  /* Get the pseudo key */
  hash_key = ehash_hash (key_p, dir_header_p->key_type);
  if (out_hash_key_p)
    {
      *out_hash_key_p = hash_key;
    }

  /* Find the location of bucket pointer in the directory */
  location = FIND_OFFSET (hash_key, dir_header_p->depth);
  if (out_location_p)
    {
      *out_location_p = location;
    }

  if (ehash_find_bucket_vpid (thread_p, ehid_p, dir_header_p, location, bucket_latch, out_vpid_p) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      return NULL;
    }

  return dir_root_page_p;
}

/*
 * ehash_search () - Search for the key; return associated value
 *   return: EH_SEARCH
 *   ehid(in): identifier for the extendible hashing structure
 *   key(in): key to search
 *   value_ptr(out): pointer to return the value associated with the key
 *
 * Note: Returns the value associated with the given key, if it is
 * found in the specified extendible hashing structure. If the
 * key is not found an error condition is returned.
 */
EH_SEARCH
ehash_search (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p, OID * value_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p = NULL;
  PAGE_PTR bucket_page_p = NULL;
  VPID bucket_vpid;
  RECDES recdes;
  PGSLOTID slot_id;
  EH_SEARCH result = EH_KEY_NOTFOUND;

  if (ehid_p == NULL || key_p == NULL)
    {
      return EH_KEY_NOTFOUND;
    }

  dir_root_page_p =
    ehash_find_bucket_vpid_with_hash (thread_p, ehid_p, key_p, PGBUF_LATCH_READ, PGBUF_LATCH_READ, &bucket_vpid, NULL,
				      NULL);
  if (dir_root_page_p == NULL)
    {
      return EH_ERROR_OCCURRED;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (bucket_vpid.pageid == NULL_PAGEID)
    {
      result = EH_KEY_NOTFOUND;
      goto end;
    }

  bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, &bucket_vpid, PGBUF_LATCH_READ);
  if (bucket_page_p == NULL)
    {
      result = EH_ERROR_OCCURRED;
      goto end;
    }

  if (ehash_locate_slot (thread_p, bucket_page_p, dir_header_p->key_type, key_p, &slot_id) == false)
    {
      result = EH_KEY_NOTFOUND;
      goto end;
    }

  (void) spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK);
  (void) ehash_read_oid_from_record (recdes.data, value_p);
  result = EH_KEY_FOUND;

end:
  if (bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }

  if (dir_root_page_p)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
    }

  return result;
}

/*
 * ehash_insert () - Insert (key, assoc_value) pair to ext. hashing
 *   return: void * (NULL is returned in case of error)
 *   ehid(in): identifier of the extendible hashing structure
 *   key(in): key value to insert
 *   value_ptr(in): pointer to the associated value to insert
 *
 * Note: Inserts the given (key & assoc_value) pair into the specified
 * extendible hashing structure. If the key already exists then
 * the previous associated value is replaced with the new one.
 * Otherwise, a new record is inserted to the correct bucket.
 *      To perform the insertion operation safely in the concurrent
 * environment, an auxiliary (and private) function
 * "eh_insert_helper", which takes an additional parameter to
 * specify the lock type to acquire on the directory, is used
 * by this function. Hoping that this insertion will not effect
 * the directory pages, it passes a shared lock
 * (i.e, an "S_LOCK") for this parameter.
 */
void *
ehash_insert (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p, OID * value_p)
{
  if (ehid_p == NULL || key_p == NULL)
    {
      return NULL;
    }

  return ehash_insert_helper (thread_p, ehid_p, key_p, value_p, S_LOCK, NULL);
}

static int
ehash_insert_to_bucket_after_create (THREAD_ENTRY * thread_p, EHID * ehid_p, PAGE_PTR dir_root_page_p,
				     EHASH_DIR_HEADER * dir_header_p, VPID * bucket_vpid_p, int location,
				     EHASH_HASH_KEY hash_key, bool is_temp, void *key_p, OID * value_p,
				     VPID * existing_ovf_vpid_p)
{
  PAGE_PTR bucket_page_p;
  EHASH_BUCKET_HEADER bucket_header;
  char found_depth;
  char init_bucket_data[3];
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  EHASH_RESULT ins_result;

  int error_code = NO_ERROR;

  found_depth = ehash_find_depth (thread_p, ehid_p, location, &null_vpid, &null_vpid);
  if (found_depth == 0)
    {
      return ER_FAILED;
    }

  init_bucket_data[0] = dir_header_p->alignment;
  init_bucket_data[1] = dir_header_p->depth - found_depth;
  init_bucket_data[2] = is_temp;
  bucket_header.local_depth = init_bucket_data[1];

  log_sysop_start (thread_p);

  error_code = file_alloc (thread_p, &dir_header_p->bucket_file, ehash_initialize_bucket_new_page, init_bucket_data,
			   bucket_vpid_p, &bucket_page_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return ER_FAILED;
    }
  if (bucket_page_p == NULL)
    {
      assert_release (false);
      log_sysop_abort (thread_p);
      return ER_FAILED;
    }
#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, bucket_page_p, PAGE_EHASH);
#endif /* !NDEBUG */

  if (ehash_connect_bucket (thread_p, ehid_p, bucket_header.local_depth, hash_key, bucket_vpid_p, is_temp) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
      log_sysop_abort (thread_p);
      return ER_FAILED;
    }

  ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, (int) bucket_header.local_depth, 1,
			    is_temp);

  log_sysop_commit (thread_p);

  ins_result =
    ehash_insert_to_bucket (thread_p, ehid_p, &dir_header_p->overflow_file, is_temp, bucket_page_p,
			    dir_header_p->key_type, key_p, value_p, existing_ovf_vpid_p);

  if (ins_result != EHASH_SUCCESSFUL_COMPLETION)
    {
      /* Slotted page module refuses to insert a short size record to an almost empty page. This should never happen. */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
      return ER_FAILED;
    }

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  return NO_ERROR;
}

static PAGE_PTR
ehash_extend_bucket (THREAD_ENTRY * thread_p, EHID * ehid_p, PAGE_PTR dir_root_page_p, EHASH_DIR_HEADER * dir_header_p,
		     PAGE_PTR bucket_page_p, void *key_p, EHASH_HASH_KEY hash_key, int *out_new_bit_p,
		     VPID * bucket_vpid, bool is_temp)
{
  VPID sibling_vpid;
  PAGE_PTR sibling_page_p = NULL;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  int old_local_depth;
  int new_local_depth;

  if (!is_temp)
    {
      log_sysop_start (thread_p);
    }

  sibling_page_p =
    ehash_split_bucket (thread_p, dir_header_p, bucket_page_p, key_p, &old_local_depth, &new_local_depth,
			&sibling_vpid, is_temp);
  if (sibling_page_p == NULL)
    {
      if (!is_temp)
	{
	  log_sysop_abort (thread_p);
	}
      return NULL;
    }

  /* Save the bit position of hash_key to be used later in deciding whether to insert the new key to original bucket or
   * to the new sibling bucket. */
  *out_new_bit_p = GETBIT (hash_key, new_local_depth);

  ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, old_local_depth, -1, is_temp);
  ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, new_local_depth, 2, is_temp);

  /* Check directory expansion condition */
  if (new_local_depth > dir_header_p->depth)
    {
      if (ehash_expand_directory (thread_p, ehid_p, new_local_depth, is_temp) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  if (!is_temp)
	    {
	      log_sysop_abort (thread_p);
	    }
	  return NULL;
	}
    }

  /* Connect the buckets */
  if ((new_local_depth - old_local_depth) > 1)
    {
      /* First, set all of them to NULL_PAGEID */
      if (ehash_connect_bucket (thread_p, ehid_p, old_local_depth, hash_key, &null_vpid, is_temp) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  if (!is_temp)
	    {
	      log_sysop_abort (thread_p);
	    }
	  return NULL;
	}

      /* Then, connect the Bucket page */
      hash_key = CLEARBIT (hash_key, new_local_depth);
      if (ehash_connect_bucket (thread_p, ehid_p, new_local_depth, hash_key, bucket_vpid, is_temp) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  if (!is_temp)
	    {
	      log_sysop_abort (thread_p);
	    }
	  return NULL;
	}
    }

  /* Finally, connect the Sibling bucket page */
  hash_key = SETBIT (hash_key, new_local_depth);
  if (ehash_connect_bucket (thread_p, ehid_p, new_local_depth, hash_key, &sibling_vpid, is_temp) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
      if (!is_temp)
	{
	  log_sysop_abort (thread_p);
	}
      return NULL;
    }

  if (!is_temp)
    {
      log_sysop_commit (thread_p);
    }
  return sibling_page_p;
}

static EHASH_RESULT
ehash_insert_bucket_after_extend_if_need (THREAD_ENTRY * thread_p, EHID * ehid_p, PAGE_PTR dir_root_page_p,
					  EHASH_DIR_HEADER * dir_header_p, VPID * bucket_vpid_p, void *key_p,
					  EHASH_HASH_KEY hash_key, int lock_type, bool is_temp, OID * value_p,
					  VPID * existing_ovf_vpid_p)
{
  PAGE_PTR bucket_page_p;
  PAGE_PTR sibling_page_p = NULL;
  PAGE_PTR target_bucket_page_p;
  int new_bit;
  EHASH_RESULT result;

  /* We need to put a X_LOCK on bucket page */
  bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, bucket_vpid_p, PGBUF_LATCH_WRITE);
  if (bucket_page_p == NULL)
    {
      return EHASH_ERROR_OCCURRED;
    }

  result =
    ehash_insert_to_bucket (thread_p, ehid_p, &dir_header_p->overflow_file, is_temp, bucket_page_p,
			    dir_header_p->key_type, key_p, value_p, existing_ovf_vpid_p);
  if (result == EHASH_BUCKET_FULL)
    {
      if (lock_type == S_LOCK)
	{
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  return EHASH_BUCKET_FULL;
	}

      sibling_page_p =
	ehash_extend_bucket (thread_p, ehid_p, dir_root_page_p, dir_header_p, bucket_page_p, key_p, hash_key, &new_bit,
			     bucket_vpid_p, is_temp);
      if (sibling_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  return EHASH_ERROR_OCCURRED;
	}

      /*
       * Try to insert the new key & assoc_value pair into one of the buckets.
       * The result of this attempt will determine if a recursive call is
       * needed to insert the new key.
       */

      if (new_bit)
	{
	  target_bucket_page_p = sibling_page_p;
	}
      else
	{
	  target_bucket_page_p = bucket_page_p;
	}

      result =
	ehash_insert_to_bucket (thread_p, ehid_p, &dir_header_p->overflow_file, is_temp, target_bucket_page_p,
				dir_header_p->key_type, key_p, value_p, existing_ovf_vpid_p);
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
    }

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  return result;
}

/*
 * ehash_insert_helper () - Perform insertion
 *   return: void * (NULL is returned in case of error)
 *   ehid(in): identifier for the extendible hashing structure
 *   key(in): key value
 *   value_ptr(in): associated value to insert
 *   lock_type(in): type of lock to acquire for accessing directory pages
 *   existing_ovf_vpid(in):
 *
 */
static void *
ehash_insert_helper (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p, OID * value_p, int lock_type,
		     VPID * existing_ovf_vpid_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  VPID bucket_vpid;

  EHASH_HASH_KEY hash_key;
  int location;
  EHASH_RESULT result;
  bool is_temp = false;

  if (file_is_temp (thread_p, &ehid_p->vfid, &is_temp) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  dir_root_page_p =
    ehash_find_bucket_vpid_with_hash (thread_p, ehid_p, key_p,
				      (lock_type == S_LOCK ? PGBUF_LATCH_READ : PGBUF_LATCH_WRITE), PGBUF_LATCH_READ,
				      &bucket_vpid, &hash_key, &location);
  if (dir_root_page_p == NULL)
    {
      return NULL;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

#if defined(EHINSERTION_ORDER)
  fprintf (stdout, "Ex Hash %d|%d|%d Insert:", ehid_p->vfid.volid, ehid_p->vfid.fileid, ehid_p->pageid);
  eh_dump_key (dir_header_p->key_type, key_p, value_p);
#endif /* EHINSERTION_ORDER */

  if (dir_header_p->key_type == DB_TYPE_STRING)
    {
      /* max length of class name is 255 */
      assert (strlen ((char *) key_p) < 256);

#if defined (ENABLE_UNUSED_FUNCTION)
      /* Check if string is too long and no overflow file has been created */
      if ((strlen ((char *) key_p) + 1) > EHASH_MAX_STRING_SIZE && VFID_ISNULL (&dir_header_p->overflow_file))
	{
	  if (lock_type == S_LOCK)
	    {
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return ehash_insert_helper (thread_p, ehid_p, key_p, value_p, X_LOCK, existing_ovf_vpid_p);
	    }
	  else
	    {
	      if (ehash_create_overflow_file (thread_p, ehid_p, dir_root_page_p, dir_header_p, file_type) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		  return NULL;
		}
	    }
	}
#endif
    }

  if (VPID_ISNULL (&bucket_vpid))
    {
      if (lock_type == S_LOCK)
	{
	  /* release lock and call itself to obtain X_LOCK */
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return ehash_insert_helper (thread_p, ehid_p, key_p, value_p, X_LOCK, existing_ovf_vpid_p);
	}
      else
	{
	  if (ehash_insert_to_bucket_after_create
	      (thread_p, ehid_p, dir_root_page_p, dir_header_p, &bucket_vpid, location, hash_key, is_temp, key_p,
	       value_p, existing_ovf_vpid_p) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return NULL;
	    }
	}
    }
  else
    {
      result =
	ehash_insert_bucket_after_extend_if_need (thread_p, ehid_p, dir_root_page_p, dir_header_p, &bucket_vpid, key_p,
						  hash_key, lock_type, is_temp, value_p, existing_ovf_vpid_p);
      if (result == EHASH_ERROR_OCCURRED)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return NULL;
	}
      else if (result == EHASH_BUCKET_FULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return ehash_insert_helper (thread_p, ehid_p, key_p, value_p, X_LOCK, existing_ovf_vpid_p);
	}
    }

  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
  return (key_p);
}

/*
 * ehash_insert_to_bucket () - Insert (key, value) to bucket
 *   return: EHASH_RESULT
 *   ehid(in): identifier for the extendible hashing structure
 *   overflow_file(in): Overflow file for extendible hash
 *   is_temp(in): is extensible hash temporary?
 *   buc_pgptr(in): bucket page to insert the key
 *   key_type(in): type of the key
 *   key_ptr(in): Pointer to the key
 *   value_ptr(in): Pointer to the associated value
 *   existing_ovf_vpid(in):
 *
 * Note: This function is used to insert a (key & assoc_value) pair
 * onto the given bucket. If the KEY already EXISTS in the bucket
 * the NEW ASSOCIATED VALUE REPLACES THE OLD ONE. Otherwise a
 * new entry is added to the bucket and the total number of
 * entries of this extendible hashing structure is incremented
 * by one. In the latter case, if the insertion is not possible
 * for some reason (e.g., the bucket does not have enough space
 * for the new record, etc.) an appropriate error code is returned.
 */
static EHASH_RESULT
ehash_insert_to_bucket (THREAD_ENTRY * thread_p, EHID * ehid_p, VFID * ovf_file_p, bool is_temp,
			PAGE_PTR bucket_page_p, DB_TYPE key_type, void *key_p, OID * value_p,
			VPID * existing_ovf_vpid_p)
{
  char *bucket_record_p;
  RECDES bucket_recdes;
  RECDES old_bucket_recdes;
  RECDES log_recdes;
#if defined (ENABLE_UNUSED_FUNCTION)
  RECDES ovf_recdes;
  VPID ovf_vpid;
  char *record_p;
#endif
  char *log_record_p;

  PGSLOTID slot_no;
  int success;
  bool is_replaced_oid = false;

  /* Check if insertion is possible, or not */
  if (ehash_locate_slot (thread_p, bucket_page_p, key_type, key_p, &slot_no) == true)
    {
      /*
       * Key already exists. So, replace the associated value
       * MIGHT BE CHANGED to allow multiple values
       */
      (void) spage_get_record (thread_p, bucket_page_p, slot_no, &old_bucket_recdes, PEEK);
      bucket_record_p = (char *) old_bucket_recdes.data;

      /*
       * Store the original OID associated with the key before it is replaced
       * with the new OID value.
       */
      is_replaced_oid = true;

      if (ehash_allocate_recdes (&bucket_recdes, old_bucket_recdes.length, old_bucket_recdes.type) == NULL)
	{
	  return EHASH_ERROR_OCCURRED;
	}

      memcpy (bucket_recdes.data, bucket_record_p, bucket_recdes.length);
      (void) ehash_write_oid_to_record (bucket_record_p, value_p);
    }
  else
    {
      /*
       * Key does not exist in the bucket, so create a record for it and
       * insert it into the bucket;
       */

      if (ehash_compose_record (key_type, key_p, value_p, &bucket_recdes) != NO_ERROR)
	{
	  return EHASH_ERROR_OCCURRED;
	}

#if defined (ENABLE_UNUSED_FUNCTION)
      /*
       * If this is a long string produce the prefix key record and see if it
       * is going to fit into this page. If it fits, produce the overflow pages.
       * If it does not return failure with SP_BUCKET_FULL value.
       */
      if (bucket_recdes.type == REC_BIGONE)
	{
	  if (bucket_recdes.length > spage_max_space_for_new_record (thread_p, bucket_page_p))
	    {
	      ehash_free_recdes (&bucket_recdes);
	      return EHASH_BUCKET_FULL;
	    }

	  ovf_recdes.data = (char *) key_p + EHASH_LONG_STRING_PREFIX_SIZE;
	  ovf_recdes.area_size = strlen ((char *) key_p) + 1;
	  ovf_recdes.length = ovf_recdes.area_size;

	  /* Update the prefix key record; put overflow page id */
	  record_p = bucket_recdes.data;
	  record_p += sizeof (OID);	/* Skip the associated OID */

	  if (existing_ovf_vpid_p != NULL)
	    /*
	     * Overflow pages already exists for this key (i.e., we are
	     * undoing a deletion of a long string
	     * **** TODO: M2 Is this right ? ****
	     */
	    *(VPID *) record_p = *existing_ovf_vpid_p;
	  else
	    {
	      /* Create the overflow pages */
	      if (overflow_insert (thread_p, ovf_file_p, &ovf_vpid, &ovf_recdes, NULL) == NULL)
		{
		  /*
		   * overflow pages creation failed; do not insert the prefix
		   * key record to the bucket; return with error
		   */
		  ehash_free_recdes (&bucket_recdes);
		  return EHASH_ERROR_OCCURRED;
		}
	      *(VPID *) record_p = ovf_vpid;
	    }
	}
#endif

      /* Try to put the record to the slotted page */
      success = spage_insert_at (thread_p, bucket_page_p, slot_no, &bucket_recdes);
      if (success != SP_SUCCESS)
	{
	  /* Problem the record was not inserted to the page */
	  ehash_free_recdes (&bucket_recdes);

#if defined (ENABLE_UNUSED_FUNCTION)
	  if (bucket_recdes.type == REC_BIGONE && existing_ovf_vpid_p == NULL)
	    {
	      /* if overflow pages has just been created delete them */
	      (void) overflow_delete (thread_p, ovf_file_p, &ovf_vpid);
	    }
#endif

	  if (success == SP_DOESNT_FIT)
	    {
	      /* There is not enough space on the slotted page for the new record */
	      return EHASH_BUCKET_FULL;
	    }
	  else
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return EHASH_ERROR_OCCURRED;
	    }
	}
    }

  /***********************************
      Log this insertion operation
   ***********************************/

  /* Add the "ehid" to the record for no-page operation logging */
  if (ehash_allocate_recdes (&log_recdes, bucket_recdes.length + sizeof (EHID), REC_HOME) == NULL)
    {
      return EHASH_ERROR_OCCURRED;
    }

  log_record_p = log_recdes.data;

  /* First insert the Ext. Hashing identifier */
  log_record_p = ehash_write_ehid_to_record (log_record_p, ehid_p);

  /* Copy (the assoc-value, key) pair from the bucket record */
  memcpy (log_record_p, bucket_recdes.data, bucket_recdes.length);

  if (!is_temp)
    {
      if (is_replaced_oid)
	{
	  /*
	   * This insertion has actully replaced the original oid. The undo
	   * logging should cause this original oid to be restored in the record.
	   * The undo recovery function "eh_rvundo_delete" with the original oid
	   * as parameter will do this.
	   */
	  log_append_undo_data2 (thread_p, RVEH_DELETE, &ehid_p->vfid, NULL, bucket_recdes.type, log_recdes.length,
				 log_recdes.data);
	}
      else
	{
	  /* insertion */
	  log_append_undo_data2 (thread_p, RVEH_INSERT, &ehid_p->vfid, NULL, bucket_recdes.type, log_recdes.length,
				 log_recdes.data);
	}
    }

  if (is_replaced_oid)
    {
      /*
       * This insertion has actually replaced the original oid. The redo logging
       * should cause this new oid to be written at its current physical
       * location.
       */
      if (!is_temp)
	{
	  log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, bucket_page_p,
				 (int) (old_bucket_recdes.data - bucket_page_p), sizeof (OID), old_bucket_recdes.data);
	}
    }
  else
    {
      /* Store the rec_type as "short" to avoid alignment problems */
      log_recdes.area_size = log_recdes.length = bucket_recdes.length + sizeof (short);

      /*
       * If undo logging was done the log_recdes.data should have enough space
       * for the redo logging since the redo log record is shorter than
       * the undo log record. Otherwise, allocate space for redo log record.
       */
      if (log_recdes.data == NULL)
	{
	  if (ehash_allocate_recdes (&log_recdes, log_recdes.length, REC_HOME) == NULL)
	    {
	      return EHASH_ERROR_OCCURRED;
	    }
	}
      log_record_p = log_recdes.data;

      /* First insert the key_type identifier */

      *(short *) log_record_p = bucket_recdes.type;
      log_record_p += sizeof (short);

      /* Copy (the assoc-value, key) pair from the bucket record */
      memcpy (log_record_p, bucket_recdes.data, bucket_recdes.length);

      if (!is_temp)
	{
	  log_append_redo_data2 (thread_p, RVEH_INSERT, &ehid_p->vfid, bucket_page_p, slot_no, log_recdes.length,
				 log_recdes.data);
	}
    }

  if (bucket_recdes.data)
    {
      ehash_free_recdes (&bucket_recdes);
    }

  if (log_recdes.data)
    {
      ehash_free_recdes (&log_recdes);
    }

  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);
  return EHASH_SUCCESSFUL_COMPLETION;
}

static int
ehash_write_key_to_record (RECDES * recdes_p, DB_TYPE key_type, void *key_p, short key_size, OID * value_p,
			   bool is_long_str)
{
  char *record_p;

  recdes_p->type = is_long_str ? REC_BIGONE : REC_HOME;

  record_p = recdes_p->data;
  record_p = ehash_write_oid_to_record (record_p, value_p);

  /* TODO: M2 64-bit ??? Is this really needed... Can we just move bytes.. we have the size of the key.. AT least all
   * data types but string_key */
  switch (key_type)
    {
    case DB_TYPE_STRING:
#if defined (ENABLE_UNUSED_FUNCTION)
      if (is_long_str)
	{
	  /* Key is a long string; produce just the prefix reord */
	  /* Skip the overflow page id; will be filled by the caller function */
	  record_p += sizeof (VPID);
	}
#endif
      memcpy (record_p, (char *) key_p, key_size);
      record_p += key_size;
      break;

    case DB_TYPE_OBJECT:
      *(OID *) record_p = *(OID *) key_p;
      break;
#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_DOUBLE:
      OR_MOVE_DOUBLE (key_p, record_p);
      break;

    case DB_TYPE_FLOAT:
      *(float *) record_p = *(float *) key_p;
      break;

    case DB_TYPE_INTEGER:
      *(int *) record_p = *(int *) key_p;
      break;

    case DB_TYPE_BIGINT:
      *(DB_BIGINT *) record_p = *(DB_BIGINT *) key_p;
      break;

    case DB_TYPE_SHORT:
      *(short *) record_p = *(short *) key_p;
      break;

    case DB_TYPE_DATE:
      *(DB_DATE *) record_p = *(DB_DATE *) key_p;
      break;

    case DB_TYPE_TIME:
      *(DB_TIME *) record_p = *(DB_TIME *) key_p;
      break;

    case DB_TYPE_TIMESTAMP:
      *(DB_UTIME *) record_p = *(DB_UTIME *) key_p;
      break;

    case DB_TYPE_DATETIME:
      *(DB_DATETIME *) record_p = *(DB_DATETIME *) key_p;
      break;

    case DB_TYPE_MONETARY:
      OR_MOVE_DOUBLE (&((DB_MONETARY *) key_p)->amount, record_p);
      break;
#endif
    default:
      /* Unspecified key type: Directory header has been corrupted */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      ehash_free_recdes (recdes_p);
      return ER_EH_CORRUPTED;
    }

  return NO_ERROR;
}

/*
 * ehash_compose_record () -
 *   return: NO_ERROR, or ER_FAILED
 *   key_type(in): type of the key
 *   key_ptr(in): Pointer to the key
 *   value_ptr(in): Pointer to the associated value
 *   recdes(in): Pointer to the Record descriptor to fill in
 *
 * Note: This function prepares a record and sets the fields of the
 * passed record descriptor to describe it. All of the records
 * prepared by this function contain the
 * (associated_value, key_value) pairs (in this order). However,
 * these two fields may optionally be preceeded with the "ehid"
 * information. The records with the "ehid" information are used
 * for logging purposes. The ones without ehid information are
 * used to insert entries into the bucket pages. The dynamic
 * memory area allocated by this function for the record should
 * be freed by the caller function.
 *
 * If the key is of string type and it is > EHASH_MAX_STRING_SIZE, we
 * only include a prefix to the key. The caller is responsible
 * for the overflow part.
 */
static int
ehash_compose_record (DB_TYPE key_type, void *key_p, OID * value_p, RECDES * recdes_p)
{
  short key_size;
  int record_size;
  bool is_long_str = false;

  if (key_type == DB_TYPE_STRING)
    {
      key_size = (short) strlen ((char *) key_p) + 1;	/* Plus one is for \0 */

      /* max length of class name is 255 */
      assert (key_size <= 256);

#if defined (ENABLE_UNUSED_FUNCTION)
      /* Check if string is too long */
      if (key_size > EHASH_MAX_STRING_SIZE)
	{
	  is_long_str = true;
	  key_size = EHASH_LONG_STRING_PREFIX_SIZE;
	}
#endif

      record_size = sizeof (OID) + key_size;

#if defined (ENABLE_UNUSED_FUNCTION)
      /* If an overflow page is needed, reserve space for it */
      if (is_long_str)
	{
	  record_size += sizeof (VPID);
	}
#endif
    }
  else
    {
      key_size = ehash_get_key_size (key_type);
      if (key_size < 0)
	{
	  return ER_FAILED;
	}
      record_size = sizeof (OID) + key_size;
    }

  if (ehash_allocate_recdes (recdes_p, record_size, REC_HOME) == NULL)
    {
      return ER_FAILED;
    }

  return ehash_write_key_to_record (recdes_p, key_type, key_p, key_size, value_p, is_long_str);
}

static int
ehash_compare_key (THREAD_ENTRY * thread_p, char *bucket_record_p, DB_TYPE key_type, void *key_p, INT16 record_type,
		   int *out_compare_result_p)
{
  int compare_result;
#if defined (ENABLE_UNUSED_FUNCTION)
  VPID *ovf_vpid_p;
  double d1, d2;
  float f1, f2;
  DB_DATETIME *dt1, *dt2;
  DB_BIGINT bi1, bi2;
#endif

  switch (key_type)
    {
    case DB_TYPE_STRING:
#if defined (ENABLE_UNUSED_FUNCTION)
      if (record_type == REC_BIGONE)
	{
	  /* bucket record is a LOG key string */
	  ovf_vpid_p = (VPID *) bucket_record_p;
	  bucket_record_p += sizeof (VPID);

	  compare_result = ehash_ansi_sql_strncmp ((char *) key_p, bucket_record_p, EHASH_LONG_STRING_PREFIX_SIZE);
	  if (!compare_result)
	    {
	      /*
	       * The prefix of the bucket string matches with the given key.
	       * It is very likely that the whole key will match. So, retrive
	       * the chopped portion of the bucket string and compare it with
	       * the chopped portion of the key.
	       */
	      if (ehash_compare_overflow (thread_p, ovf_vpid_p, (char *) key_p + EHASH_LONG_STRING_PREFIX_SIZE,
					  &compare_result) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	}
      else
#endif
	{
	  compare_result = ansisql_strcmp ((char *) key_p, bucket_record_p);
	}
      break;

    case DB_TYPE_OBJECT:
      compare_result = oid_compare ((OID *) key_p, (OID *) bucket_record_p);
      break;
#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_DOUBLE:
      OR_MOVE_DOUBLE (key_p, &d1);
      OR_MOVE_DOUBLE (bucket_record_p, &d2);

      if (d1 == d2)
	{
	  compare_result = 0;
	}
      else if (d1 > d2)
	{
	  compare_result = 1;
	}
      else
	{
	  compare_result = -1;
	}
      break;

    case DB_TYPE_FLOAT:
      f1 = *((float *) key_p);
      f2 = *((float *) bucket_record_p);

      if (f1 == f2)
	{
	  compare_result = 0;
	}
      else if (f1 > f2)
	{
	  compare_result = 1;
	}
      else
	{
	  compare_result = -1;
	}
      break;

    case DB_TYPE_INTEGER:
      compare_result = *(int *) key_p - *(int *) bucket_record_p;
      break;

    case DB_TYPE_BIGINT:
      bi1 = *((DB_BIGINT *) key_p);
      bi2 = *((DB_BIGINT *) bucket_record_p);

      if (bi1 == bi2)
	{
	  compare_result = 0;
	}
      else if (bi1 > bi2)
	{
	  compare_result = 1;
	}
      else
	{
	  compare_result = -1;
	}
      break;

    case DB_TYPE_SHORT:
      compare_result = *(short *) key_p - *(short *) bucket_record_p;
      break;

    case DB_TYPE_DATE:
      compare_result = *(DB_DATE *) key_p - *(DB_DATE *) bucket_record_p;
      break;

    case DB_TYPE_TIME:
      compare_result = *(DB_TIME *) key_p - *(DB_TIME *) bucket_record_p;
      break;

    case DB_TYPE_TIMESTAMP:
      compare_result = *(DB_UTIME *) key_p - *(DB_UTIME *) bucket_record_p;
      break;

    case DB_TYPE_DATETIME:
      dt1 = (DB_DATETIME *) key_p;
      dt2 = (DB_DATETIME *) bucket_record_p;

      if (dt1->date < dt2->date)
	{
	  compare_result = -1;
	}
      else if (dt1->date > dt2->date)
	{
	  compare_result = 1;
	}
      else if (dt1->time < dt2->time)
	{
	  compare_result = -1;
	}
      else if (dt1->time > dt2->time)
	{
	  compare_result = 1;
	}
      else
	{
	  compare_result = 0;
	}
      break;

    case DB_TYPE_MONETARY:
      OR_MOVE_DOUBLE (&((DB_MONETARY *) key_p)->amount, &d1);
      OR_MOVE_DOUBLE (&((DB_MONETARY *) bucket_record_p)->amount, &d2);

      if (d1 == d2)
	{
	  compare_result = 0;
	}
      else if (d1 > d2)
	{
	  compare_result = 1;
	}
      else
	{
	  compare_result = -1;
	}
      break;
#endif
    default:
      /* Unspecified key type: Directory header has been corrupted */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      return ER_EH_CORRUPTED;
    }

  *out_compare_result_p = compare_result;
  return NO_ERROR;
}

static bool
ehash_binary_search_bucket (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, PGSLOTID num_record, DB_TYPE key_type,
			    void *key_p, PGSLOTID * out_position_p)
{
  char *bucket_record_p;
  RECDES recdes;
  PGSLOTID low, high;
  PGSLOTID middle;
  int compare_result;

  low = 1;
  high = num_record;

  do
    {
      middle = (high + low) >> 1;

      if (spage_get_record (thread_p, bucket_page_p, middle, &recdes, PEEK) != S_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
	  return false;
	}

      bucket_record_p = (char *) recdes.data;
      bucket_record_p += sizeof (OID);

      if (ehash_compare_key (thread_p, bucket_record_p, key_type, key_p, recdes.type, &compare_result) != NO_ERROR)
	{
	  return false;
	}

      if (compare_result == 0)
	{
	  *out_position_p = middle;
	  return true;
	}

      if (compare_result < 0)
	{
	  high = middle - 1;
	}
      else
	{
	  low = middle + 1;
	}
    }
  while (high >= low);

  if (high < middle)
    {
      *out_position_p = middle;
    }
  else
    {
      /* Key is NOT in bucket; it should be located at the right of middle key. */
      *out_position_p = middle + 1;
    }

  return false;
}

/*
 * ehash_locate_slot () - Locate the slot for the key
 *   return: int
 *   buc_pgptr(in): pointer to the bucket page
 *   key_type(in):
 *   key(in): key value to search
 *   position(out): set to the location of the slot that contains the key if
 *                  it exists, or that would contain the key if it does not.
 *
 * Note: This function locates a specific key in a bucket
 * (namely, the slot number in the page). Currently this search
 * is done by using the binary search method on the real key
 * values. An alternative method is to use another hashing
 * mechanism on the pseudo keys. If the key does not exist,
 * position is set to the SLOTID value where it could be
 * inserted and false is returned.
 */
static bool
ehash_locate_slot (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, DB_TYPE key_type, void *key_p,
		   PGSLOTID * out_position_p)
{
  RECDES recdes;
  PGSLOTID num_record;
  PGSLOTID first_slot_id = -1;

  num_record = spage_number_of_records (bucket_page_p) - 1;

  /*
   * If the bucket does not contain any records other than the header,
   * then return immediately.
   */
  if (num_record < 1)
    {
      if (spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK) != S_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
	  *out_position_p = 1;
	  return false;
	}

      *out_position_p = first_slot_id + 1;
      return false;
    }

  return ehash_binary_search_bucket (thread_p, bucket_page_p, num_record, key_type, key_p, out_position_p);
}

static int
ehash_get_pseudo_key (THREAD_ENTRY * thread_p, RECDES * recdes_p, DB_TYPE key_type, EHASH_HASH_KEY * out_hash_key_p)
{
  EHASH_HASH_KEY hash_key;
  char *bucket_record_p;
#if defined (ENABLE_UNUSED_FUNCTION)
  char *whole_key_p;
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
  if (recdes_p->type == REC_BIGONE)
    {
      /*
       * The record contains a long string Compose the whole key and find
       * pseudo key by using the whole key
       */
      whole_key_p = ehash_compose_overflow (thread_p, recdes_p);
      if (whole_key_p == NULL)
	{
	  return ER_FAILED;
	}
      hash_key = ehash_hash (whole_key_p, key_type);
      free_and_init (whole_key_p);
    }
  else
#endif
    {
      bucket_record_p = (char *) recdes_p->data;
      bucket_record_p += sizeof (OID);
      hash_key = ehash_hash (bucket_record_p, key_type);
    }

  *out_hash_key_p = hash_key;
  return NO_ERROR;
}

static int
ehash_find_first_bit_position (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header_p, PAGE_PTR bucket_page_p,
			       EHASH_BUCKET_HEADER * bucket_header_p, void *key_p, int num_recs, PGSLOTID first_slot_id,
			       int *out_old_local_depth_p, int *out_new_local_depth_p)
{
  int bit_position;
  unsigned int i;
  unsigned int difference = 0, check_bit = 0;
  EHASH_HASH_KEY first_hash_key, next_hash_key;
  PGSLOTID slot_id;
  RECDES recdes;

  check_bit = 1 << (EHASH_HASH_KEY_BITS - bucket_header_p->local_depth - 1);

  /* Get the first pseudo key */
  first_hash_key = ehash_hash (key_p, dir_header_p->key_type);

  for (slot_id = first_slot_id + 1; slot_id < num_recs; slot_id++)
    {
      if (spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK) != S_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
	  return ER_EH_CORRUPTED;
	}

      if (ehash_get_pseudo_key (thread_p, &recdes, dir_header_p->key_type, &next_hash_key) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      difference = (first_hash_key ^ next_hash_key) | difference;
      if (difference & check_bit)
	{
	  break;
	}
    }

  /* Initilize bit_position to one greater than the old local depth */
  bit_position = *out_old_local_depth_p + 1;

  /* Find out the correct bit_position that the keys differ */
  for (i = bucket_header_p->local_depth + 1; i <= EHASH_HASH_KEY_BITS; i++)
    {
      if (difference & check_bit)
	{
	  bit_position = i;
	  break;
	}

      /* Shift the check bit position to right */
      check_bit >>= 1;
    }

  /* Update the bucket header and the variable parameter to new local depth */
  bucket_header_p->local_depth = bit_position;
  *out_new_local_depth_p = bit_position;

  return NO_ERROR;
}

static int
ehash_distribute_records_into_two_bucket (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header_p,
					  PAGE_PTR bucket_page_p, EHASH_BUCKET_HEADER * bucket_header_p, int num_recs,
					  PGSLOTID first_slot_id, PAGE_PTR sibling_page_p)
{
  PGSLOTID slot_id, sibling_slot_id;
  RECDES recdes;
  EHASH_HASH_KEY hash_key;
  int i, success;

  for (slot_id = i = first_slot_id + 1; i < num_recs; i++)
    {
      if (spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      if (ehash_get_pseudo_key (thread_p, &recdes, dir_header_p->key_type, &hash_key) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (GETBIT (hash_key, bucket_header_p->local_depth))
	{
	  success = spage_insert (thread_p, sibling_page_p, &recdes, &sibling_slot_id);
	  if (success != SP_SUCCESS)
	    {
#ifdef EHASH_DEBUG
	      er_log_debug (ARG_FILE_LINE,
			    "Unable to move the record from the bucket to empty sibling bucket. Slotted page module"
			    " refuses to insert a short size record to an empty page. This should never happen.");
#endif
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

	      return ER_FAILED;
	    }
	  (void) spage_delete (thread_p, bucket_page_p, slot_id);
	  /*
	   * slotid is not changed since the current slot is deleted and the
	   * next slot will take its slotid.
	   */
	}
      else
	{
	  /* skip the slot */
	  slot_id++;
	}
    }

  return NO_ERROR;
}

/*
 * ehash_split_bucket () - Split a bucket into two
 *   return: PAGE_PTR (Sibling Pageptr), NULL in case of error in split.
 *   dir_header(in): directory header;
 *   buc_pgptr(in): pointer to bucket page to split
 *   key(in): key value to insert after split
 *   old_local_depth(in): old local depth before the split operation; to be set
 *   new_local_depth(in): new local depth after the split operation; to be set
 *   sib_vpid(in): vpid of the sibling bucket; to be set
 *   is_temp(in): true if file is temporary (logging not required)
 *
 * Note: This function splits the given bucket into two buckets.
 * First, the new local depth of the bucket is found. Then a
 * sibling bucket is allocated, and the records are
 * redistributed. The contents of the sibling bucket is logged
 * for recovery purposes. (Note that the contents of the original
 * bucket page that is splitting is logged in the insertion
 * function after this function returns. Thus, this function
 * does not release the buffer that contains this page). Finally,
 * the given (key & assoc_value) pair is tried to be inserted
 * onto the correct bucket. The result of this attempt is
 * returned. (Note that this split may cause only one very
 * small record separated from the others, and the new record
 * may still be too big to fit into the bucket.) In addition to
 * the return value, three parameters are set so that the
 * directory can be updated to reflect this split.
 */
static PAGE_PTR
ehash_split_bucket (THREAD_ENTRY * thread_p, EHASH_DIR_HEADER * dir_header_p, PAGE_PTR bucket_page_p, void *key_p,
		    int *out_old_local_depth_p, int *out_new_local_depth_p, VPID * sibling_vpid_p, bool is_temp)
{
  EHASH_BUCKET_HEADER *bucket_header_p;
  VFID bucket_vfid;
  PAGE_PTR sibling_page_p;
  RECDES recdes;
  PGSLOTID first_slot_id = -1;
  int num_records;
  char init_bucket_data[3];

  int error_code = NO_ERROR;

  if (!is_temp)
    {
      /* Log the contents of the bucket page before the split operation */
      log_append_undo_data2 (thread_p, RVEH_REPLACE, &dir_header_p->bucket_file, bucket_page_p, 0, DB_PAGESIZE,
			     bucket_page_p);
    }

  num_records = spage_number_of_records (bucket_page_p);
  /* Retrieve the bucket header and update it */
  if (spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      return NULL;
    }

  bucket_header_p = (EHASH_BUCKET_HEADER *) recdes.data;
  *out_old_local_depth_p = bucket_header_p->local_depth;

  if (ehash_find_first_bit_position (thread_p, dir_header_p, bucket_page_p, bucket_header_p, key_p, num_records,
				     first_slot_id, out_old_local_depth_p, out_new_local_depth_p) != NO_ERROR)
    {
      return NULL;
    }

  bucket_vfid = dir_header_p->bucket_file;
  init_bucket_data[0] = dir_header_p->alignment;
  init_bucket_data[1] = *out_new_local_depth_p;
  init_bucket_data[2] = is_temp;

  error_code = file_alloc (thread_p, &bucket_vfid, ehash_initialize_bucket_new_page, init_bucket_data, sibling_vpid_p,
			   &sibling_page_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }
  if (sibling_page_p == NULL)
    {
      assert_release (false);
      return NULL;
    }

  if (ehash_distribute_records_into_two_bucket (thread_p, dir_header_p, bucket_page_p, bucket_header_p, num_records,
						first_slot_id, sibling_page_p) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
      if (file_dealloc (thread_p, &bucket_vfid, sibling_vpid_p, FILE_EXTENDIBLE_HASH) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      VPID_SET_NULL (sibling_vpid_p);
      return NULL;
    }

  if (!is_temp)
    {
      /*
       * TODO: We are logging too much. It is unlikely that the page is full.
       *       we just distribute the keys among two buckets. It may be better
       *       to use crumbs to log different portions of the page.
       */
      log_append_redo_data2 (thread_p, RVEH_REPLACE, &dir_header_p->bucket_file, bucket_page_p, 0, DB_PAGESIZE,
			     bucket_page_p);
      log_append_redo_data2 (thread_p, RVEH_REPLACE, &dir_header_p->bucket_file, sibling_page_p, 0, DB_PAGESIZE,
			     sibling_page_p);
    }

  pgbuf_set_dirty (thread_p, sibling_page_p, DONT_FREE);
  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);

  return sibling_page_p;
}

/*
 * ehash_expand_directory () - Expand directory
 *   return: NO_ERROR, or ER_FAILED
 *   ehid(in): identifier for the extendible hashing structure to expand
 *   new_depth(in): requested new depth
 *   is_temp(in): true if file is temporary (logging not required)
 *
 * Note: This function expands the given extendible hashing directory
 * as many times as necessary to attain the requested depth.
 * This is performed with a single pass on the original
 * directory (starting from the last pointer and working through
 * the first one).
 * During this operation, the directory is locked with X lock to
 * prevent others from accessing wrong information. If the
 * original directory is not large enough some new pages are
 * allocated for the new directory. If the disk becomes full
 * during this operation, or any system error occurs, the
 * directory expansion is canceled and an error code is
 * returned. For recovery purposes, the contents of the
 * directory pages are logged during the expansion operation.
 */
static int
ehash_expand_directory (THREAD_ENTRY * thread_p, EHID * ehid_p, int new_depth, bool is_temp)
{
  int old_pages;
  int old_ptrs;
  int new_pages;
  int new_ptrs;
  int check_pages;

  PAGE_PTR dir_header_page_p;
  EHASH_DIR_HEADER *dir_header_p;
  int end_offset;
  int new_end_offset;
  int needed_pages;
  int i, j;
  int exp_times;

  PAGE_PTR old_dir_page_p;
  PGLENGTH old_dir_offset;
  int old_dir_nth_page;

  PAGE_PTR new_dir_page_p;
  PGLENGTH new_dir_offset;
  int new_dir_nth_page;

  int error_code = NO_ERROR;

  dir_header_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_WRITE);
  if (dir_header_page_p == NULL)
    {
      return ER_FAILED;
    }
  dir_header_p = (EHASH_DIR_HEADER *) dir_header_page_p;

  error_code = file_get_num_user_pages (thread_p, &ehid_p->vfid, &old_pages);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, dir_header_page_p);
      return error_code;
    }
  old_pages -= 1;		/* The first page starts with 0 */
  old_ptrs = 1 << dir_header_p->depth;
  exp_times = 1 << (new_depth - dir_header_p->depth);

  new_ptrs = old_ptrs * exp_times;
  end_offset = old_ptrs - 1;	/* Dir first pointer has an offset of 0 */
  ehash_dir_locate (&check_pages, &end_offset);

#ifdef EHASH_DEBUG
  if (check_pages != old_pages)
    {
      pgbuf_unfix_and_init (thread_p, dir_header_page_p);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, ehid_p->vfid.volid, ehid_p->vfid.fileid,
	      ehid_p->pageid);
      return ER_FAILED;
    }
#endif

  /* Find how many pages the expanded directory will occupy */
  new_end_offset = new_ptrs - 1;
  ehash_dir_locate (&new_pages, &new_end_offset);
  needed_pages = new_pages - old_pages;
  if (needed_pages > 0)
    {
      error_code = file_alloc_multiple (thread_p, &ehid_p->vfid, ehash_initialize_dir_new_page, &is_temp, needed_pages,
					NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, dir_header_page_p);
	  return error_code;
	}
    }

  /*****************************
     Perform expansion
   ******************************/

  /* Initialize source variables */
  old_dir_nth_page = old_pages;	/* The last page of the old directory */

  old_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, old_dir_nth_page, PGBUF_LATCH_WRITE);
  if (old_dir_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, dir_header_page_p);
      return ER_FAILED;
    }
  old_dir_offset = end_offset;

  /* Initialize destination variables */
  new_dir_nth_page = new_pages;	/* The last page of the new directory */

  new_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, new_dir_nth_page, PGBUF_LATCH_WRITE);
  if (new_dir_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, dir_header_page_p);
      pgbuf_unfix_and_init (thread_p, old_dir_page_p);
      return ER_FAILED;
    }
  new_dir_offset = new_end_offset;

  if (new_dir_nth_page <= old_pages && !is_temp)
    {
      /*
       * This page is part of old directory.
       * Log the initial content of this original directory page
       */
      log_append_undo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE, new_dir_page_p);
    }

  /* Copy old directory records to new (expanded) area */
  for (j = old_ptrs; j > 0; j--)
    {
      if (old_dir_offset < 0)
	{
	  /*
	   * We reached the end of the old directory page.
	   * The next bucket pointer is in the previous directory page
	   */
	  pgbuf_unfix_and_init (thread_p, old_dir_page_p);

	  /* get another page */
	  old_dir_nth_page--;

	  old_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, old_dir_nth_page, PGBUF_LATCH_WRITE);
	  if (old_dir_page_p == NULL)
	    {
	      /* Fetch error; so return */
	      pgbuf_unfix_and_init (thread_p, dir_header_page_p);
	      pgbuf_unfix_and_init (thread_p, new_dir_page_p);
	      return ER_FAILED;
	    }

	  /*
	   * Set the olddir_offset to the offset of the last pointer in the
	   * current source directory page.
	   */
	  if (old_dir_nth_page)
	    {
	      /* This is not the first (root) directory page */
	      old_dir_offset = EHASH_LAST_OFFSET_IN_NON_FIRST_PAGE;
	    }
	  else
	    {
	      /* This is the first (root) directory page */
	      old_dir_offset = EHASH_LAST_OFFSET_IN_FIRST_PAGE;
	    }
	}

      /* Copy the next directory record "exp_times" times */
      for (i = 1; i <= exp_times; i++)
	{
	  if (new_dir_offset < 0)
	    {
	      /*
	       * There is not any more entries in the new directory page.
	       * Log this updated directory page, and get next one
	       */
	      if (!is_temp)
		{
		  log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE,
					 new_dir_page_p);
		}
	      pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

	      /* get another page */
	      new_dir_nth_page--;

	      new_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, new_dir_nth_page, PGBUF_LATCH_WRITE);
	      if (new_dir_page_p == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, dir_header_page_p);
		  pgbuf_unfix_and_init (thread_p, old_dir_page_p);
		  return ER_FAILED;
		}

	      if (new_dir_nth_page <= old_pages && !is_temp)
		{
		  /* Log the initial content of this original directory page */
		  log_append_undo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE,
					 new_dir_page_p);
		}

	      /* Set the newdir_offset to the offset of the last pointer in the current destination directory page. */
	      if (new_dir_nth_page != 0)
		{
		  /* This is not the first (root) dir page */
		  new_dir_offset = EHASH_LAST_OFFSET_IN_NON_FIRST_PAGE;
		}
	      else
		{
		  /* This is the first (root) dir page */
		  new_dir_offset = EHASH_LAST_OFFSET_IN_FIRST_PAGE;
		}
	    }

	  if ((char *) new_dir_page_p + new_dir_offset != (char *) old_dir_page_p + old_dir_offset)
	    {
	      memcpy ((char *) new_dir_page_p + new_dir_offset, (char *) old_dir_page_p + old_dir_offset,
		      sizeof (EHASH_DIR_RECORD));
	    }

	  /* Advance the destination pointer to new spot */
	  new_dir_offset -= sizeof (EHASH_DIR_RECORD);
	}

      /* Advance the source pointer to new spot */
      old_dir_offset -= sizeof (EHASH_DIR_RECORD);
    }

  /*
   * Update the directory header
   */
  dir_header_p->depth = new_depth;

  if (!is_temp)
    {
      /* Log the first directory page which also contains the directory header */
      log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE, new_dir_page_p);
    }

  /* Release the old and new directory pages */
  pgbuf_unfix_and_init (thread_p, old_dir_page_p);
  pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

#ifdef EHASH_DEBUG
  {
    DKNPAGES npages_bucket = 0;
    DKNPAGES npages_ehash = 0;
    if (file_get_num_user_pages (thread_p, &dir_header_p->bucket_file, &npages_bucket) == NO_ERROR
	&& file_get_num_user_pages (thread_p, &ehid_p->vfid, &npages_ehash) == NO_ERROR)
      {
	if (new_ptrs / npages > EHASH_BALANCE_FACTOR || npages_ehash > npages_bucket)
	  {
	    er_log_debug (ARG_FILE_LINE,
			  "WARNING: Ext. Hash EHID=%d|%d|%d is unbalanced.\n Num_bucket_pages = %d, "
			  "num_dir_pages = %d,\n num_dir_pointers = %d, directory_depth = %d.\n"
			  " You may want to consider another hash function.\n", ehid_p->vfid.volid,
			  ehid_p->vfid.fileid, ehid_p->pageid, npages_bucket, npages_ehash,
			  new_ptrs, dir_header_p->depth);
	  }
      }
  }
#endif /* EHASH_DEBUG */

  /* Release the root page holding the directory header */
  pgbuf_unfix_and_init (thread_p, dir_header_page_p);

  return NO_ERROR;
}

/*
 * ehash_connect_bucket () - Connect bucket to directory
 *   return: int NO_ERROR, or ER_FAILED
 *   ehid(in): identifier for the extendible hashing structure
 *   local_depth(in): local depth of the bucket; determines how many directory
 *                    pointers are set to point to the given bucket identifier
 *   hash_key(in): pseudo key that led to the bucket page; determines which
 *                   pointers to be updated
 *   bucket_vpid(in): Identifier of the bucket page to connect
 *   is_temp(in): true if temporary (logging is not required)
 *
 * Note: This function connects the given bucket to the directory of
 * specified extendible hashing structure. All the directory
 * pointers whose number have the same value as the hash_key
 * for the first "local_depth" bits are updated with "bucket_vpid".
 * Since these pointers are successive to each other, it is
 * known that a contagious area (possibly over several pages)
 * on the directory will be updated. Thus, this function is
 * implemented in the following way: First,  the directory pages
 * to be updated are determined. Then, these pages are retrieved,
 * updated and released one at a time. For recovery purposes, the
 * contents of these pages are logged before and after they are updated.
 */
static int
ehash_connect_bucket (THREAD_ENTRY * thread_p, EHID * ehid_p, int local_depth, EHASH_HASH_KEY hash_key,
		      VPID * bucket_vpid_p, bool is_temp)
{
  EHASH_DIR_HEADER dir_header;
  EHASH_DIR_RECORD *dir_record_p;
  EHASH_REPETITION repetition;
  PAGE_PTR page_p;
  int location;
  int first_page, last_page;
  int first_ptr_offset, last_ptr_offset;
  int start_offset, end_offset;
  int diff;
  int i, j;
  int bef_length;
  unsigned int set_bits;
  unsigned int clear_bits;

  repetition.vpid = *bucket_vpid_p;

  page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_READ);
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  memcpy (&dir_header, page_p, EHASH_DIR_HEADER_SIZE);
  pgbuf_unfix_and_init (thread_p, page_p);

  /* First find out how many page entries will be updated in the directory */
  location = GETBITS (hash_key, 1, dir_header.depth);

  diff = dir_header.depth - local_depth;
  if (diff != 0)
    {
      /* There are more than one pointers that need to be updated */
      for (set_bits = 0, i = 0; i < diff; i++)
	{
	  set_bits = 2 * set_bits + 1;
	}
      clear_bits = ~set_bits;

      first_ptr_offset = location & clear_bits;
      last_ptr_offset = location | set_bits;

      ehash_dir_locate (&first_page, &first_ptr_offset);
      ehash_dir_locate (&last_page, &last_ptr_offset);
    }
  else
    {
      /* There is only one pointer that needs to be updated */
      first_ptr_offset = location;
      ehash_dir_locate (&first_page, &first_ptr_offset);
      last_page = first_page;
      last_ptr_offset = first_ptr_offset;
    }

  /* Go over all of these pages */
  for (i = first_page; i <= last_page; i++)
    {
      page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, i, PGBUF_LATCH_WRITE);
      if (page_p == NULL)
	{
	  return ER_FAILED;
	}

      if (i == first_page)
	{
	  start_offset = first_ptr_offset;
	}
      else
	{
	  start_offset = 0;
	}

      if (i == last_page)
	{
	  end_offset = last_ptr_offset + sizeof (EHASH_DIR_RECORD);
	}
      else
	{
	  end_offset = DB_PAGESIZE;
	}

      /* Log this page */
      bef_length = end_offset - start_offset;
      repetition.count = bef_length / sizeof (EHASH_DIR_RECORD);

      if (!is_temp)
	{
	  log_append_undoredo_data2 (thread_p, RVEH_CONNECT_BUCKET, &ehid_p->vfid, page_p, start_offset, bef_length,
				     sizeof (EHASH_REPETITION), (char *) page_p + start_offset, &repetition);
	}

      /* Update the directory page */
      dir_record_p = (EHASH_DIR_RECORD *) ((char *) page_p + start_offset);
      for (j = 0; j < repetition.count; j++)
	{
	  dir_record_p->bucket_vpid = *bucket_vpid_p;
	  dir_record_p++;
	}
      pgbuf_set_dirty (thread_p, page_p, FREE);
    }

  return NO_ERROR;
}

/*
 * ehash_find_depth () - Find depth
 *   return: char (will return 0 in case of error)
 *   ehid(in): identifier for the extendible hashing structure
 *   location(in): directory entry whose neighbooring area should be checked
 *   bucket_vpid(in): vpid of the bucket pointed by the specified directory entry
 *   sib_vpid(in): vpid of the sibling bucket
 *
 */
static char
ehash_find_depth (THREAD_ENTRY * thread_p, EHID * ehid_p, int location, VPID * bucket_vpid_p, VPID * sibling_vpid_p)
{
  EHASH_DIR_HEADER dir_header;
  EHASH_DIR_RECORD *dir_record_p;
  PAGE_PTR page_p;
  int loc;
  int rel_loc;
  int iterations;
  int page_no;
  int prev_page_no;
  int i;
  unsigned int clear;
  char check_depth = 2;
  bool is_stop = false;
  int check_bit;

  prev_page_no = 0;
  page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_WRITE);
  if (page_p == NULL)
    {
      return 0;
    }
  memcpy (&dir_header, page_p, EHASH_DIR_HEADER_SIZE);

  while ((check_depth <= dir_header.depth) && !is_stop)
    {
      /*
       * Find the base location for this iteration. The base location differs from
       * the original location at the check_depth bit (it has the opposite value
       * for this bit position) and at the remaining least significant bit
       * positions (it has 0 for these bit positions).
       */
      clear = 1;
      for (i = 1; i < check_depth - 1; i++)
	{
	  clear <<= 1;
	  clear += 1;
	}

      clear = ~clear;
      loc = location & clear;

      check_bit = GETBIT (loc, EHASH_HASH_KEY_BITS - check_depth + 1);
      if (check_bit != 0)
	{
	  loc = CLEARBIT (loc, EHASH_HASH_KEY_BITS - check_depth + 1);
	}
      else
	{
	  loc = SETBIT (loc, EHASH_HASH_KEY_BITS - check_depth + 1);
	}

      /* "loc" is the base_location now */

      iterations = 1 << (check_depth - 2);

      for (i = 0; i < iterations; i++)
	{
	  rel_loc = loc | (i << 1);
	  ehash_dir_locate (&page_no, &rel_loc);
	  if (page_no != prev_page_no)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	      page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, page_no, PGBUF_LATCH_WRITE);
	      if (page_p == NULL)
		{
		  return 0;
		}
	      prev_page_no = page_no;
	    }
	  dir_record_p = (EHASH_DIR_RECORD *) ((char *) page_p + rel_loc);

	  if (!((VPID_ISNULL (&dir_record_p->bucket_vpid)) || VPID_EQ (&dir_record_p->bucket_vpid, bucket_vpid_p)
		|| VPID_EQ (&dir_record_p->bucket_vpid, sibling_vpid_p)))
	    {
	      is_stop = true;
	      break;
	    }
	}

      if (!is_stop)
	{
	  check_depth++;
	}
    }

  pgbuf_unfix_and_init (thread_p, page_p);
  return (check_depth - 1);
}

static EHASH_RESULT
ehash_check_merge_possible (THREAD_ENTRY * thread_p, EHID * ehid_p, EHASH_DIR_HEADER * dir_header_p,
			    VPID * bucket_vpid_p, PAGE_PTR bucket_page_p, int location, int lock_type,
			    int *out_old_local_depth_p, VPID * sibling_vpid_p, PAGE_PTR * out_sibling_page_p,
			    PGSLOTID * out_first_slot_id_p, int *out_num_records_p, int *out_location_p)
{
  EHASH_BUCKET_HEADER *bucket_header_p;
  EHASH_BUCKET_HEADER sibling_bucket_header;
  PAGE_PTR sibling_page_p;

  RECDES recdes;
  PGSLOTID first_slot_id = -1;

  int num_records;
  int transfer;
  int already;
  int check_bit;
  int loc = location;

  if (spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      return EHASH_ERROR_OCCURRED;
    }

  bucket_header_p = (EHASH_BUCKET_HEADER *) recdes.data;

  *out_old_local_depth_p = bucket_header_p->local_depth;
  if (bucket_header_p->local_depth == 0)
    {
      /* Special case: there is only one bucket; so do not try to merge */
      return EHASH_NO_SIBLING_BUCKET;
    }

  /* find the location of sibling bucket pointer */
  check_bit = GETBIT (loc, EHASH_HASH_KEY_BITS - dir_header_p->depth + bucket_header_p->local_depth);
  if (check_bit == 0)
    {
      loc = SETBIT (loc, EHASH_HASH_KEY_BITS - dir_header_p->depth + bucket_header_p->local_depth);
    }
  else
    {
      loc = CLEARBIT (loc, EHASH_HASH_KEY_BITS - dir_header_p->depth + bucket_header_p->local_depth);
    }

  /*
   * Now, "loc" is the index of directory entry pointing to the sibling bucket
   * (of course, if the sibling bucket exists). So, find its absolute location.
   */
  if (ehash_find_bucket_vpid (thread_p, ehid_p, dir_header_p, loc, PGBUF_LATCH_READ, sibling_vpid_p) != NO_ERROR)
    {
      return EHASH_ERROR_OCCURRED;
    }

  if (VPID_ISNULL (sibling_vpid_p))
    {
      return EHASH_NO_SIBLING_BUCKET;
    }

  if (VPID_EQ (sibling_vpid_p, bucket_vpid_p))
    {
      /* Special case: there is only one bucket; so do not try to merge */
      return EHASH_NO_SIBLING_BUCKET;
    }

  sibling_page_p =
    ehash_fix_old_page (thread_p, &ehid_p->vfid, sibling_vpid_p,
			lock_type == S_LOCK ? PGBUF_LATCH_READ : PGBUF_LATCH_WRITE);

  if (sibling_page_p == NULL)
    {
      return EHASH_ERROR_OCCURRED;
    }

  /* retrieve the bucket header */
  recdes.area_size = sizeof (EHASH_BUCKET_HEADER);
  recdes.data = (char *) &sibling_bucket_header;

  first_slot_id = -1;
  if (spage_next_record (sibling_page_p, &first_slot_id, &recdes, COPY) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      pgbuf_unfix_and_init (thread_p, sibling_page_p);

      return EHASH_ERROR_OCCURRED;
    }

  if (sibling_bucket_header.local_depth != bucket_header_p->local_depth)
    {
      pgbuf_unfix_and_init (thread_p, sibling_page_p);

      return EHASH_NO_SIBLING_BUCKET;
    }

  /* Check free space of sibling */

  num_records = spage_number_of_records (bucket_page_p);

  if (num_records > 1)
    {
      /* Bucket page contains records that needs to be moved to the sibling page */
      transfer =
	(DB_PAGESIZE - (sizeof (EHASH_BUCKET_HEADER) + 2) - spage_max_space_for_new_record (thread_p, bucket_page_p));

      already = (DB_PAGESIZE - spage_max_space_for_new_record (thread_p, sibling_page_p));

      if ((already + transfer) > EHASH_OVERFLOW_THRESHOLD)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  return EHASH_FULL_SIBLING_BUCKET;
	}
    }

  if (lock_type == S_LOCK)
    {
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
    }
  else
    {
      *out_sibling_page_p = sibling_page_p;
      *out_first_slot_id_p = first_slot_id;
      *out_num_records_p = num_records;
      *out_location_p = loc;
    }

  return EHASH_SUCCESSFUL_COMPLETION;
}

/*
 * ehash_delete () - Delete key from ext. hashing
 *   return: void * (NULL is returned in case of error)
 *   ehid(in): identifier for the extendible hashing structure
 *   key(in): Key value to remove
 *
 * Note: Deletes the given key value (together with its assoc_value)
 * from the specified extendible hashing structure. If the key
 * does not exist then it returns an error code.
 * After it removes the entry from the bucket page, it produces
 * an operational log for this deletion operation. Then, it
 * checks the condition of the bucket. If the bucket is empty
 * or underflown, it releases the locks on the extendible
 * hashing structure and calls another function to merge the
 * bucket with its sibling bucket.
 */
void *
ehash_delete (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  PAGE_PTR bucket_page_p;
  VPID ovf_vpid VPID_INITIALIZER;
  VPID bucket_vpid;
  VPID sibling_vpid;

  RECDES bucket_recdes;
  RECDES log_recdes_undo;
  RECDES log_recdes_redo;
  char *log_undo_record_p;
  char *log_redo_record_p;

  int location;
  int old_local_depth;

  EHASH_RESULT result;
  PGSLOTID max_free;
  PGSLOTID slot_no;
  bool is_long_str = false;
  bool is_temp = false;

  bool do_merge;

  if (ehid_p == NULL || key_p == NULL)
    {
      return NULL;
    }

  if (file_is_temp (thread_p, &ehid_p->vfid, &is_temp) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  dir_root_page_p =
    ehash_find_bucket_vpid_with_hash (thread_p, ehid_p, key_p, PGBUF_LATCH_READ, PGBUF_LATCH_WRITE, &bucket_vpid, NULL,
				      &location);
  if (dir_root_page_p == NULL)
    {
      return NULL;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (bucket_vpid.pageid == NULL_PAGEID)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_EH_UNKNOWN_KEY, 0);
      return NULL;
    }
  else
    {
      bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, &bucket_vpid, PGBUF_LATCH_WRITE);
      if (bucket_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return NULL;
	}

      /* Try to delete key from buc_page */

      /* Check if deletion possible, or not */
      if (ehash_locate_slot (thread_p, bucket_page_p, dir_header_p->key_type, key_p, &slot_no) == false)
	{
	  /* Key does not exist, so return errorcode */
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_EH_UNKNOWN_KEY, 0);
	  return NULL;
	}
      else
	{
	  /* Key exists in the bucket */
	  (void) spage_get_record (thread_p, bucket_page_p, slot_no, &bucket_recdes, PEEK);

	  /* Prepare the undo log record */
	  if (ehash_allocate_recdes (&log_recdes_undo, bucket_recdes.length + sizeof (EHID), REC_HOME) == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return NULL;
	    }

	  log_undo_record_p = log_recdes_undo.data;

	  /* First insert the Ext. Hashing identifier */
	  log_undo_record_p = ehash_write_ehid_to_record (log_undo_record_p, ehid_p);

	  /* Copy (the assoc-value, key) pair from the bucket record */
	  memcpy (log_undo_record_p, bucket_recdes.data, bucket_recdes.length);

	  /* Prepare the redo log record */
	  if (ehash_allocate_recdes (&log_recdes_redo, bucket_recdes.length + sizeof (short), REC_HOME) == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return NULL;
	    }

	  log_redo_record_p = log_recdes_redo.data;

	  /* First insert the key type */
	  *(short *) log_redo_record_p = bucket_recdes.type;
	  log_redo_record_p += sizeof (short);

	  /* Copy (the assoc-value, key) pair from the bucket record */
	  memcpy (log_redo_record_p, bucket_recdes.data, bucket_recdes.length);

#if defined (ENABLE_UNUSED_FUNCTION)
	  /* Check if the record contains a long string */
	  if (bucket_recdes.type == REC_BIGONE)
	    {
	      ovf_vpid = *(VPID *) (bucket_recdes.data + sizeof (OID));
	      is_long_str = true;
	      /* Overflow pages needs to be removed, too. But, that shoud be done after the bucket record deletion has
	       * been logged. This way, we will have the overflow pages available when logical undo-delete recovery
	       * operation is called. Here just save the overflow page(s) id. */
	    }
#endif

	  /* Delete the bucket record from the bucket */
	  (void) spage_delete (thread_p, bucket_page_p, slot_no);
	  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);

	  /* Check the condition of bucket after the deletion */
	  if (spage_number_of_records (bucket_page_p) <= 1)
	    {
	      /* Bucket became empty after this deletion */
	      result = EHASH_BUCKET_EMPTY;
	    }
	  else
	    {
	      max_free = spage_max_space_for_new_record (thread_p, bucket_page_p);

	      /* Check if deletion had caused the Bucket to underflow, or not */
	      if ((DB_PAGESIZE - max_free) < EHASH_UNDERFLOW_THRESHOLD)
		{
		  /* Yes bucket has underflown */
		  result = EHASH_BUCKET_UNDERFLOW;
		}
	      else
		{
		  /* Bucket is just fine */
		  result = EHASH_SUCCESSFUL_COMPLETION;
		}
	    }

	  /* Log this deletion operation */

	  if (!is_temp)
	    {
	      log_append_undo_data2 (thread_p, RVEH_DELETE, &ehid_p->vfid, NULL, bucket_recdes.type,
				     log_recdes_undo.length, log_recdes_undo.data);

	      log_append_redo_data2 (thread_p, RVEH_DELETE, &ehid_p->vfid, bucket_page_p, slot_no,
				     log_recdes_redo.length, log_recdes_redo.data);
	    }
	  ehash_free_recdes (&log_recdes_undo);
	  ehash_free_recdes (&log_recdes_redo);

	  /* Remove the overflow pages, if any. */
	  if (is_long_str)
	    {
	      (void) overflow_delete (thread_p, &dir_header_p->overflow_file, &ovf_vpid);
	    }

	  /* Check for the merge condition */
	  do_merge = false;	/* init */
	  if (result == EHASH_BUCKET_EMPTY || result == EHASH_BUCKET_UNDERFLOW)
	    {
	      if (ehash_check_merge_possible
		  (thread_p, ehid_p, dir_header_p, &bucket_vpid, bucket_page_p, location, S_LOCK, &old_local_depth,
		   &sibling_vpid, NULL, NULL, NULL, NULL) == EHASH_SUCCESSFUL_COMPLETION)
		{
		  do_merge = true;
		}
	    }

	  /* Release directory and bucket and return */
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);

	  /* do merge routine */
	  if (do_merge)
	    {
	      ehash_merge (thread_p, ehid_p, key_p, is_temp);
	    }

	  return (key_p);
	}
    }
}

static void
ehash_shrink_directory_if_need (THREAD_ENTRY * thread_p, EHID * ehid_p, EHASH_DIR_HEADER * dir_header_p, bool is_temp)
{
  int i;

  /* Check directory shrink condition */
  i = dir_header_p->depth;
  while (dir_header_p->local_depth_count[i] == 0)
    {
      i--;
    }

  if ((dir_header_p->depth - i) > 1)
    {
      ehash_shrink_directory (thread_p, ehid_p, i + 1, is_temp);
    }
}

static void
ehash_adjust_local_depth (THREAD_ENTRY * thread_p, EHID * ehid_p, PAGE_PTR dir_root_page_p,
			  EHASH_DIR_HEADER * dir_header_p, int depth, int delta, bool is_temp)
{
  int redo_inc, undo_inc, offset;

  /* Update the directory header for local depth counters */
  dir_header_p->local_depth_count[depth] += delta;
  pgbuf_set_dirty (thread_p, dir_root_page_p, DONT_FREE);

  /* Log this change on the directory header */
  offset = CAST_BUFLEN ((char *) (dir_header_p->local_depth_count + depth) - (char *) dir_root_page_p);
  redo_inc = delta;
  undo_inc = -delta;

  if (!is_temp)
    {
      log_append_undoredo_data2 (thread_p, RVEH_INC_COUNTER, &ehid_p->vfid, dir_root_page_p, offset, sizeof (int),
				 sizeof (int), &undo_inc, &redo_inc);
    }
}

static int
ehash_merge_permanent (THREAD_ENTRY * thread_p, EHID * ehid_p, PAGE_PTR dir_root_page_p,
		       EHASH_DIR_HEADER * dir_header_p, PAGE_PTR bucket_page_p, PAGE_PTR sibling_page_p,
		       VPID * bucket_vpid_p, VPID * sibling_vpid_p, int num_records, int location,
		       PGSLOTID first_slot_id, int *out_new_local_depth_p, bool is_temp)
{
  PGSLOTID slot_id;
  RECDES recdes;
  char *bucket_record_p;
  PGSLOTID new_slot_id;
  bool is_record_exist;
  int success;
  EHASH_BUCKET_HEADER sibling_bucket_header;
  char found_depth;

  if (!is_temp)
    {
      log_append_undo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, sibling_page_p, 0, DB_PAGESIZE, sibling_page_p);
    }

  /* Move records of original page into the sibling bucket */
  for (slot_id = 1; slot_id < num_records; slot_id++)
    {
      spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK);
      bucket_record_p = (char *) recdes.data;
      bucket_record_p += sizeof (OID);

#if defined (ENABLE_UNUSED_FUNCTION)
      if (recdes.type == REC_BIGONE)	/* string type */
	{
	  bucket_record_p = ehash_compose_overflow (thread_p, &recdes);
	  if (bucket_record_p == NULL)
	    {
	      return ER_FAILED;
	    }
	}
#endif

      is_record_exist =
	ehash_locate_slot (thread_p, sibling_page_p, dir_header_p->key_type, (void *) bucket_record_p, &new_slot_id);
      if (is_record_exist == false)
	{
	  success = spage_insert_at (thread_p, sibling_page_p, new_slot_id, &recdes);
	  if (success != SP_SUCCESS)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
#if defined (ENABLE_UNUSED_FUNCTION)
	      if (recdes.type == REC_BIGONE)
		{
		  free_and_init (bucket_record_p);
		}
#endif
	      return ER_FAILED;
	    }
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
#if defined (ENABLE_UNUSED_FUNCTION)
	  if (recdes.type == REC_BIGONE)
	    {
	      free_and_init (bucket_record_p);
	    }
#endif
	  return ER_FAILED;
	}

#if defined (ENABLE_UNUSED_FUNCTION)
      if (recdes.type == REC_BIGONE)
	{
	  free_and_init (bucket_record_p);
	}
#endif
    }

  found_depth = ehash_find_depth (thread_p, ehid_p, location, bucket_vpid_p, sibling_vpid_p);
  if (found_depth == 0)
    {
      return ER_FAILED;
    }

  /* Update the sibling header */
  sibling_bucket_header.local_depth = dir_header_p->depth - found_depth;
  *out_new_local_depth_p = sibling_bucket_header.local_depth;

  /* Set the record descriptor to the sib_header */
  recdes.data = (char *) &sibling_bucket_header;
  recdes.area_size = sizeof (EHASH_BUCKET_HEADER);
  recdes.length = sizeof (EHASH_BUCKET_HEADER);
  (void) spage_update (thread_p, sibling_page_p, first_slot_id, &recdes);

  pgbuf_set_dirty (thread_p, sibling_page_p, DONT_FREE);

  if (!is_temp)
    {
      log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, sibling_page_p, 0, DB_PAGESIZE, sibling_page_p);
    }

  return NO_ERROR;
}

/*
 * ehash_merge () - If possible, merge two buckets
 *   return:
 *   ehid(in): identifier for the extendible hashing structure
 *   key(in): key value that has just been removed; used to locate the bucket.
 *   is_temp(in): true if temporary (logging is not required)
 *
 * Note: This function checks to determine if it is possible (and
 * desirable) to merge the bucket (that contained the given key
 * before the deletion) with its sibling bucket. If so, it starts
 * a permanent system operation and performs the merge. And then,
 * it checks the size of the directory. If the directory is too
 * big for the remaining buckets, it shrinks the directory to
 * a more tolerable size before concluding the system permanent operation.
 */
static void
ehash_merge (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p, bool is_temp)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  VPID bucket_vpid;
  PAGE_PTR bucket_page_p;
  PAGE_PTR sibling_page_p;
  VPID sibling_vpid;
  int location;
  int old_local_depth, new_local_depth;
  EHASH_HASH_KEY hash_key;
  int bucket_status;
  EHASH_RESULT check_result;
  PGSLOTID max_free;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  PGSLOTID first_slot_id = -1;
  int num_records, sibling_location;

  dir_root_page_p =
    ehash_find_bucket_vpid_with_hash (thread_p, ehid_p, key_p, PGBUF_LATCH_WRITE, PGBUF_LATCH_WRITE, &bucket_vpid,
				      &hash_key, &location);
  if (dir_root_page_p == NULL)
    {
      return;
    }
  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (bucket_vpid.pageid != NULL_PAGEID)
    {
      bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, &bucket_vpid, PGBUF_LATCH_WRITE);
      if (bucket_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return;
	}

      /* Check the status of bucket to see if merge is still needed */
      if (spage_number_of_records (bucket_page_p) <= 1)
	{
	  bucket_status = EHASH_BUCKET_EMPTY;
	}
      else
	{
	  max_free = spage_max_space_for_new_record (thread_p, bucket_page_p);

	  /* Check if the Bucket is underflown, or not */
	  if ((DB_PAGESIZE - max_free) < EHASH_UNDERFLOW_THRESHOLD)
	    {
	      bucket_status = EHASH_BUCKET_UNDERFLOW;
	    }
	  else
	    {
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      bucket_status = NO_ERROR;
	    }
	}

      if (bucket_status == EHASH_BUCKET_EMPTY || bucket_status == EHASH_BUCKET_UNDERFLOW)
	{
	  check_result =
	    ehash_check_merge_possible (thread_p, ehid_p, dir_header_p, &bucket_vpid, bucket_page_p, location, X_LOCK,
					&old_local_depth, &sibling_vpid, &sibling_page_p, &first_slot_id, &num_records,
					&sibling_location);

	  switch (check_result)
	    {
	    case EHASH_NO_SIBLING_BUCKET:
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);

	      if ((bucket_status == EHASH_BUCKET_EMPTY) && (old_local_depth != 0))
		{
		  if (!is_temp)
		    {
		      log_sysop_start (thread_p);
		    }

		  if (file_dealloc (thread_p, &dir_header_p->bucket_file, &bucket_vpid, FILE_EXTENDIBLE_HASH)
		      != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		      if (!is_temp)
			{
			  log_sysop_abort (thread_p);
			}
		      return;
		    }

		  /* Set all pointers to the bucket to NULL */
		  if (ehash_connect_bucket (thread_p, ehid_p, old_local_depth, hash_key, &null_vpid, is_temp) !=
		      NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		      if (!is_temp)
			{
			  log_sysop_abort (thread_p);
			}
		      return;
		    }

		  ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, old_local_depth, -1,
					    is_temp);
		  ehash_shrink_directory_if_need (thread_p, ehid_p, dir_header_p, is_temp);
		  if (!is_temp)
		    {
		      log_sysop_commit (thread_p);
		    }
		}
	      break;

	    case EHASH_FULL_SIBLING_BUCKET:
	      /* If the sib_bucket does not have room for the remaining records of buc_page, the record has already
	       * been deleted, so just release bucket page */
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      break;

	    case EHASH_SUCCESSFUL_COMPLETION:
	      /* Perform actual merge operation */
	      if (!is_temp)
		{
		  log_sysop_start (thread_p);
		}

	      if (ehash_merge_permanent (thread_p, ehid_p, dir_root_page_p, dir_header_p, bucket_page_p, sibling_page_p,
					 &bucket_vpid, &sibling_vpid, num_records, sibling_location, first_slot_id,
					 &new_local_depth, is_temp) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, sibling_page_p);
		  pgbuf_unfix_and_init (thread_p, bucket_page_p);
		  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		  if (!is_temp)
		    {
		      log_sysop_abort (thread_p);
		    }
		  return;
		}

	      pgbuf_unfix_and_init (thread_p, sibling_page_p);
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);

	      if (file_dealloc (thread_p, &dir_header_p->bucket_file, &bucket_vpid, FILE_EXTENDIBLE_HASH) != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		  if (!is_temp)
		    {
		      log_sysop_abort (thread_p);
		    }
		  return;
		}

	      ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, old_local_depth, -2, is_temp);
	      ehash_adjust_local_depth (thread_p, ehid_p, dir_root_page_p, dir_header_p, new_local_depth, 1, is_temp);
	      ehash_shrink_directory_if_need (thread_p, ehid_p, dir_header_p, is_temp);

	      /* Update some directory pointers */
	      if (ehash_connect_bucket (thread_p, ehid_p, new_local_depth, hash_key, &sibling_vpid, is_temp)
		  != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
		  if (!is_temp)
		    {
		      log_sysop_abort (thread_p);
		    }
		  return;
		}

	      if (!is_temp)
		{
		  log_sysop_commit (thread_p);
		}
	      break;

	    case EHASH_ERROR_OCCURRED:
	      /* There happened an error in the merge operation; so return */

	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return;

	    default:
	      /* An undefined merge result was returned; This should never happen */
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	      return;
	    }
	}
    }

  /* Release directory and return */
  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
}

/*
 * ehash_shrink_directory () - Shrink directory
 *   return:
 *   ehid(in): identifier for the extendible hashing structure to shrink
 *   new_depth(in): requested new depth of directory
 *   is_temp(in): true if temporary file (logging is not required)
 *
 * Note: This function shrinks the directory of specified extendible
 * hashing structure as many times as necessary to attain
 * the requested depth. During this operation, the directory is
 * locked with X_LOCK to prevent others from accessing wrong
 * information. If the original directory is large enough, some
 * pages are deallocated from the new directory.
 * The remaining portion of the directory is logged and updated
 * before the extra part is deallocated.
 */
static void
ehash_shrink_directory (THREAD_ENTRY * thread_p, EHID * ehid_p, int new_depth, bool is_temp)
{
  int old_pages;
  int old_ptrs;
  int new_pages;
  int new_ptrs;

  EHASH_DIR_HEADER *dir_header_p;
  int end_offset;
  int new_end_offset;
  int check_pages;
  int i;
  int times;

  PAGE_PTR old_dir_page_p;
  int old_dir_offset;
  int old_dir_nth_page;
  int prev_pg_no;

  PAGE_PTR new_dir_page_p;
  PGLENGTH new_dir_offset;
  int new_dir_nth_page;
  int ret = NO_ERROR;

  dir_header_p = (EHASH_DIR_HEADER *) ehash_fix_nth_page (thread_p, &ehid_p->vfid, 0, PGBUF_LATCH_WRITE);
  if (dir_header_p == NULL)
    {
      return;
    }

  if (dir_header_p->depth < new_depth)
    {
#ifdef EHASH_DEBUG
      er_log_debug (ARG_FILE_LINE, "WARNING in eh_shrink_dir:The directory has a depth of %d , shrink is cancelled ",
		    dir_header_p->depth);
#endif
      pgbuf_unfix (thread_p, (PAGE_PTR) dir_header_p);
      return;
    }

  old_ptrs = 1 << dir_header_p->depth;
  times = 1 << (dir_header_p->depth - new_depth);
  ret = file_get_num_user_pages (thread_p, &ehid_p->vfid, &old_pages);
  if (ret != NO_ERROR)
    {
#ifdef EHASH_DEBUG
      er_log_debug (ARG_FILE_LINE, "WARNING: error reading user page number from file, shrink is cancelled.\n");
#endif
      ASSERT_ERROR ();
      pgbuf_unfix (thread_p, (PAGE_PTR) dir_header_p);
      return;
    }
  old_pages -= 1;		/* The first page starts with 0 */

  new_ptrs = old_ptrs / times;	/* Calculate how many pointers will remain */

  end_offset = old_ptrs - 1;	/* Directory first pointer has an offset of 0 */
  ehash_dir_locate (&check_pages, &end_offset);
#ifdef EHASH_DEBUG
  if (check_pages != old_pages)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, ehid_p->vfid.volid, ehid_p->vfid.fileid,
	      ehid_p->pageid);
      return;
    }
#endif

  /* Perform shrink */

  prev_pg_no = 0;
  old_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, prev_pg_no, PGBUF_LATCH_WRITE);
  if (old_dir_page_p == NULL)
    {
      return;
    }

  new_dir_nth_page = 0;
  new_dir_page_p = (PAGE_PTR) dir_header_p;
  new_dir_offset = EHASH_DIR_HEADER_SIZE;

  if (!is_temp)
    {
      log_append_undo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE, new_dir_page_p);
    }

  dir_header_p->depth = new_depth;	/* Decrease the directory global depth */

  /* Copy old directory records to new (shrunk) area */
  for (i = 1; i < new_ptrs; i++)
    {
      /* Location 0 will not change */

      /* Locate the next source pointer */
      old_dir_offset = i * times;
      ehash_dir_locate (&old_dir_nth_page, &old_dir_offset);

      if (old_dir_nth_page != prev_pg_no)
	{
	  /* We reached the end of the source page. The next bucket pointer is in the next Dir page */

	  prev_pg_no = old_dir_nth_page;
	  pgbuf_unfix_and_init (thread_p, old_dir_page_p);

	  /* set olddir_pgptr to the new pgptr */
	  old_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, old_dir_nth_page, PGBUF_LATCH_WRITE);
	  if (old_dir_page_p == NULL)
	    {
	      pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);
	      return;
	    }
	}

      /* Advance the destination pointer to new spot */
      new_dir_offset += sizeof (EHASH_DIR_RECORD);

      if ((DB_PAGESIZE - new_dir_offset) < SSIZEOF (EHASH_DIR_RECORD))
	{
	  /* There is no more place in the directory page for new bucket pointers */

	  if (!is_temp)
	    {
	      /* Log this updated directory page */
	      log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE,
				     new_dir_page_p);
	    }

	  pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

	  /* get another page */
	  new_dir_nth_page++;

	  /* set newdir_pgptr to the new pgptr */
	  new_dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, new_dir_nth_page, PGBUF_LATCH_WRITE);
	  if (new_dir_page_p == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, old_dir_page_p);
	      return;
	    }
	  new_dir_offset = 0;

	  if (!is_temp)
	    {
	      /* Log the initial content of this directory page */
	      log_append_undo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE,
				     new_dir_page_p);
	    }
	}

      /* Perform the copy operation */
      memcpy ((char *) new_dir_page_p + new_dir_offset, (char *) old_dir_page_p + old_dir_offset,
	      sizeof (EHASH_DIR_RECORD));
    }

  if (!is_temp)
    {
      /* Log this updated directory page */
      log_append_redo_data2 (thread_p, RVEH_REPLACE, &ehid_p->vfid, new_dir_page_p, 0, DB_PAGESIZE, new_dir_page_p);
    }

  /* Release the source and destination pages */
  pgbuf_unfix_and_init (thread_p, old_dir_page_p);
  pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

  /* remove unwanted part of directory. */
  new_end_offset = new_ptrs - 1;
  ehash_dir_locate (&new_pages, &new_end_offset);
  ret = file_numerable_truncate (thread_p, &ehid_p->vfid, new_pages + 1);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
}

static EHASH_HASH_KEY
ehash_hash_string_type (char *key_p, char *original_key_p)
{
  EHASH_HASH_KEY hash_key = 0;
  char copy_psekey[12];
  int i;
  int length;
  int times;
  int remaining;
  int Int;
  char Char;
  int byte;
  char *p = NULL;
  char *new_key_p = NULL;

  length = (int) strlen (key_p);

  if (length > 0)
    {
      /* Eliminate any trailing space characters */
      if (char_isspace (*(char *) (key_p + length - 1)))
	{
	  for (p = key_p + length - 1; char_isspace (*p) && (p > key_p); p--)
	    {
	      ;
	    }
	  length = (int) (p - key_p + 1);
	  key_p = new_key_p = (char *) malloc (length + 1);	/* + 1 is for \0 */
	  if (key_p == NULL)
	    {
	      return 0;
	    }
	  memcpy (key_p, original_key_p, length);
	  *(char *) (key_p + length) = '\0';
	}

      /* Takes the floor of division */
      times = length / sizeof (EHASH_HASH_KEY);

      /* Apply Folding method */
      for (i = 1; i <= times; i++)
	{
	  memcpy (&Int, key_p, sizeof (int));
	  hash_key += Int;	/* Add next word of original key */

	  key_p += sizeof (EHASH_HASH_KEY);
	}

      remaining = length - times * sizeof (EHASH_HASH_KEY);

      /* Go over the remaining chars of key */
      for (i = 1; i <= remaining; i++)
	{
	  /* shift left to align with previous content */
	  Int = (int) *key_p++ << i;
	  hash_key += Int;
	}

      if (new_key_p)
	{
	  free_and_init (new_key_p);
	}
    }

  /* Copy the hash_key to an aux. area, to be further hashed into individual bytes */
  memcpy (&copy_psekey, &hash_key, sizeof (EHASH_HASH_KEY));
  copy_psekey[sizeof (EHASH_HASH_KEY)] = '\0';

  hash_key = 0;
  byte = mht_1strhash (copy_psekey, 509);
  hash_key = hash_key + (byte << (8 * (sizeof (EHASH_HASH_KEY) - 1)));
  byte = mht_2strhash (copy_psekey, 509);
  hash_key = hash_key + (byte << (8 * (sizeof (EHASH_HASH_KEY) - 2)));
  byte = mht_3strhash (copy_psekey, 509);
  hash_key = hash_key + (byte << (8 * (sizeof (EHASH_HASH_KEY) - 3)));

  /* Go over the chars of the given pseudo key */
  Char = '\0';
  key_p = (char *) &copy_psekey;
  for (i = 0; (unsigned int) i < sizeof (EHASH_HASH_KEY); i++)
    {
      Char += (char) *key_p++;
    }

  /* Change the first byte of the pseudo key to the SUM of all of them */
  return hash_key + Char;
}

static EHASH_HASH_KEY
ehash_hash_eight_bytes_type (char *key_p)
{
  EHASH_HASH_KEY hash_key = 0;
  unsigned int i;
  int Int;
  char Char;

  for (i = 0; i < sizeof (double) / sizeof (int); i++)
    {
      memcpy (&Int, key_p, sizeof (int));
      hash_key += htonl (Int);	/* Add next word of original key */
      key_p += sizeof (int);
    }

  /* Go over the chars of the given pseudo key */
  Char = '\0';
  key_p = (char *) &hash_key;
  for (i = 0; i < sizeof (EHASH_HASH_KEY); i++)
    {
      Char ^= (char) *key_p++;
    }

  /* Change the first byte of the pseudo key to the summation of all of them */
  memcpy (&hash_key, &Char, sizeof (char));

  return hash_key;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static EHASH_HASH_KEY
ehash_hash_four_bytes_type (char *key_p)
{
  EHASH_HASH_KEY hash_key = 0;
  unsigned int i;
  unsigned short Short, *Short2;
  char Char;

  Short = ntohs (*(unsigned short *) key_p);	/* Get the left half of the int */
  Short2 = (unsigned short *) key_p;
  Short2++;

  Short += ntohs (*Short2);
  hash_key = (EHASH_HASH_KEY) (Short << EHASH_SHORT_BITS);
  hash_key += (EHASH_HASH_KEY) Short;

  /* Go over the chars of the given pseudo key */
  Char = '\0';
  key_p = (char *) &hash_key;
  for (i = 0; i < sizeof (EHASH_HASH_KEY); i++)
    {
      Char += (char) *key_p++;
    }

  /* Change the first byte of the pseudo key to the SUM of all of them */
  memcpy (&hash_key, &Char, sizeof (char));

  return hash_key;
}

static EHASH_HASH_KEY
ehash_hash_two_bytes_type (char *key_p)
{
  EHASH_HASH_KEY hash_key = 0;
  unsigned int i;
  unsigned short Short;
  char Char;

  Short = ntohs (*(unsigned short *) key_p);
  hash_key = (EHASH_HASH_KEY) (Short << EHASH_SHORT_BITS);
  hash_key += (EHASH_HASH_KEY) Short;

  /* Go over the chars of the given pseudo key */
  Char = '\0';
  key_p = (char *) &hash_key;
  for (i = 0; i < sizeof (EHASH_HASH_KEY); i++)
    {
      Char += (char) *key_p++;
    }

  /* Change the first byte of the pseudo key to the summation of all of them */
  memcpy (&hash_key, &Char, sizeof (char));

  return hash_key;
}
#endif

/*
 * ehash_hash () - Hash function
 *   return: EHASH_HASH_KEY
 *   orig_key(in): original key to encode into a pseudo key
 *   key_type(in): type of the key
 *
 * Note: This function converts the given original key into a pseudo
 * key. Since the original key is presented as a character
 * string, its conversion into a int-compatible type is essential
 * prior to performing any operation on it.
 * Depending on the "key_type", the original key might be
 * folded (for DB_TYPE_STRING, DB_TYPE_OBJECT and DB_TYPE_DOUBLE) or duplicated
 * (for DB_TYPE_SHORT) into a computer word.
 * This function does not change the value of parameter
 * orig_key, as it might be on a bucket.
 */
static EHASH_HASH_KEY
ehash_hash (void *original_key_p, DB_TYPE key_type)
{
  char *key = (char *) original_key_p;
  EHASH_HASH_KEY hash_key = 0;

  switch (key_type)
    {
    case DB_TYPE_STRING:
      hash_key = ehash_hash_string_type (key, (char *) original_key_p);
      break;

    case DB_TYPE_OBJECT:
      hash_key = ehash_hash_eight_bytes_type (key);
      break;

#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_BIGINT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
      if (key_type == DB_TYPE_MONETARY)
	{
	  key = (char *) &(((DB_MONETARY *) original_key_p)->amount);
	}

      hash_key = ehash_hash_eight_bytes_type (key);
      break;

    case DB_TYPE_FLOAT:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATETIME:
    case DB_TYPE_INTEGER:
      hash_key = ehash_hash_four_bytes_type (key);
      break;

    case DB_TYPE_SHORT:
      hash_key = ehash_hash_two_bytes_type (key);
      break;
#endif
    default:
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
    }

  return hash_key;
}

static int
ehash_apply_each (THREAD_ENTRY * thread_p, EHID * ehid_p, RECDES * recdes_p, DB_TYPE key_type, char *bucket_record_p,
		  OID * assoc_value_p, int *out_apply_error, int (*apply_function) (THREAD_ENTRY * thread_p, void *key,
										    void *data, void *args), void *args)
{
#if defined (ENABLE_UNUSED_FUNCTION)
  char *long_str;
#endif
  char *str_next_key_p, *temp_p;
  int key_size;
  char next_key[sizeof (DB_MONETARY)];
  int i;
  void *key_p = &next_key;

  switch (key_type)
    {
    case DB_TYPE_STRING:
#if defined (ENABLE_UNUSED_FUNCTION)
      if (recdes_p->type == REC_BIGONE)
	{
	  /* This is a long string */
	  long_str = ehash_compose_overflow (thread_p, recdes_p);
	  if (long_str == NULL)
	    {
	      *out_apply_error = ER_FAILED;
	      return NO_ERROR;
	    }
	  key_p = long_str;
	}
      else
#endif
	{
	  /* Short String */
	  key_size = recdes_p->length - CAST_BUFLEN (bucket_record_p - recdes_p->data);

	  str_next_key_p = (char *) malloc (key_size);
	  if (str_next_key_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) key_size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  temp_p = str_next_key_p;
	  for (i = 0; i < key_size; i++)
	    {
	      *temp_p++ = *bucket_record_p++;
	    }

	  key_p = str_next_key_p;
	}

      break;

    case DB_TYPE_OBJECT:
      memcpy (&next_key, bucket_record_p, sizeof (OID));
      break;
#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_DOUBLE:
      OR_MOVE_DOUBLE (bucket_record_p, &next_key);
      break;

    case DB_TYPE_FLOAT:
      *((float *) &next_key) = *(float *) bucket_record_p;
      break;

    case DB_TYPE_INTEGER:
      *((int *) &next_key) = *(int *) bucket_record_p;
      break;

    case DB_TYPE_BIGINT:
      *((DB_BIGINT *) (&next_key)) = *(DB_BIGINT *) bucket_record_p;
      break;

    case DB_TYPE_SHORT:
      *((short *) &next_key) = *(short *) bucket_record_p;
      break;

    case DB_TYPE_DATE:
      *((DB_DATE *) (&next_key)) = *(DB_DATE *) bucket_record_p;
      break;

    case DB_TYPE_TIME:
      *((DB_TIME *) (&next_key)) = *(DB_TIME *) bucket_record_p;
      break;

    case DB_TYPE_TIMESTAMP:
      *((DB_UTIME *) (&next_key)) = *(DB_UTIME *) bucket_record_p;
      break;

    case DB_TYPE_DATETIME:
      *((DB_DATETIME *) (&next_key)) = *(DB_DATETIME *) bucket_record_p;
      break;

    case DB_TYPE_MONETARY:
      OR_MOVE_DOUBLE (&((DB_MONETARY *) bucket_record_p)->amount, &((DB_MONETARY *) (&next_key))->amount);
      ((DB_MONETARY *) (&next_key))->type = ((DB_MONETARY *) bucket_record_p)->type;
      break;
#endif
    default:
      /* Unspecified key type: Directory header has been corrupted */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, ehid_p->vfid.volid, ehid_p->vfid.fileid,
	      ehid_p->pageid);
      return ER_EH_ROOT_CORRUPTED;
    }

  *out_apply_error = (*apply_function) (thread_p, key_p, assoc_value_p, args);

  if (key_type == DB_TYPE_STRING)
    {
      free_and_init (key_p);
    }

  return NO_ERROR;
}

/*
 * ehash_map () - Apply function to all entries
 *   return: int NO_ERROR, or ER_FAILED
 *   ehid(in): identifier of the extendible hashing structure
 *   fun(in): function to call on key, data, and arguments
 *   args(in):
 *
 */
int
ehash_map (THREAD_ENTRY * thread_p, EHID * ehid_p,
	   int (*apply_function) (THREAD_ENTRY * thread_p, void *key, void *data, void *args), void *args)
{
  EHASH_DIR_HEADER *dir_header_p;
  int num_pages, bucket_page_no, num_records;
  PAGE_PTR dir_page_p, bucket_page_p = NULL;
  char *bucket_record_p;
  RECDES recdes;
  PGSLOTID slot_id, first_slot_id = NULL_SLOTID;
  OID assoc_value;
  int apply_error_code = NO_ERROR;

  if (ehid_p == NULL)
    {
      return ER_FAILED;
    }

  dir_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_READ);
  if (dir_page_p == NULL)
    {
      return ER_FAILED;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_page_p;
  if (file_get_num_user_pages (thread_p, &dir_header_p->bucket_file, &num_pages) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Retrieve each bucket page and apply the given function to its entries */
  for (bucket_page_no = 0; apply_error_code == NO_ERROR && bucket_page_no < num_pages; bucket_page_no++)
    {
      first_slot_id = NULL_SLOTID;
      bucket_page_p = ehash_fix_nth_page (thread_p, &dir_header_p->bucket_file, bucket_page_no, PGBUF_LATCH_READ);
      if (bucket_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_page_p);
	  return ER_FAILED;
	}

      num_records = spage_number_of_records (bucket_page_p);

      (void) spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK);

      /* Skip the first slot since it contains the bucket header */
      for (slot_id = first_slot_id + 1; apply_error_code == NO_ERROR && slot_id < num_records; slot_id++)
	{
	  (void) spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK);
	  bucket_record_p = (char *) recdes.data;
	  bucket_record_p = ehash_read_oid_from_record (bucket_record_p, &assoc_value);

	  if (ehash_apply_each (thread_p, ehid_p, &recdes, dir_header_p->key_type, bucket_record_p, &assoc_value,
				&apply_error_code, apply_function, args) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, bucket_page_p);
	      pgbuf_unfix_and_init (thread_p, dir_page_p);
	      return ER_FAILED;
	    }
	}
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }

  pgbuf_unfix_and_init (thread_p, dir_page_p);

  return apply_error_code;
}

/*
 * Debugging functions
 */

/*
 * ehash_dump () - Dump directory & all buckets
 *   return:
 *   ehid(in): identifier for the extendible hashing structure to dump
 *
 * Note: A debugging function. Dumps the contents of the directory
 * and the buckets of specified ext. hashing structure.
 */
void
ehash_dump (THREAD_ENTRY * thread_p, EHID * ehid_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  EHASH_DIR_RECORD *dir_record_p;
  int num_pages;
  int num_ptrs;

  int check_pages;
  int end_offset;
  int i;

  PAGE_PTR dir_page_p, dir_root_page_p;
  PGLENGTH dir_offset;
  int dir_page_no;
  int dir_ptr_no;

  PAGE_PTR bucket_page_p;
  int bucket_page_no;

  if (ehid_p == NULL)
    {
      return;
    }

  dir_root_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_READ);
  if (dir_root_page_p == NULL)
    {
      return;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (file_get_num_user_pages (thread_p, &ehid_p->vfid, &num_pages) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return;
    }
  num_pages -= 1;		/* The first page starts with 0 */

  num_ptrs = 1 << dir_header_p->depth;
  end_offset = num_ptrs - 1;	/* Directory first pointer has an offset of 0 */
  ehash_dir_locate (&check_pages, &end_offset);

#ifdef EHASH_DEBUG
  if (check_pages != num_pages)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, ehid_p->vfid.volid, ehid_p->vfid.fileid,
	      ehid_p->pageid);
      return;
    }
#endif


  printf ("*********************************************************\n");
  printf ("*                      DIRECTORY                        *\n");
  printf ("*                                                       *\n");
  printf ("*    Depth    :  %d                                      *\n", dir_header_p->depth);
  printf ("*    Key type : ");

  switch (dir_header_p->key_type)
    {
    case DB_TYPE_STRING:
      printf (" string                                 *\n");
      break;

    case DB_TYPE_OBJECT:
      printf (" OID                                 *\n");
      break;
#if defined (ENABLE_UNUSED_FUNCTION)
    case DB_TYPE_DOUBLE:
      printf (" double                                 *\n");
      break;

    case DB_TYPE_FLOAT:
      printf (" float                                 *\n");
      break;

    case DB_TYPE_INTEGER:
      printf (" int                                    *\n");
      break;

    case DB_TYPE_BIGINT:
      printf (" BIGINT                                 *\n");
      break;

    case DB_TYPE_SHORT:
      printf (" short                                  *\n");
      break;

    case DB_TYPE_DATE:
      printf (" date                                   *\n");
      break;

    case DB_TYPE_TIME:
      printf (" time                                   *\n");
      break;

    case DB_TYPE_TIMESTAMP:
      printf (" utime                                  *\n");
      break;

    case DB_TYPE_DATETIME:
      printf (" datetime                               *\n");
      break;

    case DB_TYPE_MONETARY:
      printf (" monetary                               *\n");
      break;
#endif
    default:
      /* Unspecified key type: Directory header has been corrupted */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, ehid_p->vfid.volid, ehid_p->vfid.fileid,
	      ehid_p->pageid);
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      return;
    }

  printf ("*    Key size :  %d                                      *\n", ehash_get_key_size (dir_header_p->key_type));
  printf ("*                                                       *\n");
  printf ("*               LOCAL DEPTH COUNTERS                    *\n");
  for (i = 0; (unsigned int) i <= EHASH_HASH_KEY_BITS; i++)
    {
      if (dir_header_p->local_depth_count[i] != 0)
	{
	  fprintf (stdout, "*    There are %d ", dir_header_p->local_depth_count[i]);
	  fprintf (stdout, " buckets with local depth %d             *\n", i);
	}
    }

  printf ("*                                                       *\n");
  printf ("*                      POINTERS                         *\n");
  printf ("*                                                       *\n");

  /* Print directory */

  dir_offset = EHASH_DIR_HEADER_SIZE;
  dir_page_no = 0;
  dir_ptr_no = 0;

  dir_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_READ);
  if (dir_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      return;
    }

  for (i = 0; i < num_ptrs; i++)
    {
      if (DB_PAGESIZE - dir_offset < SSIZEOF (EHASH_DIR_RECORD))
	{
	  /* We reached the end of the directory page. The next bucket pointer is in the next directory page. */

	  /* Release previous page, and unlock it */
	  pgbuf_unfix_and_init (thread_p, dir_page_p);

	  dir_page_no++;

	  /* Get another page */
	  dir_page_p = ehash_fix_nth_page (thread_p, &ehid_p->vfid, dir_page_no, PGBUF_LATCH_READ);
	  if (dir_page_p == NULL)
	    {
	      return;
	    }

	  dir_offset = 0;
	}

      /* Print out the next directory record */
      dir_record_p = (EHASH_DIR_RECORD *) ((char *) dir_page_p + dir_offset);

      if (VPID_ISNULL (&dir_record_p->bucket_vpid))
	{
	  printf ("*    Dir loc :  %d   points to bucket page  id: NULL    *\n", dir_ptr_no);
	}
      else
	{
	  printf ("*    Dir loc :  %d   points to vol:%d bucket pgid: %d  *\n", dir_ptr_no,
		  dir_record_p->bucket_vpid.volid, dir_record_p->bucket_vpid.pageid);
	}

      dir_ptr_no++;
      dir_offset += sizeof (EHASH_DIR_RECORD);
    }

  /* Release last page */
  pgbuf_unfix_and_init (thread_p, dir_page_p);

  printf ("*                                                       *\n");
  printf ("*********************************************************\n");

  /* Print buckets */

  if (file_get_num_user_pages (thread_p, &dir_header_p->bucket_file, &num_pages) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return;
    }
  num_pages -= 1;

  for (bucket_page_no = 0; bucket_page_no <= num_pages; bucket_page_no++)
    {
      bucket_page_p = ehash_fix_nth_page (thread_p, &dir_header_p->bucket_file, bucket_page_no, PGBUF_LATCH_READ);
      if (bucket_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  return;
	}

      printf ("\n\n");
      ehash_dump_bucket (thread_p, bucket_page_p, dir_header_p->key_type);
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }

  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
  return;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * ehash_print_bucket () - Retrieve the bucket and print its contents
 *   return:
 *   ehid(in): identifier for the extendible hashing structure
 *   nth_ptr(in): which pointer of the directory points to the bucket
 *
 * Note: A debugging function. Prints out the contents of the bucket
 * pointed by the "nth_ptr" pointer.
 */
void
ehash_print_bucket (THREAD_ENTRY * thread_p, EHID * ehid_p, int nth_ptr)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  VPID bucket_vpid;
  PAGE_PTR bucket_page_p;

  dir_root_page_p = ehash_fix_ehid_page (thread_p, ehid_p, PGBUF_LATCH_READ);
  if (dir_root_page_p == NULL)
    {
      return;
    }

  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (ehash_find_bucket_vpid (thread_p, ehid_p, dir_header_p, nth_ptr, PGBUF_LATCH_WRITE, &bucket_vpid) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      return;
    }

  bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, &bucket_vpid, PGBUF_LATCH_READ);
  if (bucket_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      return;
    }
  ehash_dump_bucket (thread_p, bucket_page_p, dir_header_p->key_type);

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  pgbuf_unfix_and_init (thread_p, dir_root_page_p);

  return;
}
#endif

/*
 * ehash_dump_bucket () - Print the bucket's contents
 *   return:
 *   buc_pgptr(in): bucket page whose contents is going to be dumped
 *   key_type(in): type of the key
 *
 * Note: A debugging function. Prints out the contents of the given bucket.
 */
static void
ehash_dump_bucket (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, DB_TYPE key_type)
{
  EHASH_BUCKET_HEADER *bucket_header_p;
  char *bucket_record_p;
  RECDES recdes;
  PGSLOTID slot_id, first_slot_id = -1;
  int key_size;
  OID assoc_value;
  int num_records;
  int i;
#if defined (ENABLE_UNUSED_FUNCTION)
  VPID *ovf_vpid_p;
  int hour, minute, second, millisecond, month, day, year;
  double d;
#endif

  (void) spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK);
  bucket_header_p = (EHASH_BUCKET_HEADER *) recdes.data;

  printf ("************************************************************\n");
  printf ("*  local_depth : %d                                         *\n", bucket_header_p->local_depth);
  printf ("*  no. records : %d                                         *\n",
	  spage_number_of_records (bucket_page_p) - 1);
  printf ("*                                                          *\n");
  printf ("*   No          Key                   Assoc Value          *\n");
  printf ("*  ====   =====================    ==================      *\n");

  num_records = spage_number_of_records (bucket_page_p);

  for (slot_id = 1; slot_id < num_records; slot_id++)
    {
      printf ("*   %2d", slot_id);

      spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK);
      bucket_record_p = (char *) recdes.data;
      bucket_record_p = ehash_read_oid_from_record (bucket_record_p, &assoc_value);

      switch (key_type)
	{

	case DB_TYPE_STRING:
#if defined (ENABLE_UNUSED_FUNCTION)
	  if (recdes.type == REC_BIGONE)
	    {
	      ovf_vpid_p = (VPID *) bucket_record_p;
	      bucket_record_p += sizeof (VPID);

	      printf ("    ");
	      for (i = 0; i < EHASH_LONG_STRING_PREFIX_SIZE; i++)
		{
		  putchar (*(bucket_record_p++));
		}
	      printf ("  ovf: %4d | %1d  ", ovf_vpid_p->pageid, ovf_vpid_p->volid);
	    }
	  else
#endif
	    {
	      key_size = recdes.length - CAST_BUFLEN (bucket_record_p - recdes.data);
	      printf ("    %s", bucket_record_p);
	      for (i = 0; i < (29 - key_size); i++)
		{
		  printf (" ");
		}
	    }
	  break;

	case DB_TYPE_OBJECT:
	  printf ("   (%5d,%5d,%5d)          ", ((OID *) bucket_record_p)->volid, ((OID *) bucket_record_p)->pageid,
		  ((OID *) bucket_record_p)->slotid);
	  break;
#if defined (ENABLE_UNUSED_FUNCTION)
	case DB_TYPE_DOUBLE:
	  OR_MOVE_DOUBLE (bucket_record_p, &d);
	  printf ("    %20f      ", d);
	  break;

	case DB_TYPE_FLOAT:
	  printf ("    %20f      ", *(float *) bucket_record_p);
	  break;

	case DB_TYPE_INTEGER:
	  printf ("    %14d              ", *(int *) bucket_record_p);
	  break;

	case DB_TYPE_BIGINT:
	  printf ("    %19lld             ", (long long) (*(DB_BIGINT *) bucket_record_p));
	  break;

	case DB_TYPE_SHORT:
	  printf ("    %14d              ", *(short *) bucket_record_p);
	  break;

	case DB_TYPE_DATE:
	  db_date_decode ((DB_DATE *) bucket_record_p, &month, &day, &year);
	  fprintf (stdout, "    %2d/%2d/%4d              ", month, day, year);
	  break;

	case DB_TYPE_TIME:
	  db_time_decode ((DB_TIME *) bucket_record_p, &hour, &minute, &second);
	  fprintf (stdout, "      %2d:%2d:%2d               ", hour, minute, second);
	  break;

	case DB_TYPE_TIMESTAMP:
	  {
	    DB_DATE tmp_date;
	    DB_TIME tmp_time;

	    db_timestamp_decode_ses ((DB_UTIME *) bucket_record_p, &tmp_date, &tmp_time);
	    db_date_decode (&tmp_date, &month, &day, &year);
	    db_time_decode (&tmp_time, &hour, &minute, &second);
	    printf ("    %2d:%2d:%2d %2d/%2d/%4d             ", hour, minute, second, month, day, year);
	  }
	  break;

	case DB_TYPE_DATETIME:
	  db_datetime_decode ((DB_DATETIME *) bucket_record_p, &month, &day, &year, &hour, &minute, &second,
			      &millisecond);
	  printf ("    %2d:%2d:%2d.%03d %2d/%2d/%4d             ", hour, minute, second, millisecond, month, day, year);
	  break;

	case DB_TYPE_MONETARY:
	  OR_MOVE_DOUBLE (bucket_record_p, &d);
	  printf ("    %14f type %d       ", d, ((DB_MONETARY *) bucket_record_p)->type);
	  break;
#endif
	default:
	  /* Unspecified key type: Directory header has been corrupted */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_INVALID_KEY_TYPE, 1, key_type);
	  return;
	}
      printf ("(%5d,%5d,%5d)  *\n", assoc_value.volid, assoc_value.pageid, assoc_value.slotid);

    }

  printf ("*********************************************************\n");
}

/*
 * Recovery functions
 */

/*
 * ehash_rv_init_bucket_redo () - Redo the initilization of a bucket page
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Redo the initilization of a bucket page. The data area of the
 * recovery structure contains the alignment value and the
 * local depth of the bucket page.
 */
int
ehash_rv_init_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  char alignment;
  EHASH_BUCKET_HEADER bucket_header;
  RECDES bucket_recdes;
  const char *record_p;
  PGSLOTID slot_id;
  int success;

  record_p = recv_p->data;

  alignment = *(char *) record_p;
  record_p += sizeof (char);
  bucket_header.local_depth = *(char *) record_p;

  pgbuf_set_page_ptype (thread_p, recv_p->pgptr, PAGE_EHASH);

  /*
   * Initilize the bucket to contain variable-length records
   * on ordered slots.
   */
  spage_initialize (thread_p, recv_p->pgptr, UNANCHORED_KEEP_SEQUENCE, alignment, DONT_SAFEGUARD_RVSPACE);

  /* Set the record descriptor to the Bucket header */
  bucket_recdes.data = (char *) &bucket_header;
  bucket_recdes.area_size = bucket_recdes.length = sizeof (EHASH_BUCKET_HEADER);
  bucket_recdes.type = REC_HOME;

  success = spage_insert (thread_p, recv_p->pgptr, &bucket_recdes, &slot_id);
  if (success != SP_SUCCESS)
    {
      /* Slotted page module refuses to insert a short size record to an empty page. This should never happen. */
      if (success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * ehash_rv_init_dir_redo () - Redo the initilization of a directory
 *   return: int
 *   recv(in): Recovery structure
 */
int
ehash_rv_init_dir_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  pgbuf_set_page_ptype (thread_p, recv_p->pgptr, PAGE_EHASH);

  return log_rv_copy_char (thread_p, recv_p);
}

/*
 * ehash_rv_insert_redo () - Redo the insertion of an entry to a bucket page
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Redo the insertion of a (key, assoc-value) entry to a specific
 * extendible hashing structure. The data area of the recovery
 * structure contains the key type, and the entry to be inserted.
 */
int
ehash_rv_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;
  RECDES recdes;
  int success;

  slot_id = recv_p->offset;
  recdes.type = *(short *) (recv_p->data);
  recdes.data = (char *) (recv_p->data) + sizeof (short);
  recdes.area_size = recdes.length = recv_p->length - sizeof (recdes.type);

  success = spage_insert_for_recovery (thread_p, recv_p->pgptr, slot_id, &recdes);
  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);

  if (success != SP_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return er_errid ();
    }

  return NO_ERROR;
}


/*
 * ehash_rv_insert_undo () - Undo the insertion of an entry to ext. hash
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo the insertion of a (key, assoc-value) entry to a
 * specific extendible hashing structure. The data area of the
 * recovery structure contains the ext. hashing identifier, and
 * the entry to be deleted. Note that this function uses the
 * recovery delete function (not the original one) in order to
 * avoid possible merge operations.
 */
int
ehash_rv_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  EHID ehid;
  char *record_p = (char *) recv_p->data;
#if defined (ENABLE_UNUSED_FUNCTION)
  short record_type = recv_p->offset;
#endif

  record_p = ehash_read_ehid_from_record (record_p, &ehid);
  record_p += sizeof (OID);

#if defined (ENABLE_UNUSED_FUNCTION)
  if (record_type == REC_BIGONE)
    {
      /* This record is for a long string. At the monent it seems more logical to compose the whole key here rather
       * than at the logging time. But, if the overflow pages cannot be guaranteed to exist when this function is
       * called, then the whole key would need to be logged and this portion of the code becomes unnecessary. */
      RECDES recdes;
      char *long_str;
      int error;

      /* Compose the whole key */
      recdes.data = (char *) recv_p->data + sizeof (EHID);
      recdes.length = recdes.area_size = recv_p->length - sizeof (EHID);
      long_str = ehash_compose_overflow (thread_p, &recdes);

      if (long_str == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  return error;
	}

      error = ehash_rv_delete (thread_p, &ehid, (void *) long_str);
      free_and_init (long_str);
      return error;
    }
  else
#endif
    {
      return ehash_rv_delete (thread_p, &ehid, (void *) record_p);
    }
}

/*
 * ehash_rv_delete_redo () - Redo the deletion of an entry to ext. hash
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Redo the deletion of a (key, assoc-value) entry from a
 * specific extendible hashing structure. The data area of the
 * recovery structure contains the key type followed by the
 * entry to be deleted.
 */
int
ehash_rv_delete_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;
  RECDES recdes;
  RECDES existing_recdes;

  slot_id = recv_p->offset;
  recdes.type = *(short *) (recv_p->data);
  recdes.data = (char *) (recv_p->data) + sizeof (short);
  recdes.area_size = recdes.length = recv_p->length - sizeof (recdes.type);

  if (spage_get_record (thread_p, recv_p->pgptr, slot_id, &existing_recdes, PEEK) == S_SUCCESS)
    /*
     * There is a record in the specified slot. Check if it is the same
     * record
     */
    if ((existing_recdes.type == recdes.type) && (existing_recdes.length == recdes.length)
	&& (memcmp (existing_recdes.data, recdes.data, recdes.length) == 0))
      {
	/* The record exist in the correct slot in the page. So, delete this slot from the page */
	(void) spage_delete (thread_p, recv_p->pgptr, slot_id);
	pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);

      }

  return NO_ERROR;
}

/*
 * ehash_rv_delete_undo () - Undo the deletion of an entry from ext. hash
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo the deletion of a (key, assoc-value) entry from a
 * specific extendible hashing structure. The data area of the
 * recovery structure contains the ext. hashing identifier,
 * followed by the entry to be inserted back.
 */
int
ehash_rv_delete_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  EHID ehid;
  OID oid;
  char *record_p = (char *) recv_p->data;
  int error = NO_ERROR;
#if defined (ENABLE_UNUSED_FUNCTION)
  short record_type = recv_p->offset;
#endif

  record_p = ehash_read_ehid_from_record (record_p, &ehid);
  record_p = ehash_read_oid_from_record (record_p, &oid);

  /* Now rec_ptr is pointing to the key value */
#if defined (ENABLE_UNUSED_FUNCTION)
  if (record_type == REC_BIGONE)
    {
      /* This record is for a long string. At the monent it seems more logical to compose the whole key here rather
       * than at the logging time. But, if the overflow pages cannot be guaranteed to exist when this function is
       * called, then the whole key would need to be logged and this portion of the code becomes unnecessary. */
      RECDES recdes;
      VPID *vpid = (VPID *) record_p;
      char *long_str;

      /* Compose the whole key */
      recdes.data = (char *) recv_p->data + sizeof (EHID);
      recdes.length = recdes.area_size = recv_p->length - sizeof (EHID);
      long_str = ehash_compose_overflow (thread_p, &recdes);
      if (long_str == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (ehash_insert_helper (thread_p, &ehid, (void *) long_str, &oid, S_LOCK, vpid) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      free_and_init (long_str);
    }
  else
#endif
    {
      if (ehash_insert_helper (thread_p, &ehid, (void *) record_p, &oid, S_LOCK, NULL) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
    }

  return (error);
}

/*
 * ehash_rv_delete () - Recovery delete function
 *   return: int
 *   ehid(in): identifier for the extendible hashing structure
 *   key(in): Key value to remove
 *
 * Note: This is the recovery version of the "ehash_delete" function. Just
 * like the original function, this one also deletes the given
 * key value (together with its assoc_value) from the specified
 * extendible hashing structure, and returns an error code if the
 * key does not exist. However, unlike "ehash_delete", it does not
 * check the condition of bucket and invoke "eh_try_to_merge"
 * when possible. Nor does it produce undo log information. (It
 * merely produces redo log to produce the compansating log
 * record for this operation. This is to minimize the recovery
 * and transaction abort activities.)
 */
static int
ehash_rv_delete (THREAD_ENTRY * thread_p, EHID * ehid_p, void *key_p)
{
  EHASH_DIR_HEADER *dir_header_p;
  PAGE_PTR dir_root_page_p;
  VPID bucket_vpid;
  PAGE_PTR bucket_page_p;
  PGSLOTID slot_no;

  RECDES bucket_recdes;
  RECDES log_recdes_redo;
  char *log_redo_record_p;
  int error = NO_ERROR;

  dir_root_page_p =
    ehash_find_bucket_vpid_with_hash (thread_p, ehid_p, key_p, PGBUF_LATCH_READ, PGBUF_LATCH_WRITE, &bucket_vpid, NULL,
				      NULL);
  if (dir_root_page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  dir_header_p = (EHASH_DIR_HEADER *) dir_root_page_p;

  if (bucket_vpid.pageid == NULL_PAGEID)
    {
      pgbuf_unfix_and_init (thread_p, dir_root_page_p);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_EH_UNKNOWN_KEY, 0);
      return ER_EH_UNKNOWN_KEY;
    }
  else
    {
      bucket_page_p = ehash_fix_old_page (thread_p, &ehid_p->vfid, &bucket_vpid, PGBUF_LATCH_WRITE);
      if (bucket_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      /* Try to delete key from buc_page */

      /* Check if deletion possible, or not */
      if (ehash_locate_slot (thread_p, bucket_page_p, dir_header_p->key_type, key_p, &slot_no) == false)
	{
	  /* Key does not exist, so return errorcode */
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  pgbuf_unfix_and_init (thread_p, dir_root_page_p);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_EH_UNKNOWN_KEY, 0);
	  return ER_EH_UNKNOWN_KEY;
	}
      else
	{
	  /* Key exists in the bucket */

	  /* Put the redo (compansating) log record */
	  (void) spage_get_record (thread_p, bucket_page_p, slot_no, &bucket_recdes, PEEK);

	  /* Prepare the redo log record */
	  if (ehash_allocate_recdes (&log_recdes_redo, bucket_recdes.length + sizeof (short), REC_HOME) == NULL)
	    {
	      /*
	       * Will not be able to log a compensating log record... continue
	       * anyhow...without the log...since we are doing recovery anyhow
	       */
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  else
	    {
	      log_redo_record_p = log_recdes_redo.data;

	      /* First insert the key type */
	      *(short *) log_redo_record_p = bucket_recdes.type;
	      log_redo_record_p += sizeof (short);

	      /* Copy (the assoc-value, key) pair from the bucket record */
	      memcpy (log_redo_record_p, bucket_recdes.data, bucket_recdes.length);

	      /* why is redo logged inside an undo? */
	      log_append_redo_data2 (thread_p, RVEH_DELETE, &ehid_p->vfid, bucket_page_p, slot_no,
				     log_recdes_redo.length, log_recdes_redo.data);

	      ehash_free_recdes (&log_recdes_redo);
	    }

	  /* Delete the bucket record from the bucket; */
	  (void) spage_delete (thread_p, bucket_page_p, slot_no);
	  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);

	  /* Do not remove the overflow pages here */
	  /* Since they will be removed by their own undo functions */
	}
    }

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  pgbuf_unfix_and_init (thread_p, dir_root_page_p);

  return error;
}

/*
 * ehash_rv_increment () - Recovery increment counter
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: This function increments the value of an (integer) counter
 * during the recovery (or transaction abort) time. The location
 * of the counter and the amount of increment is passed as the
 * first and second element, respectively, on the data area of
 * the recovery structure.
 */
int
ehash_rv_increment (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  int inc_cnt;

  inc_cnt = *(int *) recv_p->data;

  *(int *) ((char *) recv_p->pgptr + recv_p->offset) += inc_cnt;

  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * ehash_rv_connect_bucket_redo () - Recovery connect bucket
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: This function sets a number of directory pointers (on the
 * same directory page) to the specified bucket pageid. The
 * bucket pageid, and the number of pointers to be updated are
 * passed as the first and the second elements, respectively,
 * of the data area of the recovery stucture.
 */
int
ehash_rv_connect_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  EHASH_REPETITION repetition;
  EHASH_DIR_RECORD *dir_record_p;
  int i;

  repetition = *(EHASH_REPETITION *) recv_p->data;

  /* Update the directory page */
  dir_record_p = (EHASH_DIR_RECORD *) ((char *) recv_p->pgptr + recv_p->offset);
  for (i = 0; i < repetition.count; i++)
    {

      if (!VPID_EQ (&(dir_record_p->bucket_vpid), &repetition.vpid))
	{
	  /* Yes, correction is needed */
	  dir_record_p->bucket_vpid = repetition.vpid;
	  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);
	}

      dir_record_p++;
    }

  return NO_ERROR;
}

static char *
ehash_read_oid_from_record (char *record_p, OID * oid_p)
{
  oid_p->pageid = *(PAGEID *) record_p;
  record_p += sizeof (PAGEID);

  oid_p->volid = *(VOLID *) record_p;
  record_p += sizeof (VOLID);

  oid_p->slotid = *(PGSLOTID *) record_p;
  record_p += sizeof (PGSLOTID);

  return record_p;
}

static char *
ehash_write_oid_to_record (char *record_p, OID * oid_p)
{
  *(PAGEID *) record_p = oid_p->pageid;
  record_p += sizeof (PAGEID);

  *(VOLID *) record_p = oid_p->volid;
  record_p += sizeof (VOLID);

  *(PGSLOTID *) record_p = oid_p->slotid;
  record_p += sizeof (PGSLOTID);

  return record_p;
}

static char *
ehash_read_ehid_from_record (char *record_p, EHID * ehid_p)
{
  ehid_p->pageid = *(PAGEID *) record_p;
  record_p += sizeof (PAGEID);

  ehid_p->vfid.fileid = *(FILEID *) record_p;
  record_p += sizeof (FILEID);

  ehid_p->vfid.volid = *(VOLID *) record_p;
  record_p += (sizeof (EHID) - sizeof (PAGEID) - sizeof (FILEID));

  return record_p;
}

static char *
ehash_write_ehid_to_record (char *record_p, EHID * ehid_p)
{
  *(PAGEID *) record_p = ehid_p->pageid;
  record_p += sizeof (PAGEID);

  *(FILEID *) record_p = ehid_p->vfid.fileid;
  record_p += sizeof (FILEID);

  *(VOLID *) record_p = ehid_p->vfid.volid;
  record_p += (sizeof (EHID) - sizeof (PAGEID) - sizeof (FILEID));

  return record_p;
}
