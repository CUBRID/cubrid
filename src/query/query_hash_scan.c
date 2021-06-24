/*
 *
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

#include "fetch.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "string_opfunc.h"
#include "query_hash_scan.h"
#include "db_value_printer.hpp"
#include "dbtype.h"
#include "chartype.h"
#include "storage_common.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "file_manager.h"
#include "db_date.h"
#include "thread_compat.hpp"
#include "oid.h"


static bool safe_memcpy (void *data, void *source, int size);
static DB_VALUE_COMPARE_RESULT qdata_hscan_key_compare (HASH_SCAN_KEY * ckey1, HASH_SCAN_KEY * ckey2, int *diff_pos);

/****************************************************************************/
/************************ file hash structure *******************************/
/****************************************************************************/
/* Number of bits pseudo key consists of */
#define FHS_HASH_KEY_BITS           (sizeof (FHS_HASH_KEY) * 8)

/* Number of bits the short data type consists of */
#define FHS_SHORT_BITS  (sizeof (short) * 8)

#define FHS_FLAG_INDIRECT	-1	/* 0xFFFF, record is page id for duplicate key bucket */
#define FHS_FLAG_DUMMY_NUM	0	/* meaningless flag of record in dk bucket */

#define FHS_HEADER_SLOT_ID	0
#define FHS_FIRST_SLOT_ID	1

/* key */
typedef unsigned int FHS_HASH_KEY;
#define FHS_KEY_SIZE (sizeof (FHS_HASH_KEY))
#define FHS_ALIGNMENT ((char) FHS_KEY_SIZE)
#define FHS_MAX_DUP_KEY 100	/* 10% of MAXNUM (PAGE 16K / RECORD 14 bytes) */
				/* TO_DO : adjust it properly using statistical information in optimizer */

#define SET_VPID(dest_vpid, vol_id, page_id)  \
  do \
    { \
      (dest_vpid).volid = vol_id; \
      (dest_vpid).pageid = page_id; \
    } \
  while (0)

/* Directory elements : Pointer to the bucket */
typedef struct fhs_dir_record FHS_DIR_RECORD;
struct fhs_dir_record
{
  VPID bucket_vpid;		/* bucket pointer */
};

/* Each bucket has a header area to store its local depth */
typedef struct fhs_bucket_header FHS_BUCKET_HEADER;
struct fhs_bucket_header
{
  char local_depth;		/* The local depth of the bucket */
};

typedef struct fhs_dk_bucket_header FHS_DK_BUCKET_HEADER;
struct fhs_dk_bucket_header
{
  VPID next_bucket;		/* bucket pointer */
};

typedef enum
{
  FHS_SUCCESSFUL_COMPLETION,
  FHS_BUCKET_FULL,		/* Bucket full condition; used in insertion */
  FHS_ERROR_OCCURRED
} FHS_RESULT;

static void inline fhs_read_tftid_from_record (char *record_p, TFTID * tftid_p);
static void inline fhs_read_key_from_record (char *record_p, int *key);
static void inline fhs_read_flag_from_record (char *record_p, short *flag);
static char inline *fhs_write_tftid_to_record (char *record_p, TFTID * tftid_p);
static char inline *fhs_write_key_to_record (char *record_p, void *key_p);
static char inline *fhs_write_flag_to_record (char *record_p, short flag);
static int fhs_compose_record (THREAD_ENTRY * thread_p, void *key_p, TFTID * value_p, RECDES * recdes_p, short flag);
static PAGE_PTR fhs_fix_ehid_page (THREAD_ENTRY * thread_p, EHID * ehid, PGBUF_LATCH_MODE latch_mode);
static PAGE_PTR fhs_fix_old_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, const VPID * vpid_p,
				  PGBUF_LATCH_MODE latch_mode);
static PAGE_PTR fhs_fix_nth_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, int offset,
				  PGBUF_LATCH_MODE latch_mode);
static void fhs_dir_locate (int *out_page_no_p, int *out_offset_p);
static char *fhs_allocate_recdes (THREAD_ENTRY * thread_p, RECDES * recdes_p, int size, short type);
static void fhs_free_recdes (THREAD_ENTRY * thread_p, RECDES * recdes_p);
static int fhs_initialize_dir_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args);
static int fhs_initialize_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args);
static int fhs_initialize_dk_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args);
static bool fhs_binary_search_bucket (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, PGSLOTID num_record,
				      void *key_p, PGSLOTID * out_position_p, bool need_to_backward);
static FHS_HASH_KEY fhs_hash (void *original_key_p);
static FHS_HASH_KEY fhs_hash_four_bytes_type (char *key_p);
static int fhs_compare_key (THREAD_ENTRY * thread_p, char *bucket_record_p, void *key_p,
			    INT16 record_type, int *out_compare_result_p);
static bool fhs_locate_slot (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, void *key_p,
			     PGSLOTID * out_position_p, bool need_to_backward);
static int fhs_find_bucket_vpid_with_hash (THREAD_ENTRY * thread_p, FHSID * fhsid_p, void *key_p,
					   PGBUF_LATCH_MODE root_latch, PGBUF_LATCH_MODE bucket_latch,
					   VPID * out_vpid_p, FHS_HASH_KEY * out_hash_key_p, int *out_location_p);
static int fhs_insert_to_bucket_after_create (THREAD_ENTRY * thread_p, FHSID * fhsid_p, VPID * bucket_vpid_p,
					      int location, FHS_HASH_KEY hash_key, void *key_p, TFTID * value_p);
static FHS_RESULT fhs_insert_bucket_after_extend_if_need (THREAD_ENTRY * thread_p, FHSID * fhsid_p,
							  VPID * bucket_vpid_p, void *key_p, FHS_HASH_KEY hash_key,
							  TFTID * value_p);
static char fhs_find_depth (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int location, VPID * bucket_vpid_p,
			    VPID * sibling_vpid_p);
static int fhs_connect_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int local_depth, FHS_HASH_KEY hash_key,
			       VPID * bucket_vpid_p);
static FHS_RESULT fhs_insert_to_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p, void *key_p,
					TFTID * value_p);
static FHS_RESULT fhs_insert_to_dk_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, VPID * next_bucket, void *key_p,
					   TFTID * value_p);
static PAGE_PTR fhs_extend_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p, void *key_p,
				   FHS_HASH_KEY hash_key, int *out_new_bit_p, VPID * bucket_vpid);
static PAGE_PTR fhs_split_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p, void *key_p,
				  int *out_old_local_depth_p, int *out_new_local_depth_p, VPID * sibling_vpid_p);
static int fhs_expand_directory (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int new_depth);
static int fhs_find_first_bit_position (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p,
					FHS_BUCKET_HEADER * bucket_header_p, void *key_p, int num_recs,
					PGSLOTID first_slot_id, int *out_old_local_depth_p, int *out_new_local_depth_p);
static int fhs_distribute_records_into_two_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p,
						   PAGE_PTR bucket_page_p, FHS_BUCKET_HEADER * bucket_header_p,
						   int num_recs, PGSLOTID first_slot_id, PAGE_PTR sibling_page_p);
static int fhs_get_pseudo_key (THREAD_ENTRY * thread_p, RECDES * recdes_p, FHS_HASH_KEY * out_hash_key_p);

/* Number of pointers the first page of the directory contains */
#define FHS_NUM_FIRST_PAGES \
  ((DB_PAGESIZE) / SSIZEOF (FHS_DIR_RECORD))

/* Offset of the last pointer in the first directory page */
#define FHS_LAST_OFFSET_IN_FIRST_PAGE \
  ((FHS_NUM_FIRST_PAGES - 1) * sizeof(FHS_DIR_RECORD))

/* Number of pointers for each directory page (other than the first one)  */
#define FHS_NUM_NON_FIRST_PAGES \
  (DB_PAGESIZE / sizeof(FHS_DIR_RECORD))

/* Offset of the last pointer in the other directory pages */
#define FHS_LAST_OFFSET_IN_NON_FIRST_PAGE \
  ((FHS_NUM_NON_FIRST_PAGES - 1) * sizeof(FHS_DIR_RECORD))

#define GETBITS(value, pos, n) \
  ( ((value) >> ( FHS_HASH_KEY_BITS - (pos) - (n) + 1)) & (~(~0UL << (n))) )
#define FIND_OFFSET(hash_key, depth) (GETBITS((hash_key), 1, (depth)))
#define GETBIT(word, pos) (GETBITS((word), (pos), 1))
#define SETBIT(word,  pos) ( (word) | (1 << (FHS_HASH_KEY_BITS - (pos))) )
#define CLEARBIT(word, pos) ( (word) & ~(1 << (FHS_HASH_KEY_BITS - (pos))) )

/****************************************************************************/
/************************ file hash structure *******************************/
/****************************************************************************/

/*
 * qdata_alloc_hscan_key () - allocate new hash key
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 *   val_cnt(in): size of key
 *   alloc_vals(in): if true will allocate dbvalues
 */
HASH_SCAN_KEY *
qdata_alloc_hscan_key (cubthread::entry * thread_p, int val_cnt, bool alloc_vals)
{
  HASH_SCAN_KEY *key;
  int i;

  key = (HASH_SCAN_KEY *) db_private_alloc (thread_p, sizeof (HASH_SCAN_KEY));
  if (key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_SCAN_KEY));
      return NULL;
    }

  key->values = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * val_cnt);
  if (key->values == NULL)
    {
      db_private_free (thread_p, key);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *) * val_cnt);
      return NULL;
    }

  if (alloc_vals)
    {
      for (i = 0; i < val_cnt; i++)
	{
	  key->values[i] = pr_make_value ();
	  if (key->values[i] == NULL)
	    {
	      key->free_values = true;
	      qdata_free_hscan_key (thread_p, key, i);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
	      return NULL;
	    }
	}
    }

  key->val_count = val_cnt;
  key->free_values = alloc_vals;
  return key;
}

/*
 * qdata_free_hscan_key () - free hash key
 *   thread_p(in): thread
 *   key(in): hash key
 */
void
qdata_free_hscan_key (cubthread::entry * thread_p, HASH_SCAN_KEY * key, int val_count)
{
  if (key == NULL)
    {
      return;
    }

  if (key->values != NULL)
    {
      if (key->free_values)
	{
	  for (int i = 0; i < val_count; i++)
	    {
	      if (key->values[i])
		{
		  pr_free_value (key->values[i]);
		}
	    }
	}

      /* free values array */
      db_private_free (thread_p, key->values);
    }

  /* free structure */
  db_private_free (thread_p, key);
}

/*
 * qdata_hash_scan_key () - compute hash of aggregate key
 *   returns: hash value
 *   key(in): key
 *   ht_size(in): hash table size (in buckets)
 */
unsigned int
qdata_hash_scan_key (const void *key, unsigned int ht_size, HASH_METHOD hash_method)
{
  HASH_SCAN_KEY *ckey = (HASH_SCAN_KEY *) key;
  unsigned int hash_val = 0, tmp_hash_val;
  int i;

  /* build hash value */
  for (i = 0; i < ckey->val_count; i++)
    {
      tmp_hash_val = mht_get_hash_number (ht_size, ckey->values[i]);
      hash_val = hash_val ^ tmp_hash_val;
      if (hash_val == 0)
	{
	  hash_val = tmp_hash_val;
	}
    }

  if (hash_method == HASH_METH_HASH_FILE)
    {
      hash_val = fhs_hash (&hash_val);
    }

  return hash_val;
}

/*
 * qdata_hscan_key_compare () - compare two aggregate keys
 *   returns: comparison result
 *   key1(in): first key
 *   key2(in): second key
 *   diff_pos(out): if not equal, position of difference, otherwise -1
 */
static DB_VALUE_COMPARE_RESULT
qdata_hscan_key_compare (HASH_SCAN_KEY * ckey1, HASH_SCAN_KEY * ckey2, int *diff_pos)
{
  DB_VALUE_COMPARE_RESULT result;
  int i;

  assert (diff_pos);
  *diff_pos = -1;

  if (ckey1 == ckey2)
    {
      /* same pointer, same values */
      return DB_EQ;
    }

  if (ckey1->val_count != ckey2->val_count)
    {
      /* can't compare keys of different sizes; shouldn't get here */
      assert (false);
      return DB_UNK;
    }

  for (i = 0; i < ckey1->val_count; i++)
    {
      result = tp_value_compare (ckey1->values[i], ckey2->values[i], 0, 1);
      if (result != DB_EQ)
	{
	  *diff_pos = i;
	  return result;
	}
    }

  /* if we got this far, it's equal */
  return DB_EQ;
}

/*
 * qdata_hscan_key_eq () - check equality of two aggregate keys
 *   returns: true if equal, false otherwise
 *   key1(in): first key
 *   key2(in): second key
 */
int
qdata_hscan_key_eq (const void *key1, const void *key2)
{
  int decoy;

  /* compare for equality */
  return (qdata_hscan_key_compare ((HASH_SCAN_KEY *) key1, (HASH_SCAN_KEY *) key2, &decoy) == DB_EQ);
}

/*
 * qdata_build_hscan_key () - build aggregate key structure from reguvar list
 *   returns: NO_ERROR or error code
 *   thread_p(in): thread
 *   key(out): aggregate key
 *   regu_list(in): reguvar list for fetching values
 */
int
qdata_build_hscan_key (THREAD_ENTRY * thread_p, val_descr * vd, REGU_VARIABLE_LIST regu_list, HASH_SCAN_KEY * key)
{
  int rc = NO_ERROR;

  /* build key */
  key->free_values = false;	/* references precreated DB_VALUES */
  key->val_count = 0;
  while (regu_list != NULL)
    {
      rc = fetch_peek_dbval (thread_p, &regu_list->value, vd, NULL, NULL, NULL, &key->values[key->val_count]);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      /* next */
      regu_list = regu_list->next;
      key->val_count++;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_print_hash_scan_entry () - Print the entry
 *                              Will be used by mht_dump() function
 *   return:
 *   fp(in)     :
 *   key(in)    :
 *   data(in)   :
 *   args(in)   :
 */
int
qdata_print_hash_scan_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *data, void *args)
{
  HASH_SCAN_VALUE *data2 = (HASH_SCAN_VALUE *) data;
  HASH_METHOD hash_list_scan_type = args ? *((HASH_METHOD *) args) : HASH_METH_NOT_USE;

  if (data2 == NULL || args == NULL)
    {
      return false;
    }
  if (fp == NULL)
    {
      fp = stdout;
    }

  fprintf (fp, "LIST_CACHE_ENTRY (%p) {\n", data);
  if (hash_list_scan_type == HASH_METH_IN_MEM)
    {
      fprintf (fp, "data_size = [%d]  data = [%.*s]\n", QFILE_GET_TUPLE_LENGTH (data2->tuple),
	       QFILE_GET_TUPLE_LENGTH (data2->tuple), data2->tuple);
    }
  else if (hash_list_scan_type == HASH_METH_HYBRID)
    {
      fprintf (fp, "pageid = [%d]  volid = [%d]  offset = [%d]\n", data2->pos->vpid.pageid,
	       data2->pos->vpid.volid, data2->pos->offset);
    }

  fprintf (fp, "\n}");

  return true;
}

/*
 * qdata_copy_hscan_key () - deep copy hash key
 *   returns: pointer to new hash key
 *   thread_p(in): thread
 *   key(in): source key
 */
HASH_SCAN_KEY *
qdata_copy_hscan_key (cubthread::entry * thread_p, HASH_SCAN_KEY * key, REGU_VARIABLE_LIST probe_regu_list,
		      val_descr * vd)
{
  HASH_SCAN_KEY *new_key = NULL;
  int i = 0;
  DB_TYPE vtype1, vtype2;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  if (key)
    {
      /* make a copy */
      new_key = qdata_alloc_hscan_key (thread_p, key->val_count, false);
    }

  if (new_key)
    {
      /* copy values */
      new_key->val_count = key->val_count;
      new_key->free_values = true;
      for (i = 0; i < key->val_count; i++)
	{
	  vtype1 = REGU_VARIABLE_GET_TYPE (&probe_regu_list->value);
	  vtype2 = DB_VALUE_DOMAIN_TYPE (key->values[i]);

	  if (vtype1 != vtype2)
	    {
	      new_key->values[i] = pr_make_value ();
	      if (new_key->values[i] == NULL)
		{
		  qdata_free_hscan_key (thread_p, new_key, i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
		  return NULL;
		}

	      status = tp_value_coerce (key->values[i], new_key->values[i], probe_regu_list->value.domain);
	      if (status != DOMAIN_COMPATIBLE)
		{
		  qdata_free_hscan_key (thread_p, new_key, ++i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (vtype2),
			  pr_type_name (vtype1));
		  return NULL;
		}
	    }
	  else
	    {
	      new_key->values[i] = pr_copy_value (key->values[i]);
	      if (new_key->values[i] == NULL)
		{
		  qdata_free_hscan_key (thread_p, new_key, i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
		  return NULL;
		}
	    }
	  probe_regu_list = probe_regu_list->next;
	}
    }

  return new_key;
}

/*
 * qdata_copy_hscan_key_without_alloc () - deep copy hash key
 *   returns: pointer to new hash key
 *   thread_p(in): thread
 *   key(in): source key
 */
HASH_SCAN_KEY *
qdata_copy_hscan_key_without_alloc (cubthread::entry * thread_p, HASH_SCAN_KEY * key,
				    REGU_VARIABLE_LIST probe_regu_list, HASH_SCAN_KEY * new_key)
{
  DB_TYPE vtype1, vtype2;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  if (key == NULL)
    {
      return NULL;
    }
  if (new_key)
    {
      /* copy values */
      new_key->val_count = key->val_count;
      for (int i = 0; i < key->val_count; i++)
	{
	  vtype1 = REGU_VARIABLE_GET_TYPE (&probe_regu_list->value);
	  vtype2 = DB_VALUE_DOMAIN_TYPE (key->values[i]);

	  if (vtype1 != vtype2)
	    {
	      pr_clear_value (new_key->values[i]);
	      status = tp_value_coerce (key->values[i], new_key->values[i], probe_regu_list->value.domain);
	      if (status != DOMAIN_COMPATIBLE)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (vtype2),
			  pr_type_name (vtype1));
		  return NULL;
		}
	    }
	  else
	    {
	      pr_clear_value (new_key->values[i]);
	      if (pr_clone_value (key->values[i], new_key->values[i]) != NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
		  return NULL;
		}
	    }
	  probe_regu_list = probe_regu_list->next;
	}
    }

  return new_key;
}

/*
 * qdata_alloc_hscan_value () - allocate new hash value
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 */
HASH_SCAN_VALUE *
qdata_alloc_hscan_value (cubthread::entry * thread_p, QFILE_TUPLE tpl)
{
  HASH_SCAN_VALUE *value;
  int tuple_size = QFILE_GET_TUPLE_LENGTH (tpl);

  /* alloc structure */
  value = (HASH_SCAN_VALUE *) db_private_alloc (thread_p, sizeof (HASH_SCAN_VALUE));
  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_SCAN_VALUE));
      return NULL;
    }

  value->tuple = (QFILE_TUPLE) db_private_alloc (thread_p, tuple_size);
  if (value->tuple == NULL)
    {
      qdata_free_hscan_value (thread_p, value);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, tuple_size);
      return NULL;
    }
  /* save tuple */
  if (!safe_memcpy (value->tuple, tpl, tuple_size))
    {
      qdata_free_hscan_value (thread_p, value);
      return NULL;
    }
  return value;
}

/*
 * qdata_alloc_hscan_value_OID () - allocate new hash OID value
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 */
HASH_SCAN_VALUE *
qdata_alloc_hscan_value_OID (cubthread::entry * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  HASH_SCAN_VALUE *value;

  /* alloc structure */
  value = (HASH_SCAN_VALUE *) db_private_alloc (thread_p, sizeof (HASH_SCAN_VALUE));
  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_SCAN_VALUE));
      return NULL;
    }

  value->pos = (QFILE_TUPLE_SIMPLE_POS *) db_private_alloc (thread_p, sizeof (QFILE_TUPLE_SIMPLE_POS));
  if (value->pos == NULL)
    {
      qdata_free_hscan_value (thread_p, value);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QFILE_TUPLE_SIMPLE_POS));
      return NULL;
    }

  /* save position */
  value->pos->offset = scan_id_p->curr_offset;
  value->pos->vpid = scan_id_p->curr_vpid;

  return value;
}

static bool
safe_memcpy (void *data, void *source, int size)
{
  if (size < 0)
    {
      return false;
    }
  memcpy (data, source, (size_t) size);
  return true;
}

/*
 * qdata_free_hscan_value () - free hash value
 *   thread_p(in): thread
 *   key(in): hash value
 */
void
qdata_free_hscan_value (cubthread::entry * thread_p, HASH_SCAN_VALUE * value)
{
  if (value == NULL)
    {
      return;
    }

  /* free values */
  if (value->data != NULL)
    {
      db_private_free_and_init (thread_p, value->data);
    }
  /* free structure */
  db_private_free_and_init (thread_p, value);
}

/*
 * qdata_free_agg_hentry () - free key-value pair of hash entry
 *   returns: error code or NO_ERROR
 *   key(in): key pointer
 *   data(in): value pointer
 *   args(in): args passed by mht_rem (should be null)
 */
int
qdata_free_hscan_entry (const void *key, void *data, void *args)
{
  /* free key */
  qdata_free_hscan_key ((cubthread::entry *) args, (HASH_SCAN_KEY *) key, key ? ((HASH_SCAN_KEY *) key)->val_count : 0);

  /* free tuple */
  qdata_free_hscan_value ((cubthread::entry *) args, (HASH_SCAN_VALUE *) data);

  /* all ok */
  return NO_ERROR;
}

/*
 * fhs_dump_bucket () - Print the bucket's contents
 *   return:
 *   buc_pgptr(in): bucket page whose contents is going to be dumped
 *
 * Note: A debugging function. Prints out the contents of the given bucket.
 */
static void
fhs_dump_bucket (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p)
{
  FHS_BUCKET_HEADER *bucket_header_p;
  FHS_DK_BUCKET_HEADER *dk_bucket_header_p;
  char *bucket_record_p;
  RECDES recdes;
  PGSLOTID slot_id, first_slot_id = -1;
  int key_size;
  TFTID assoc_value;
  int num_records;
  int i, key;
  short flag;

  (void) spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK);
  bucket_header_p = (FHS_BUCKET_HEADER *) recdes.data;
  dk_bucket_header_p = (FHS_DK_BUCKET_HEADER *) recdes.data;

  printf ("*************************************************************\n");
  printf ("*  local_depth : %d                                         *\n", bucket_header_p->local_depth);
  printf ("*  next_vpid   : %d                                         *\n", dk_bucket_header_p->next_bucket.pageid);
  printf ("*  no. records : %d                                         *\n",
	  spage_number_of_records (bucket_page_p) - 1);
  printf ("*                                                           *\n");
  printf ("*   No        Key         flag             Value            *\n");
  printf ("*  ====   =============  ========   ==================      *\n");

  num_records = spage_number_of_records (bucket_page_p);

  for (slot_id = 1; slot_id < num_records; slot_id++)
    {
      printf ("*   %2d", slot_id);

      spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK);
      bucket_record_p = (char *) recdes.data;
      fhs_read_tftid_from_record (bucket_record_p, &assoc_value);
      fhs_read_key_from_record (bucket_record_p, &key);
      fhs_read_flag_from_record (bucket_record_p, &flag);

      printf ("      %u  ", key);
      printf ("    %d       ", flag);
      printf ("(%5d,%5d,%5d)     *\n", assoc_value.volid, assoc_value.pageid, assoc_value.offset);
    }

  printf ("*************************************************************\n");
}

/*
 * fhs_dump () - Dump directory & all buckets
 *   return:
 *   fhsid(in): identifier for the extendible hashing structure to dump
 *
 * Note: A debugging function. Dumps the contents of the directory
 * and the buckets of specified ext. hashing structure.
 */
void
fhs_dump (THREAD_ENTRY * thread_p, FHSID * fhsid_p)
{
  FHS_DIR_RECORD *dir_record_p;
  int num_pages;
  int num_ptrs;

  int check_pages;
  int end_offset;
  int i;

  PAGE_PTR dir_page_p;
  PGLENGTH dir_offset;
  int dir_page_no;
  int dir_ptr_no;

  PAGE_PTR bucket_page_p;
  int bucket_page_no;

  if (fhsid_p == NULL)
    {
      return;
    }

  if (file_get_num_user_pages (thread_p, &fhsid_p->ehid.vfid, &num_pages) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return;
    }
  num_pages -= 1;		/* The first page starts with 0 */

  num_ptrs = 1 << fhsid_p->depth;
  end_offset = num_ptrs - 1;	/* Directory first pointer has an offset of 0 */
  fhs_dir_locate (&check_pages, &end_offset);

  if (check_pages != num_pages)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_ROOT_CORRUPTED, 3, fhsid_p->ehid.vfid.volid,
	      fhsid_p->ehid.vfid.fileid, fhsid_p->ehid.pageid);
      return;
    }

  printf ("*********************************************************\n");
  printf ("*                      DIRECTORY                        *\n");
  printf ("*                                                       *\n");
  printf ("*    Depth    :  %d                                      *\n", fhsid_p->depth);
  printf ("*    Key type : int                                     *\n");
  printf ("*    Key size :  %ld                                      *\n", sizeof (FHS_HASH_KEY));
  printf ("*                                                       *\n");
  printf ("*                      POINTERS                         *\n");
  printf ("*                                                       *\n");

  /* Print directory */

  dir_offset = 0;
  dir_page_no = 0;
  dir_ptr_no = 0;

  dir_page_p = fhs_fix_ehid_page (thread_p, &fhsid_p->ehid, PGBUF_LATCH_READ);
  if (dir_page_p == NULL)
    {
      return;
    }

  for (i = 0; i < num_ptrs; i++)
    {
      if (DB_PAGESIZE - dir_offset < SSIZEOF (FHS_DIR_RECORD))
	{
	  /* We reached the end of the directory page. The next bucket pointer is in the next directory page. */

	  /* Release previous page, and unlock it */
	  pgbuf_unfix_and_init (thread_p, dir_page_p);

	  dir_page_no++;

	  /* Get another page */
	  dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, dir_page_no, PGBUF_LATCH_READ);
	  if (dir_page_p == NULL)
	    {
	      return;
	    }

	  dir_offset = 0;
	}

      /* Print out the next directory record */
      dir_record_p = (FHS_DIR_RECORD *) ((char *) dir_page_p + dir_offset);

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
      dir_offset += sizeof (FHS_DIR_RECORD);
    }

  /* Release last page */
  pgbuf_unfix_and_init (thread_p, dir_page_p);

  printf ("*                                                       *\n");
  printf ("*********************************************************\n");

  /* Print buckets */

  if (file_get_num_user_pages (thread_p, &fhsid_p->bucket_file, &num_pages) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return;
    }
  num_pages -= 1;

  for (bucket_page_no = 0; bucket_page_no <= num_pages; bucket_page_no++)
    {
      bucket_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->bucket_file, bucket_page_no, PGBUF_LATCH_READ);
      if (bucket_page_p == NULL)
	{
	  return;
	}

      printf ("\n\n");
      fhs_dump_bucket (thread_p, bucket_page_p);
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }

  return;
}

static inline char *
fhs_write_tftid_to_record (char *record_p, TFTID * tftid_p)
{
  memcpy (record_p, tftid_p, sizeof (TFTID));
  record_p += sizeof (TFTID);
  return record_p;
}

static inline char *
fhs_write_key_to_record (char *record_p, void *key_p)
{
  *(int *) record_p = *(int *) key_p;
  record_p += sizeof (int);

  return record_p;
}

static inline char *
fhs_write_flag_to_record (char *record_p, short flag)
{
  *(short *) record_p = flag;
  record_p += sizeof (short);

  return record_p;
}

static inline void
fhs_read_tftid_from_record (char *record_p, TFTID * tftid_p)
{
  memcpy (tftid_p, record_p, sizeof (TFTID));
}

static inline void
fhs_read_key_from_record (char *record_p, int *key)
{
  record_p += sizeof (TFTID);
  *key = *(int *) record_p;
}

static inline void
fhs_read_flag_from_record (char *record_p, short *flag)
{
  record_p += sizeof (TFTID) + sizeof (int);
  *flag = *(short *) record_p;
}

/*
 * fhs_compose_record () -
 *   return: NO_ERROR, or ER_FAILED
 *   key_ptr(in): Pointer to the key
 *   value_ptr(in): Pointer to the associated value
 *   recdes(in): Pointer to the Record descriptor to fill in
 *
 * Note: record : TFTID  | key(int) | flag(short)
 *                8byte    4byte       2byte    = 14byte
 */
static int
fhs_compose_record (THREAD_ENTRY * thread_p, void *key_p, TFTID * value_p, RECDES * recdes_p, short flag)
{
  int record_size;
  char *record_p;

  /* record : TFTID | key(int) | flag(short) */
  record_size = sizeof (TFTID) + sizeof (FHS_HASH_KEY) + sizeof (short);
  if (fhs_allocate_recdes (thread_p, recdes_p, record_size, REC_HOME) == NULL)
    {
      return ER_FAILED;
    }

  recdes_p->type = REC_HOME;

  record_p = recdes_p->data;
  record_p = fhs_write_tftid_to_record (record_p, value_p);
  record_p = fhs_write_key_to_record (record_p, key_p);
  record_p = fhs_write_flag_to_record (record_p, flag);

  return NO_ERROR;
}

/*
 * fhs_fix_ehid_page () -
 *   return: specified page pointer, or NULL
 *   ehid(in): extendible hashing structure
 *   latch_mode(in): lock mode
 */
static PAGE_PTR
fhs_fix_ehid_page (THREAD_ENTRY * thread_p, EHID * ehid, PGBUF_LATCH_MODE latch_mode)
{
  VPID vpid;

  vpid.volid = ehid->vfid.volid;
  vpid.pageid = ehid->pageid;

  return fhs_fix_old_page (thread_p, &(ehid->vfid), &vpid, latch_mode);
}

/*
 * fhs_fix_old_page () -
 *   return: specified page pointer, or NULL
 *   vfid_p(in): only for error reporting
 *   vpid_p(in):
 *   latch_mode(in): lock mode
 */
static PAGE_PTR
fhs_fix_old_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, const VPID * vpid_p, PGBUF_LATCH_MODE latch_mode)
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
#endif
  return page_p;
}

/*
 * fhs_fix_nth_page () -
 *   return: specified page pointer, or NULL
 *   vfid(in):
 *   offset(in):
 *   lock(in): lock mode
 */
static PAGE_PTR
fhs_fix_nth_page (THREAD_ENTRY * thread_p, const VFID * vfid_p, int offset, PGBUF_LATCH_MODE latch_mode)
{
  VPID vpid;

  if (file_numerable_find_nth (thread_p, vfid_p, offset, false, NULL, NULL, &vpid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  return fhs_fix_old_page (thread_p, vfid_p, &vpid, latch_mode);
}

/*
 * fhs_dir_locate()
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
fhs_dir_locate (int *out_page_no_p, int *out_offset_p)
{
  int page_no, offset;

  offset = *out_offset_p;

  if (offset < FHS_NUM_FIRST_PAGES)
    {
      /* in the first page */
      offset = offset * sizeof (FHS_DIR_RECORD);
      page_no = 0;		/* at least one page */
    }
  else
    {
      /* not in the first page */
      offset -= FHS_NUM_FIRST_PAGES;
      page_no = offset / FHS_NUM_NON_FIRST_PAGES + 1;
      offset = (offset % FHS_NUM_NON_FIRST_PAGES) * sizeof (FHS_DIR_RECORD);
    }

  *out_page_no_p = page_no;
  *out_offset_p = offset;
}

/*
 * fhs_allocate_recdes () -
 *   return: char *, or NULL
 *   recdes(out) : Record descriptor
 *   size(in)    : Request size
 *   type(in)    : Request type
 */
static char *
fhs_allocate_recdes (THREAD_ENTRY * thread_p, RECDES * recdes_p, int size, short type)
{
  recdes_p->area_size = recdes_p->length = size;
  recdes_p->type = type;
  recdes_p->data = NULL;

  if (recdes_p->area_size > 0)
    {
      recdes_p->data = (char *) db_private_alloc (thread_p, recdes_p->area_size);
      if (recdes_p->data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) recdes_p->area_size);
	}
    }

  return recdes_p->data;
}

/*
 * fhs_free_recdes () -
 *   return:
 *   recdes(in): Record descriptor
 */
static void
fhs_free_recdes (THREAD_ENTRY * thread_p, RECDES * recdes_p)
{
  if (recdes_p->data)
    {
      db_private_free_and_init (thread_p, recdes_p->data);
    }
  recdes_p->area_size = recdes_p->length = 0;

}

/*
 * fhs_create () - Create an extendible hashing structure
 *   return: FHSID * (NULL in case of error)
 *   ehid(in): identifier for the extendible hashing structure to
 *             create. The volid field should already be set; others
 *             are set by this function.
 *   exp_num_entries(in): expected number of entries.
 *                   This figure is used as a guide to estimate the number of
 *		     pages the extendible hashing structure will occupy.
 *		     The purpose of this estimate is to increase the locality
 *		     of reference on the disk.
 *		     If the number of entries is not known, a negative value
 *		     should be passed to this parameter.
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
FHSID *
fhs_create (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int exp_num_entries)
{
  FHS_DIR_RECORD *dir_record_p = NULL;
  VFID dir_vfid = VFID_INITIALIZER;
  VPID dir_vpid = VPID_INITIALIZER;
  PAGE_PTR dir_page_p = NULL;
  VFID bucket_vfid = VFID_INITIALIZER;
  VPID bucket_vpid = VPID_INITIALIZER;
  char init_bucket_data;
  DKNPAGES exp_bucket_pages;
  DKNPAGES exp_dir_pages;
  short key_size;
  PAGE_TYPE ptype = PAGE_EHASH;

  if (fhsid_p == NULL)
    {
      return NULL;
    }

  /* Set the key size */
  key_size = sizeof (FHS_HASH_KEY);

  /* Estimate number of bucket pages */
  if (exp_num_entries < 0)
    {
      /* Assume minimum size */
      exp_bucket_pages = 1;
    }
  else
    {
      exp_bucket_pages = exp_num_entries * (key_size + sizeof (TFTID) + sizeof (short));
      exp_bucket_pages = CEIL_PTVDIV (exp_bucket_pages, DB_PAGESIZE);
    }

  /* Create the first bucket and initialize its header */
  if (file_create_ehash (thread_p, exp_bucket_pages, true, NULL, &bucket_vfid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  /* initialization of the first bucket page */
  init_bucket_data = 0;
  if (file_alloc (thread_p, &bucket_vfid, fhs_initialize_bucket_new_page, &init_bucket_data, &bucket_vpid, NULL)
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
      fhs_dir_locate (&exp_dir_pages, &exp_bucket_pages);
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

  if (file_create_ehash_dir (thread_p, exp_dir_pages, true, NULL, &dir_vfid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
  if (file_alloc (thread_p, &dir_vfid, file_init_temp_page_type, &ptype, &dir_vpid, &dir_page_p) != NO_ERROR)
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
#endif

  /* store bucket page id */
  dir_record_p = (FHS_DIR_RECORD *) dir_page_p;
  dir_record_p->bucket_vpid = bucket_vpid;

  /* Finishing up; release the pages and return directory file id */
  pgbuf_set_dirty (thread_p, dir_page_p, FREE);

  /* Initialize the file hash scan id */
  fhsid_p->depth = 0;
  fhsid_p->alignment = FHS_ALIGNMENT;
  VFID_COPY (&fhsid_p->bucket_file, &bucket_vfid);
  VFID_COPY (&fhsid_p->ehid.vfid, &dir_vfid);
  fhsid_p->ehid.pageid = dir_vpid.pageid;

  return fhsid_p;

exit_on_error:

  if (!VFID_ISNULL (&bucket_vfid))
    {
      if (file_destroy (thread_p, &bucket_vfid, true) != NO_ERROR)
	{
	  assert_release (false);
	}
    }
  if (!VFID_ISNULL (&dir_vfid))
    {
      if (file_destroy (thread_p, &dir_vfid, true) != NO_ERROR)
	{
	  assert_release (false);
	}
    }
  return NULL;
}

/*
 * fhs_destroy () - destroy the extensible hash table
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fhsid_p (in)   : extensible hash identifier
 *
 * note: only temporary extensible hash tables can be destroyed.
 */
int
fhs_destroy (THREAD_ENTRY * thread_p, FHSID * fhsid_p)
{
  if (fhsid_p == NULL)
    {
      return ER_FAILED;
    }

#if 0				/* for debug */
  fhs_dump (thread_p, fhsid_p);
#endif /* for debug */

  if (file_destroy (thread_p, &(fhsid_p->bucket_file), true) != NO_ERROR)
    {
      assert_release (false);
    }
  if (file_destroy (thread_p, &fhsid_p->ehid.vfid, true) != NO_ERROR)
    {
      assert_release (false);
    }

  return NO_ERROR;
}

/*
 * fhs_initialize_bucket_new_page () - Initialize a newly allocated page
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry
 * page_p (in)	 : New bucket page
 * args (in)     : depth. Used to initialize and log the page.
 */
static int
fhs_initialize_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args)
{
  char depth;
  FHS_BUCKET_HEADER bucket_header;
  RECDES bucket_recdes;
  PGSLOTID slot_id;
  int success = SP_SUCCESS;

  int error_code = NO_ERROR;

  depth = *((char *) args);

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
  spage_initialize (thread_p, page_p, UNANCHORED_KEEP_SEQUENCE, FHS_ALIGNMENT, DONT_SAFEGUARD_RVSPACE);

  /* Initialize the bucket header */
  bucket_header.local_depth = depth;

  /* Set the record descriptor to the Bucket header */
  bucket_recdes.data = (char *) &bucket_header;
  bucket_recdes.area_size = bucket_recdes.length = sizeof (FHS_BUCKET_HEADER);
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

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * fhs_initialize_dk_bucket_new_page () - Initialize a newly allocated page
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry
 * page_p (in)	 : New bucket page
 * args (in)     : depth. Used to initialize and log the page.
 */
static int
fhs_initialize_dk_bucket_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args)
{
  VPID next_bucket;
  FHS_DK_BUCKET_HEADER dk_bucket_header;
  RECDES bucket_recdes;
  PGSLOTID slot_id;
  int success = SP_SUCCESS;

  int error_code = NO_ERROR;

  next_bucket = *((VPID *) args);

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
  spage_initialize (thread_p, page_p, UNANCHORED_KEEP_SEQUENCE, FHS_ALIGNMENT, DONT_SAFEGUARD_RVSPACE);

  /* Initialize the bucket header */
  dk_bucket_header.next_bucket = next_bucket;

  /* Set the record descriptor to the Bucket header */
  bucket_recdes.data = (char *) &dk_bucket_header;
  bucket_recdes.area_size = bucket_recdes.length = sizeof (FHS_DK_BUCKET_HEADER);
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

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * fhs_initialize_dir_new_pages () - Initialize new page used to expand extensible hash directory.
 *
 * return	    : Error code
 * thread_p (in)    : Thread entry
 * page_p (in)	    : New directory page
 * ignore_args (in) : (not used)
 */
static int
fhs_initialize_dir_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, void *args)
{
  pgbuf_set_page_ptype (thread_p, page_p, PAGE_EHASH);
  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * fhs_search () - Search for the first key of duplicated keys;
 *			 return associated value.
 *   return: EH_SEARCH
 *   fhsid(in): identifier for the extendible hashing structure
 *   key(in): key to search
 *   value_ptr(out): pointer to return the value associated with the key
 *
 * Note: Returns the value associated with the given key, if it is
 * found in the specified extendible hashing structure. If the
 * key is not found an error condition is returned.
 */
EH_SEARCH
fhs_search (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hlsid_p, TFTID * value_p)
{
  PAGE_PTR bucket_page_p = NULL;
  PAGE_PTR dk_bucket_page_p = NULL;
  VPID bucket_vpid, dk_bucket_vpid;
  RECDES recdes;
  PGSLOTID slot_id;
  EH_SEARCH result = EH_KEY_NOTFOUND;
  short flag;
  TFTID temp_tftid;

  if (fhs_find_bucket_vpid_with_hash
      (thread_p, hlsid_p->file.hash_table, (void *) &hlsid_p->curr_hash_key, PGBUF_LATCH_READ, PGBUF_LATCH_READ,
       &bucket_vpid, NULL, NULL) != NO_ERROR)
    {
      return EH_ERROR_OCCURRED;
    }
  if (bucket_vpid.pageid == NULL_PAGEID)
    {
      result = EH_KEY_NOTFOUND;
      goto end;
    }
  bucket_page_p = fhs_fix_old_page (thread_p, &hlsid_p->file.hash_table->bucket_file, &bucket_vpid, PGBUF_LATCH_READ);
  if (bucket_page_p == NULL)
    {
      result = EH_ERROR_OCCURRED;
      goto end;
    }
  if (fhs_locate_slot (thread_p, bucket_page_p, (void *) &hlsid_p->curr_hash_key, &slot_id, false) == false)
    {
      result = EH_KEY_NOTFOUND;
      goto end;
    }
  if (spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK) != S_SUCCESS)
    {
      result = EH_ERROR_OCCURRED;
      goto end;
    }

  fhs_read_flag_from_record (recdes.data, &flag);
  if (flag == FHS_FLAG_INDIRECT)
    {
      /* indirect data. search DK bucket */
      hlsid_p->file.is_dk_bucket = true;
      fhs_read_tftid_from_record (recdes.data, &temp_tftid);
      SET_VPID (dk_bucket_vpid, temp_tftid.volid, temp_tftid.pageid);
      /* fix dk bucket page */
      dk_bucket_page_p =
	fhs_fix_old_page (thread_p, &hlsid_p->file.hash_table->bucket_file, &dk_bucket_vpid, PGBUF_LATCH_READ);
      if (dk_bucket_page_p == NULL)
	{
	  result = EH_ERROR_OCCURRED;
	  goto end;
	}
      /* get first record */
      if (spage_get_record (thread_p, dk_bucket_page_p, FHS_FIRST_SLOT_ID, &recdes, PEEK) != S_SUCCESS)
	{
	  result = EH_ERROR_OCCURRED;
	  goto end;
	}
      fhs_read_tftid_from_record (recdes.data, value_p);
      /* save last oid */
      SET_OID (&hlsid_p->file.curr_oid, dk_bucket_vpid.volid, dk_bucket_vpid.pageid, FHS_FIRST_SLOT_ID);
      result = EH_KEY_FOUND;
    }
  else
    {
      /* direct data */
      hlsid_p->file.is_dk_bucket = false;
      fhs_read_tftid_from_record (recdes.data, value_p);
      /* save last oid */
      SET_OID (&hlsid_p->file.curr_oid, bucket_vpid.volid, bucket_vpid.pageid, slot_id);
      result = EH_KEY_FOUND;
    }

end:
  if (bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }
  if (dk_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, dk_bucket_page_p);
    }

  return result;
}

/*
 * fhs_search_next () - Search for the next key; return associated value
 *   return: EH_SEARCH
 *   hlsid_p(in): identifier for the hash list scan
 *
 * Note: Returns the next value associated with the given key, if it is
 * found in the specified extendible hashing structure.
 */
EH_SEARCH
fhs_search_next (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hlsid_p, TFTID * value_p)
{
  PAGE_PTR bucket_page_p = NULL;
  PAGE_PTR next_bucket_page_p = NULL;
  VPID bucket_vpid, next_bucket_vpid;
  RECDES recdes;
  EH_SEARCH result = EH_KEY_NOTFOUND;
  char *bucket_key;
  int compare_result;
  PGSLOTID num_record;
  TFTID temp_tftid;

  if (hlsid_p->file.curr_oid.pageid == NULL_PAGEID)
    {
      return EH_KEY_NOTFOUND;
    }

  SET_VPID (bucket_vpid, hlsid_p->file.curr_oid.volid, hlsid_p->file.curr_oid.pageid);
  bucket_page_p = fhs_fix_old_page (thread_p, &hlsid_p->file.hash_table->ehid.vfid, &bucket_vpid, PGBUF_LATCH_READ);
  if (bucket_page_p == NULL)
    {
      result = EH_ERROR_OCCURRED;
      goto end;
    }

  if (hlsid_p->file.is_dk_bucket)
    {
      num_record = spage_number_of_records (bucket_page_p) - 1;
      if (hlsid_p->file.curr_oid.slotid >= num_record)
	{
	  /* already last slot id */
	  /* search data in next bucket */
	  (void) spage_get_record (thread_p, bucket_page_p, FHS_HEADER_SLOT_ID, &recdes, PEEK);
	  fhs_read_tftid_from_record (recdes.data, &temp_tftid);
	  SET_VPID (next_bucket_vpid, temp_tftid.volid, temp_tftid.pageid);
	  if (VPID_ISNULL (&next_bucket_vpid))
	    {
	      result = EH_KEY_NOTFOUND;
	      goto end;
	    }
	  next_bucket_page_p =
	    fhs_fix_old_page (thread_p, &hlsid_p->file.hash_table->ehid.vfid, &next_bucket_vpid, PGBUF_LATCH_READ);
	  /* get first record */
	  if (spage_get_record (thread_p, next_bucket_page_p, FHS_FIRST_SLOT_ID, &recdes, PEEK) != S_SUCCESS)
	    {
	      result = EH_ERROR_OCCURRED;
	      goto end;
	    }
	  fhs_read_tftid_from_record (recdes.data, value_p);
	  /* save last oid */
	  SET_OID (&hlsid_p->file.curr_oid, next_bucket_vpid.volid, next_bucket_vpid.pageid, FHS_FIRST_SLOT_ID);
	  result = EH_KEY_FOUND;
	  goto end;
	}
      else
	{
	  (void) spage_get_record (thread_p, bucket_page_p, ++hlsid_p->file.curr_oid.slotid, &recdes, PEEK);
	  fhs_read_tftid_from_record (recdes.data, value_p);
	  result = EH_KEY_FOUND;
	  goto end;
	}
    }
  else
    {
      if (hlsid_p->file.curr_oid.slotid <= 1)
	{
	  /* already last slot id */
	  result = EH_KEY_NOTFOUND;
	  goto end;
	}
      (void) spage_get_record (thread_p, bucket_page_p, --hlsid_p->file.curr_oid.slotid, &recdes, PEEK);

      /* compare key */
      bucket_key = (char *) recdes.data;
      bucket_key += sizeof (TFTID);
      if (fhs_compare_key (thread_p, bucket_key, (void *) &hlsid_p->curr_hash_key, recdes.type, &compare_result) !=
	  NO_ERROR)
	{
	  result = EH_ERROR_OCCURRED;
	  goto end;
	}
      if (compare_result != 0)
	{
	  /* the all keys is already found */
	  result = EH_KEY_NOTFOUND;
	  goto end;
	}

      fhs_read_tftid_from_record (recdes.data, value_p);
      result = EH_KEY_FOUND;
      goto end;
    }

end:
  if (bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
    }
  if (next_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, next_bucket_page_p);
    }
  return result;
}

static int
fhs_find_bucket_vpid_with_hash (THREAD_ENTRY * thread_p, FHSID * fhsid_p, void *key_p, PGBUF_LATCH_MODE root_latch,
				PGBUF_LATCH_MODE bucket_latch, VPID * out_vpid_p, FHS_HASH_KEY * out_hash_key_p,
				int *out_location_p)
{
  FHS_HASH_KEY hash_key;
  int location, dir_offset;
  FHS_DIR_RECORD *dir_record_p;
  PAGE_PTR dir_page_p;

  /* Get the pseudo key */
  hash_key = *((FHS_HASH_KEY *) key_p);
  if (out_hash_key_p)
    {
      *out_hash_key_p = hash_key;
    }

  /* Find the location of bucket pointer in the directory */
  location = FIND_OFFSET (hash_key, fhsid_p->depth);
  if (out_location_p)
    {
      *out_location_p = location;
    }

  /* Find the bucket page containing the key */
  fhs_dir_locate (&dir_offset, &location);
  dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, dir_offset, bucket_latch);
  if (dir_page_p == NULL)
    {
      return ER_FAILED;
    }
  dir_record_p = (FHS_DIR_RECORD *) ((char *) dir_page_p + location);
  pgbuf_unfix_and_init (thread_p, dir_page_p);

  *out_vpid_p = dir_record_p->bucket_vpid;

  return NO_ERROR;
}

/*
 * fhs_locate_slot () - Locate the slot for the key
 *   return: int
 *   buc_pgptr(in): pointer to the bucket page
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
fhs_locate_slot (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, void *key_p,
		 PGSLOTID * out_position_p, bool need_to_backward)
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

  return fhs_binary_search_bucket (thread_p, bucket_page_p, num_record, key_p, out_position_p, need_to_backward);
}

static int
fhs_compare_key (THREAD_ENTRY * thread_p, char *bucket_record_p, void *key_p, INT16 record_type,
		 int *out_compare_result_p)
{
  int compare_result;
  unsigned int u1, u2;

  u1 = *((unsigned int *) key_p);
  u2 = *((unsigned int *) bucket_record_p);

  if (u1 == u2)
    {
      compare_result = 0;
    }
  else if (u1 > u2)
    {
      compare_result = 1;
    }
  else
    {
      compare_result = -1;
    }

  *out_compare_result_p = compare_result;
  return NO_ERROR;
}

/*
 * fhs_hash () - Hash function
 *   return: FHS_HASH_KEY
 *   orig_key(in): original key to encode into a pseudo key
 *
 * Note: This function converts the given original key into a pseudo
 * key. Since the original key is presented as a character
 * string, its conversion into a int-compatible type is essential
 * prior to performing any operation on it.
 * This function does not change the value of parameter
 * orig_key, as it might be on a bucket.
 */
static FHS_HASH_KEY
fhs_hash (void *original_key_p)
{
  char *key = (char *) original_key_p;
  FHS_HASH_KEY hash_key = 0;
  hash_key = fhs_hash_four_bytes_type (key);

  return hash_key;
}

static FHS_HASH_KEY
fhs_hash_four_bytes_type (char *key_p)
{
  FHS_HASH_KEY hash_key = 0;
  unsigned int i;
  char Char;

  hash_key = (FHS_HASH_KEY) ntohl (*(unsigned int *) key_p);

  /* Go over the chars of the given pseudo key */
  Char = '\0';
  key_p = (char *) &hash_key;
  for (i = 0; i < sizeof (FHS_HASH_KEY); i++)
    {
      Char += (char) *key_p++;
    }

  /* Change the first byte of the pseudo key to the SUM of all of them */
  memcpy (&hash_key, &Char, sizeof (char));

  return hash_key;
}

static bool
fhs_binary_search_bucket (THREAD_ENTRY * thread_p, PAGE_PTR bucket_page_p, PGSLOTID num_record,
			  void *key_p, PGSLOTID * out_position_p, bool need_to_backward)
{
  char *bucket_record_p;
  RECDES recdes;
  PGSLOTID low, high;
  PGSLOTID middle;
  int compare_result;
  short flag;

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
      fhs_read_flag_from_record ((char *) recdes.data, &flag);
      bucket_record_p = (char *) recdes.data;
      bucket_record_p += sizeof (TFTID);

      if (fhs_compare_key (thread_p, bucket_record_p, key_p, recdes.type, &compare_result) != NO_ERROR)
	{
	  return false;
	}

      if (compare_result == 0)
	{
	  if (need_to_backward)
	    {
	      /* Find the first slotid */
	      while (compare_result == 0)
		{
		  if (middle == 1)
		    {
		      *out_position_p = middle;
		      return true;
		    }
		  --middle;

		  if (spage_get_record (thread_p, bucket_page_p, middle, &recdes, PEEK) != S_SUCCESS)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
		      return false;
		    }

		  bucket_record_p = (char *) recdes.data;
		  bucket_record_p += sizeof (TFTID);

		  if (fhs_compare_key (thread_p, bucket_record_p, key_p, recdes.type, &compare_result) != NO_ERROR)
		    {
		      return false;
		    }
		}
	      *out_position_p = middle + 1;
	    }
	  else
	    {
	      /* go to last slot using flag */
	      if (flag == FHS_FLAG_INDIRECT)
		{
		  *out_position_p = middle;
		}
	      else if (flag >= 1)
		{
		  *out_position_p = middle + flag - 1;
		}
	      else
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
		  return false;
		}
	    }
	  return true;
	}

      if (compare_result < 0)
	{
	  high = middle - 1;
	}
      else
	{
	  low = (flag > 0) ? middle + flag : middle + 1;
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
 * fhs_insert () - Insert (key, assoc_value) pair to ext. hashing
 *   return: void * (NULL is returned in case of error)
 *   fhsid(in): identifier of the extendible hashing structure
 *   key(in): key value to insert
 *   value_ptr(in): pointer to the associated value to insert
 *
 * Note: Inserts the given (key & assoc_value) pair into the specified
 * extendible hashing structure. If the key already exists then
 * the previous associated value is replaced with the new one.
 * Otherwise, a new record is inserted to the correct bucket.
 *      To perform the insertion operation safely in the concurrent
 * environment, an auxiliary (and private) function
 */
void *
fhs_insert (THREAD_ENTRY * thread_p, FHSID * fhsid_p, void *key_p, TFTID * value_p)
{
  VPID bucket_vpid;

  FHS_HASH_KEY hash_key;
  int location;
  FHS_RESULT result;

  if (fhs_find_bucket_vpid_with_hash (thread_p, fhsid_p, key_p,
				      PGBUF_LATCH_WRITE, PGBUF_LATCH_READ, &bucket_vpid, &hash_key,
				      &location) != NO_ERROR)
    {
      return NULL;
    }

  if (VPID_ISNULL (&bucket_vpid))
    {
      if (fhs_insert_to_bucket_after_create (thread_p, fhsid_p, &bucket_vpid, location, hash_key, key_p, value_p) !=
	  NO_ERROR)
	{
	  return NULL;
	}
    }
  else
    {
      result = fhs_insert_bucket_after_extend_if_need (thread_p, fhsid_p, &bucket_vpid, key_p, hash_key, value_p);
      if (result == FHS_ERROR_OCCURRED)
	{
	  return NULL;
	}
      else if (result == FHS_BUCKET_FULL)
	{
	  return fhs_insert (thread_p, fhsid_p, key_p, value_p);
	}
    }

  return (key_p);
}

static int
fhs_insert_to_bucket_after_create (THREAD_ENTRY * thread_p, FHSID * fhsid_p, VPID * bucket_vpid_p, int location,
				   FHS_HASH_KEY hash_key, void *key_p, TFTID * value_p)
{
  PAGE_PTR bucket_page_p;
  FHS_BUCKET_HEADER bucket_header;
  char found_depth;
  char init_bucket_data;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  FHS_RESULT ins_result;

  int error_code = NO_ERROR;

  found_depth = fhs_find_depth (thread_p, fhsid_p, location, &null_vpid, &null_vpid);
  if (found_depth == 0)
    {
      return ER_FAILED;
    }

  init_bucket_data = fhsid_p->depth - found_depth;
  bucket_header.local_depth = init_bucket_data;

  error_code = file_alloc (thread_p, &fhsid_p->bucket_file, fhs_initialize_bucket_new_page, &init_bucket_data,
			   bucket_vpid_p, &bucket_page_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }
  if (bucket_page_p == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, bucket_page_p, PAGE_EHASH);
#endif

  if (fhs_connect_bucket (thread_p, fhsid_p, bucket_header.local_depth, hash_key, bucket_vpid_p) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
      return ER_FAILED;
    }

  ins_result = fhs_insert_to_bucket (thread_p, fhsid_p, bucket_page_p, key_p, value_p);

  if (ins_result != FHS_SUCCESSFUL_COMPLETION)
    {
      /* Slotted page module refuses to insert a short size record to an almost empty page. This should never happen. */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      pgbuf_unfix_and_init (thread_p, bucket_page_p);
      return ER_FAILED;
    }

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  return NO_ERROR;
}

static FHS_RESULT
fhs_insert_bucket_after_extend_if_need (THREAD_ENTRY * thread_p, FHSID * fhsid_p, VPID * bucket_vpid_p, void *key_p,
					FHS_HASH_KEY hash_key, TFTID * value_p)
{
  PAGE_PTR bucket_page_p;
  PAGE_PTR sibling_page_p = NULL;
  PAGE_PTR target_bucket_page_p;
  int new_bit;
  FHS_RESULT result;

  /* We need to put a X_LOCK on bucket page */
  bucket_page_p = fhs_fix_old_page (thread_p, &fhsid_p->bucket_file, bucket_vpid_p, PGBUF_LATCH_WRITE);
  if (bucket_page_p == NULL)
    {
      return FHS_ERROR_OCCURRED;
    }

  result = fhs_insert_to_bucket (thread_p, fhsid_p, bucket_page_p, key_p, value_p);
  if (result == FHS_BUCKET_FULL)
    {
      sibling_page_p = fhs_extend_bucket (thread_p, fhsid_p, bucket_page_p, key_p, hash_key, &new_bit, bucket_vpid_p);
      if (sibling_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, bucket_page_p);
	  return FHS_ERROR_OCCURRED;
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

      result = fhs_insert_to_bucket (thread_p, fhsid_p, target_bucket_page_p, key_p, value_p);
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
    }

  pgbuf_unfix_and_init (thread_p, bucket_page_p);
  return result;
}

/*
 * fhs_find_depth () - Find depth
 *   return: char (will return 0 in case of error)
 *   fhsid(in): identifier for the extendible hashing structure
 *   location(in): directory entry whose neighbooring area should be checked
 *   bucket_vpid(in): vpid of the bucket pointed by the specified directory entry
 *   sib_vpid(in): vpid of the sibling bucket
 *
 */
static char
fhs_find_depth (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int location, VPID * bucket_vpid_p, VPID * sibling_vpid_p)
{
  PAGE_PTR page_p = NULL;
  FHS_DIR_RECORD *dir_record_p;
  int loc;
  int rel_loc;
  int iterations;
  int page_no;
  int prev_page_no = -1;
  int i;
  unsigned int clear;
  char check_depth = 2;
  bool is_stop = false;
  int check_bit;

  while ((check_depth <= fhsid_p->depth) && !is_stop)
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

      check_bit = GETBIT (loc, FHS_HASH_KEY_BITS - check_depth + 1);
      if (check_bit != 0)
	{
	  loc = CLEARBIT (loc, FHS_HASH_KEY_BITS - check_depth + 1);
	}
      else
	{
	  loc = SETBIT (loc, FHS_HASH_KEY_BITS - check_depth + 1);
	}

      /* "loc" is the base_location now */

      iterations = 1 << (check_depth - 2);

      for (i = 0; i < iterations; i++)
	{
	  rel_loc = loc | (i << 1);
	  fhs_dir_locate (&page_no, &rel_loc);
	  if (prev_page_no == -1)
	    {
	      /* first time */
	      page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, page_no, PGBUF_LATCH_WRITE);
	      if (page_p == NULL)
		{
		  return 0;
		}
	      prev_page_no = page_no;
	    }
	  else if (page_no != prev_page_no)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	      page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, page_no, PGBUF_LATCH_WRITE);
	      if (page_p == NULL)
		{
		  return 0;
		}
	      prev_page_no = page_no;
	    }
	  dir_record_p = (FHS_DIR_RECORD *) ((char *) page_p + rel_loc);

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

/*
 * fhs_connect_bucket () - Connect bucket to directory
 *   return: int NO_ERROR, or ER_FAILED
 *   fhsid(in): identifier for the extendible hashing structure
 *   local_depth(in): local depth of the bucket; determines how many directory
 *                    pointers are set to point to the given bucket identifier
 *   hash_key(in): pseudo key that led to the bucket page; determines which
 *                   pointers to be updated
 *   bucket_vpid(in): Identifier of the bucket page to connect
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
fhs_connect_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int local_depth, FHS_HASH_KEY hash_key,
		    VPID * bucket_vpid_p)
{
  FHS_DIR_RECORD *dir_record_p;
  PAGE_PTR page_p;
  int location;
  int first_page, last_page;
  int first_ptr_offset, last_ptr_offset;
  int start_offset, end_offset;
  int diff;
  int i, j;
  int bef_length, count;
  unsigned int set_bits;
  unsigned int clear_bits;

  /* First find out how many page entries will be updated in the directory */
  location = GETBITS (hash_key, 1, fhsid_p->depth);

  diff = fhsid_p->depth - local_depth;
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

      fhs_dir_locate (&first_page, &first_ptr_offset);
      fhs_dir_locate (&last_page, &last_ptr_offset);
    }
  else
    {
      /* There is only one pointer that needs to be updated */
      first_ptr_offset = location;
      fhs_dir_locate (&first_page, &first_ptr_offset);
      last_page = first_page;
      last_ptr_offset = first_ptr_offset;
    }

  /* Go over all of these pages */
  for (i = first_page; i <= last_page; i++)
    {
      page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, i, PGBUF_LATCH_WRITE);
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
	  end_offset = last_ptr_offset + sizeof (FHS_DIR_RECORD);
	}
      else
	{
	  end_offset = DB_PAGESIZE;
	}

      /* Log this page */
      bef_length = end_offset - start_offset;
      count = bef_length / sizeof (FHS_DIR_RECORD);

      /* Update the directory page */
      dir_record_p = (FHS_DIR_RECORD *) ((char *) page_p + start_offset);
      for (j = 0; j < count; j++)
	{
	  dir_record_p->bucket_vpid = *bucket_vpid_p;
	  dir_record_p++;
	}
      pgbuf_set_dirty (thread_p, page_p, FREE);
    }

  return NO_ERROR;
}

/*
 * fhs_insert_to_bucket () - Insert (key, value) to bucket
 *   return: FHS_RESULT
 *   fhsid(in): identifier for the extendible hashing structure
 *   buc_pgptr(in): bucket page to insert the key
 *   key_ptr(in): Pointer to the key
 *   value_ptr(in): Pointer to the associated value
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
static FHS_RESULT
fhs_insert_to_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p, void *key_p, TFTID * value_p)
{
  RECDES bucket_recdes;
  RECDES old_bucket_recdes;

  PGSLOTID slot_no, tmp_slot;
  int success;
  short flag;
  VPID dk_bucket_vpid;
  PAGE_PTR dk_bucket_page_p = NULL;
  TFTID tmp_tftid;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };

  /* Check if insertion is duplicate, or not */
  if (fhs_locate_slot (thread_p, bucket_page_p, key_p, &slot_no, true) == true)
    {
      /* Key already exists. Append new data to the last slot of duplicate keys. */
      (void) spage_get_record (thread_p, bucket_page_p, slot_no, &old_bucket_recdes, PEEK);
      fhs_read_flag_from_record (old_bucket_recdes.data, &flag);

      if (flag == FHS_FLAG_INDIRECT)
	{
	  /* the case of inserting record into DK bucket */
	  /* get the dk bucket vpid */
	  fhs_read_tftid_from_record (old_bucket_recdes.data, &tmp_tftid);
	  SET_VPID (dk_bucket_vpid, tmp_tftid.volid, tmp_tftid.pageid);
	  /* insert new record into dk_bucket */
	  if (fhs_insert_to_dk_bucket (thread_p, fhsid_p, &dk_bucket_vpid, key_p, value_p) != FHS_SUCCESSFUL_COMPLETION)
	    {
	      /* This should never happen. */
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	  return FHS_SUCCESSFUL_COMPLETION;
	}
      else if (flag + 1 >= FHS_MAX_DUP_KEY)
	{
	  /* the case of inserting firstly to DK bucket */
	  /* make DK bucket page */
	  success =
	    file_alloc (thread_p, &fhsid_p->bucket_file, fhs_initialize_dk_bucket_new_page, &null_vpid, &dk_bucket_vpid,
			&dk_bucket_page_p);
	  if (success != NO_ERROR || dk_bucket_page_p == NULL)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	  /* insert new record into dk_bucket */
	  if (fhs_compose_record (thread_p, key_p, value_p, &bucket_recdes, FHS_FLAG_DUMMY_NUM) != NO_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	  success = spage_insert (thread_p, dk_bucket_page_p, &bucket_recdes, &tmp_slot);
	  if (success != SP_SUCCESS)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	  fhs_free_recdes (thread_p, &bucket_recdes);

	  /* move duplicate key from bucket to dk_bucket */
	  success = spage_insert_at (thread_p, dk_bucket_page_p, tmp_slot, &old_bucket_recdes);
	  if (success != SP_SUCCESS)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	  (void) spage_delete (thread_p, bucket_page_p, slot_no);
	  for (int i = flag - 1; i > 0; i--)
	    {
	      (void) spage_get_record (thread_p, bucket_page_p, slot_no, &old_bucket_recdes, PEEK);
	      success = spage_insert_at (thread_p, dk_bucket_page_p, tmp_slot, &old_bucket_recdes);
	      if (success != SP_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  goto error;
		}
	      (void) spage_delete (thread_p, bucket_page_p, slot_no);
	      /* slot_no is not changed since the current slot is deleted */
	    }
	  /* make indirect record for bucket */
	  SET_TFTID (tmp_tftid, dk_bucket_vpid.volid, dk_bucket_vpid.pageid, 0);
	  if (fhs_compose_record (thread_p, key_p, &tmp_tftid, &bucket_recdes, FHS_FLAG_INDIRECT) != NO_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	}
      else
	{
	  /* the case of inserting duplicate key record into bucket */
	  if (fhs_compose_record (thread_p, key_p, value_p, &bucket_recdes, ++flag) != NO_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto error;
	    }
	}
    }
  else
    {
      /* Key does not exist in the bucket */
      if (fhs_compose_record (thread_p, key_p, value_p, &bucket_recdes, 1) != NO_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto error;
	}
    }
  /* insert it into the bucket */
  success = spage_insert_at (thread_p, bucket_page_p, slot_no, &bucket_recdes);
  if (success != SP_SUCCESS)
    {
      /* Problem the record was not inserted to the page */
      fhs_free_recdes (thread_p, &bucket_recdes);
      if (success == SP_DOESNT_FIT)
	{
	  /* There is not enough space on the slotted page for the new record */
	  if (dk_bucket_page_p)
	    {
	      pgbuf_set_dirty (thread_p, dk_bucket_page_p, FREE);
	    }
	  return FHS_BUCKET_FULL;
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto error;
	}
    }

  if (bucket_recdes.data)
    {
      fhs_free_recdes (thread_p, &bucket_recdes);
    }
  if (dk_bucket_page_p)
    {
      pgbuf_set_dirty (thread_p, dk_bucket_page_p, FREE);
    }

  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);
  return FHS_SUCCESSFUL_COMPLETION;

error:
  if (bucket_recdes.data)
    {
      fhs_free_recdes (thread_p, &bucket_recdes);
    }
  if (dk_bucket_page_p)
    {
      pgbuf_set_dirty (thread_p, dk_bucket_page_p, FREE);
    }
  return FHS_ERROR_OCCURRED;
}

/*
 * fhs_insert_to_dk_bucket () - Insert (key, value) to dk bucket
 *   return: FHS_RESULT
 *   fhsid(in): identifier for the extendible hashing structure
 *   buc_pgptr(in): bucket page to insert the key
 *   key_ptr(in): Pointer to the key
 *   value_ptr(in): Pointer to the associated value
 *
 * Note:
 * Insert (key, value) to DK bucket
 * if DK bucket is full, create new DK bucket and connect to prior DK bucket
 */
static FHS_RESULT
fhs_insert_to_dk_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, VPID * next_bucket_vpid, void *key_p,
			 TFTID * value_p)
{
  RECDES bucket_recdes, old_bucket_recdes;
  PGSLOTID tmp_slot;
  int success;
  FHS_DK_BUCKET_HEADER *dk_bucket_header_p;
  VPID tmp_bucket_vpid, cur_bucket_vpid;
  PAGE_PTR dk_bucket_page_p = NULL;
  PAGE_PTR new_dk_bucket_page_p = NULL;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };

  /* get last DK bucket page. TO_DO : add last_page for performance */
  tmp_bucket_vpid = *next_bucket_vpid;
  do
    {
      if (dk_bucket_page_p)
	{
	  pgbuf_unfix_and_init (thread_p, dk_bucket_page_p);
	}
      dk_bucket_page_p = fhs_fix_old_page (thread_p, &fhsid_p->bucket_file, &tmp_bucket_vpid, PGBUF_LATCH_WRITE);
      if (dk_bucket_page_p == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto error;
	}
      (void) spage_get_record (thread_p, dk_bucket_page_p, 0, &old_bucket_recdes, PEEK);
      dk_bucket_header_p = (FHS_DK_BUCKET_HEADER *) old_bucket_recdes.data;
      tmp_bucket_vpid = dk_bucket_header_p->next_bucket;
    }
  while (!VPID_ISNULL (&tmp_bucket_vpid));

  /* insert new record into dk_bucket */
  if (fhs_compose_record (thread_p, key_p, value_p, &bucket_recdes, FHS_FLAG_DUMMY_NUM) != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }
  success = spage_insert (thread_p, dk_bucket_page_p, &bucket_recdes, &tmp_slot);
  if (success == SP_DOESNT_FIT)
    {
      /* make new dk_bucket page */
      success =
	file_alloc (thread_p, &fhsid_p->bucket_file, fhs_initialize_dk_bucket_new_page, &null_vpid, &cur_bucket_vpid,
		    &new_dk_bucket_page_p);
      if (success != NO_ERROR || dk_bucket_page_p == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto error;
	}
      /* connect with prior page */
      dk_bucket_header_p->next_bucket = cur_bucket_vpid;
      pgbuf_set_dirty (thread_p, dk_bucket_page_p, DONT_FREE);

      /* insert record into new dk_bucket */
      success = spage_insert (thread_p, new_dk_bucket_page_p, &bucket_recdes, &tmp_slot);
      if (success != SP_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto error;
	}
      pgbuf_set_dirty (thread_p, new_dk_bucket_page_p, DONT_FREE);
    }
  else if (success != SP_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  if (dk_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, dk_bucket_page_p);
    }
  fhs_free_recdes (thread_p, &bucket_recdes);
  if (new_dk_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, new_dk_bucket_page_p);
    }

  return FHS_SUCCESSFUL_COMPLETION;

error:
  if (bucket_recdes.data)
    {
      fhs_free_recdes (thread_p, &bucket_recdes);
    }
  if (dk_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, dk_bucket_page_p);
    }
  if (new_dk_bucket_page_p)
    {
      pgbuf_unfix_and_init (thread_p, new_dk_bucket_page_p);
    }
  return FHS_ERROR_OCCURRED;
}

static PAGE_PTR
fhs_extend_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p,
		   PAGE_PTR bucket_page_p, void *key_p, FHS_HASH_KEY hash_key, int *out_new_bit_p, VPID * bucket_vpid)
{
  VPID sibling_vpid;
  PAGE_PTR sibling_page_p = NULL;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  int old_local_depth;
  int new_local_depth;

  sibling_page_p =
    fhs_split_bucket (thread_p, fhsid_p, bucket_page_p, key_p, &old_local_depth, &new_local_depth, &sibling_vpid);
  if (sibling_page_p == NULL)
    {
      return NULL;
    }

  /* Save the bit position of hash_key to be used later in deciding whether to insert the new key to original bucket or
   * to the new sibling bucket. */
  *out_new_bit_p = GETBIT (hash_key, new_local_depth);

  /* Check directory expansion condition */
  if (new_local_depth > fhsid_p->depth)
    {
      if (fhs_expand_directory (thread_p, fhsid_p, new_local_depth) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  return NULL;
	}
    }

  /* Connect the buckets */
  if ((new_local_depth - old_local_depth) > 1)
    {
      /* First, set all of them to NULL_PAGEID */
      if (fhs_connect_bucket (thread_p, fhsid_p, old_local_depth, hash_key, &null_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	  return NULL;
	}

      /* Then, connect the Bucket page */
      hash_key = CLEARBIT (hash_key, new_local_depth);
      if (fhs_connect_bucket (thread_p, fhsid_p, new_local_depth, hash_key, bucket_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, sibling_page_p);
	}
    }

  /* Finally, connect the Sibling bucket page */
  hash_key = SETBIT (hash_key, new_local_depth);
  if (fhs_connect_bucket (thread_p, fhsid_p, new_local_depth, hash_key, &sibling_vpid) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, sibling_page_p);
      return NULL;
    }

  return sibling_page_p;
}

/*
 * fhs_split_bucket () - Split a bucket into two
 *   return: PAGE_PTR (Sibling Pageptr), NULL in case of error in split.
 *   buc_pgptr(in): pointer to bucket page to split
 *   key(in): key value to insert after split
 *   old_local_depth(in): old local depth before the split operation; to be set
 *   new_local_depth(in): new local depth after the split operation; to be set
 *   sib_vpid(in): vpid of the sibling bucket; to be set
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
fhs_split_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p, void *key_p,
		  int *out_old_local_depth_p, int *out_new_local_depth_p, VPID * sibling_vpid_p)
{
  FHS_BUCKET_HEADER *bucket_header_p;
  VFID bucket_vfid;
  PAGE_PTR sibling_page_p;
  RECDES recdes;
  PGSLOTID first_slot_id = -1;
  int num_records;
  char init_bucket_data;

  int error_code = NO_ERROR;

  num_records = spage_number_of_records (bucket_page_p);
  /* Retrieve the bucket header and update it */
  if (spage_next_record (bucket_page_p, &first_slot_id, &recdes, PEEK) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
      return NULL;
    }
  bucket_header_p = (FHS_BUCKET_HEADER *) recdes.data;
  *out_old_local_depth_p = bucket_header_p->local_depth;

  if (fhs_find_first_bit_position (thread_p, fhsid_p, bucket_page_p, bucket_header_p, key_p, num_records,
				   first_slot_id, out_old_local_depth_p, out_new_local_depth_p) != NO_ERROR)
    {
      return NULL;
    }

  bucket_vfid = fhsid_p->bucket_file;
  init_bucket_data = *out_new_local_depth_p;

  error_code = file_alloc (thread_p, &bucket_vfid, fhs_initialize_bucket_new_page, &init_bucket_data, sibling_vpid_p,
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

  if (fhs_distribute_records_into_two_bucket (thread_p, fhsid_p, bucket_page_p, bucket_header_p, num_records,
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

  pgbuf_set_dirty (thread_p, sibling_page_p, DONT_FREE);
  pgbuf_set_dirty (thread_p, bucket_page_p, DONT_FREE);

  return sibling_page_p;
}

/*
 * fhs_expand_directory () - Expand directory
 *   return: NO_ERROR, or ER_FAILED
 *   fhsid(in): identifier for the extendible hashing structure to expand
 *   new_depth(in): requested new depth
 *
 * Note: This function expands the given extendible hashing directory
 * as many times as necessary to attain the requested depth.
 * This is performed with a single pass on the original
 * directory (starting from the last pointer and working through
 * the first one).
 * If the original directory is not large enough some new pages are
 * allocated for the new directory. If the disk becomes full
 * during this operation, or any system error occurs, the
 * directory expansion is canceled and an error code is
 * returned. For recovery purposes, the contents of the
 * directory pages are logged during the expansion operation.
 */
static int
fhs_expand_directory (THREAD_ENTRY * thread_p, FHSID * fhsid_p, int new_depth)
{
  int old_pages;
  int old_ptrs;
  int new_pages;
  int new_ptrs;
  int check_pages;
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

  error_code = file_get_num_user_pages (thread_p, &fhsid_p->ehid.vfid, &old_pages);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  old_pages -= 1;		/* The first page starts with 0 */
  old_ptrs = 1 << fhsid_p->depth;
  exp_times = 1 << (new_depth - fhsid_p->depth);

  new_ptrs = old_ptrs * exp_times;
  end_offset = old_ptrs - 1;	/* Dir first pointer has an offset of 0 */
  fhs_dir_locate (&check_pages, &end_offset);

  /* Find how many pages the expanded directory will occupy */
  new_end_offset = new_ptrs - 1;
  fhs_dir_locate (&new_pages, &new_end_offset);
  needed_pages = new_pages - old_pages;
  if (needed_pages > 0)
    {
      error_code =
	file_alloc_multiple (thread_p, &fhsid_p->ehid.vfid, fhs_initialize_dir_new_page, NULL, needed_pages, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  /*****************************
     Perform expansion
   ******************************/

  /* Initialize source variables */
  old_dir_nth_page = old_pages;	/* The last page of the old directory */

  old_dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, old_dir_nth_page, PGBUF_LATCH_WRITE);
  if (old_dir_page_p == NULL)
    {
      return ER_FAILED;
    }
  old_dir_offset = end_offset;

  /* Initialize destination variables */
  new_dir_nth_page = new_pages;	/* The last page of the new directory */

  new_dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, new_dir_nth_page, PGBUF_LATCH_WRITE);
  if (new_dir_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, old_dir_page_p);
      return ER_FAILED;
    }
  new_dir_offset = new_end_offset;

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

	  old_dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, old_dir_nth_page, PGBUF_LATCH_WRITE);
	  if (old_dir_page_p == NULL)
	    {
	      /* Fetch error; so return */
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
	      old_dir_offset = FHS_LAST_OFFSET_IN_NON_FIRST_PAGE;
	    }
	  else
	    {
	      /* This is the first (root) directory page */
	      old_dir_offset = FHS_LAST_OFFSET_IN_FIRST_PAGE;
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
	      pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

	      /* get another page */
	      new_dir_nth_page--;

	      new_dir_page_p = fhs_fix_nth_page (thread_p, &fhsid_p->ehid.vfid, new_dir_nth_page, PGBUF_LATCH_WRITE);
	      if (new_dir_page_p == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, old_dir_page_p);
		  return ER_FAILED;
		}

	      /* Set the newdir_offset to the offset of the last pointer in the current destination directory page. */
	      if (new_dir_nth_page != 0)
		{
		  /* This is not the first (root) dir page */
		  new_dir_offset = FHS_LAST_OFFSET_IN_NON_FIRST_PAGE;
		}
	      else
		{
		  /* This is the first (root) dir page */
		  new_dir_offset = FHS_LAST_OFFSET_IN_FIRST_PAGE;
		}
	    }

	  if ((char *) new_dir_page_p + new_dir_offset != (char *) old_dir_page_p + old_dir_offset)
	    {
	      memcpy ((char *) new_dir_page_p + new_dir_offset, (char *) old_dir_page_p + old_dir_offset,
		      sizeof (FHS_DIR_RECORD));
	    }

	  /* Advance the destination pointer to new spot */
	  new_dir_offset -= sizeof (FHS_DIR_RECORD);
	}

      /* Advance the source pointer to new spot */
      old_dir_offset -= sizeof (FHS_DIR_RECORD);
    }

  /*
   * Update the directory header
   */
  fhsid_p->depth = new_depth;

  /* Release the old and new directory pages */
  pgbuf_unfix_and_init (thread_p, old_dir_page_p);
  pgbuf_set_dirty (thread_p, new_dir_page_p, FREE);

  return NO_ERROR;
}

static int
fhs_find_first_bit_position (THREAD_ENTRY * thread_p, FHSID * fhsid_p, PAGE_PTR bucket_page_p,
			     FHS_BUCKET_HEADER * bucket_header_p, void *key_p, int num_recs, PGSLOTID first_slot_id,
			     int *out_old_local_depth_p, int *out_new_local_depth_p)
{
  int bit_position;
  unsigned int i;
  unsigned int difference = 0, check_bit = 0;
  FHS_HASH_KEY first_hash_key, next_hash_key;
  PGSLOTID slot_id;
  RECDES recdes;

  check_bit = 1 << (FHS_HASH_KEY_BITS - bucket_header_p->local_depth - 1);

  /* Get the first pseudo key */
  first_hash_key = *((FHS_HASH_KEY *) key_p);

  /* TO_DO : Is there any way to find the depth more effectively? */
  for (slot_id = first_slot_id + 1; slot_id < num_recs; slot_id++)
    {
      if (spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK) != S_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_EH_CORRUPTED, 0);
	  return ER_EH_CORRUPTED;
	}

      if (fhs_get_pseudo_key (thread_p, &recdes, &next_hash_key) != NO_ERROR)
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
  for (i = bucket_header_p->local_depth + 1; i <= FHS_HASH_KEY_BITS; i++)
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
fhs_distribute_records_into_two_bucket (THREAD_ENTRY * thread_p, FHSID * fhsid_p,
					PAGE_PTR bucket_page_p, FHS_BUCKET_HEADER * bucket_header_p, int num_recs,
					PGSLOTID first_slot_id, PAGE_PTR sibling_page_p)
{
  PGSLOTID slot_id, sibling_slot_id;
  RECDES recdes;
  FHS_HASH_KEY hash_key;
  int i, success;

  for (slot_id = i = first_slot_id + 1; i < num_recs; i++)
    {
      if (spage_get_record (thread_p, bucket_page_p, slot_id, &recdes, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      if (fhs_get_pseudo_key (thread_p, &recdes, &hash_key) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (GETBIT (hash_key, bucket_header_p->local_depth))
	{
	  success = spage_insert (thread_p, sibling_page_p, &recdes, &sibling_slot_id);
	  if (success != SP_SUCCESS)
	    {
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

static int
fhs_get_pseudo_key (THREAD_ENTRY * thread_p, RECDES * recdes_p, FHS_HASH_KEY * out_hash_key_p)
{
  FHS_HASH_KEY hash_key;
  char *bucket_record_p;

  bucket_record_p = (char *) recdes_p->data;
  bucket_record_p += sizeof (TFTID);
  hash_key = *((FHS_HASH_KEY *) bucket_record_p);

  *out_hash_key_p = hash_key;
  return NO_ERROR;
}
