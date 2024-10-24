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
 * external_sort.c - External sorting module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

#include "error_manager.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "external_sort.h"
#include "file_manager.h"
#include "page_buffer.h"
#include "log_manager.h"
#include "disk_manager.h"
#include "slotted_page.h"
#include "overflow_file.h"
#include "boot_sr.h"
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#include "server_support.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"	// for thread_get_thread_entry_info and thread_sleep
#include "list_file.h"
#include "query_manager.h"
#include "object_representation.h"

#include <functional>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Estimate on number of pages in the multipage temporary file */
#define SORT_MULTIPAGE_FILE_SIZE_ESTIMATE  20

/* Upper limit on the half of the total number of the temporary files.
 * The exact upper limit on total number of temp files is twice this number.
 * (i.e., this number specifies the upper limit on the number of total input
 * or total output files at each stage of the merging process.
 */
#define SORT_MAX_HALF_FILES      4

/* Lower limit on the half of the total number of the temporary files.
 * The exact lower limit on total number of temp files is twice this number.
 * (i.e., this number specifies the lower limit on the number of total input
 * or total output files at each stage of the merging process.
 */
#define SORT_MIN_HALF_FILES      2

#define SORT_MAX_PARALLEL      16

/* Initial size of the dynamic array that keeps the file contents list */
#define SORT_INITIAL_DYN_ARRAY_SIZE 30

/* Expansion Ratio of the dynamic array that keeps the file contents list */
#define SORT_EXPAND_DYN_ARRAY_RATIO 1.5

#define SORT_MAXREC_LENGTH             \
        ((ssize_t)(DB_PAGESIZE - sizeof(SLOTTED_PAGE_HEADER) - sizeof(SLOT)))

#define SORT_SWAP_PTR(a,b) { char **temp; temp = a; a = b; b = temp; }

#define SORT_CHECK_DUPLICATE(a, b)  \
    do {                          \
        if (cmp == 0) {           \
            if (option == SORT_DUP) \
                sort_append(a, b);  \
            *(a) = NULL;          \
            dup_num++;            \
        }                         \
    } while (0)

#define IS_PARALLEL_EXECUTION(t) ((t)->px_max_index > 1)

enum parallel_type
{
  PX_SINGLE = 0,
  PX_MAIN_IN_PARALLEL = 1,
  PX_THREAD_IN_PARALLEL
};
typedef enum parallel_type PARALLEL_TYPE;

typedef struct file_contents FILE_CONTENTS;
struct file_contents
{				/* node of the file_contents linked list */
  int *num_pages;		/* Dynamic array whose elements keep the sizes of the runs contained in the file in
				 * terms of number of slotted pages it occupies */
  int num_slots;		/* Total number of elements the array has */
  int first_run;		/* The index of the array element keeping the size of the first run of the file. */
  int last_run;			/* The index of the array element keeping the size of the last run of the file. */
};

typedef struct vol_info VOL_INFO;
struct vol_info
{				/* volume information */
  INT16 volid;			/* Volume identifier */
  int file_cnt;			/* Current number of files in the volume */
};

typedef struct vol_list VOL_LIST;
struct vol_list
{				/* temporary volume identifier list */
  INT16 volid;			/* main sorting disk volume */
  int vol_ent_cnt;		/* number of array entries */
  int vol_cnt;			/* number of temporary volumes */
  VOL_INFO *vol_info;		/* array of volume information */
};

typedef struct sort_param SORT_PARAM;
struct sort_param
{
  VFID temp[2 * SORT_MAX_HALF_FILES];	/* Temporary file identifiers */
  VFID multipage_file;		/* Temporary file for multi page sorting records */
  FILE_CONTENTS file_contents[2 * SORT_MAX_HALF_FILES];	/* Contents of each temporary file */

  bool tde_encrypted;		/* whether related temp files are encrypted (TDE) or not */

  VOL_LIST vol_list;		/* Temporary volume information list */
  char *internal_memory;	/* Internal_memory used for internal sorting phase and as input/output buffers for temp
				 * files during merging phase */
  int tot_runs;			/* Total number of runs */
  int tot_buffers;		/* Size of internal memory used in terms of number of buffers it occupies */
  int tot_tempfiles;		/* Total number of temporary files */
  int half_files;		/* Half number of temporary files */
  int in_half;			/* Which half of temp files is for input */

  void *out_arg;		/* Arguments to pass to the output function */

  /* Comparison function to use in the internal sorting and the merging phases */
  SORT_CMP_FUNC *cmp_fn;
  void *cmp_arg;
  SORT_DUP_OPTION option;

  /* input function to apply on temporary records */
  SORT_GET_FUNC *get_fn;
  void *get_arg;

  /* output function to apply on temporary records */
  SORT_PUT_FUNC *put_fn;
  void *put_arg;

  /* Estimated number of pages in each temp file (used in initialization) */
  int tmp_file_pgs;

  /* Details about the "limit" clause */
  int limit;

  /* total number of recordes */
  unsigned int total_numrecs;

  /* support parallelism */
#if defined(SERVER_MODE)
  /* pthread_mutex_t px_mtx;    /* px_node status mutex */
#endif
  int px_max_index;
  int px_index;
  bool px_status;
  int px_result_file_idx;
  int px_tran_index;
};

typedef struct sort_rec_list SORT_REC_LIST;
struct sort_rec_list
{
  struct sort_rec_list *next;	/* next sorted record item */
  int rec_pos;			/* record position */
  bool is_duplicated;		/* duplicated sort_key record flag */
};				/* Sort record list */

typedef struct slotted_pheader SLOTTED_PAGE_HEADER;
struct slotted_pheader
{
  INT16 nslots;			/* Number of allocated slots for the page */
  INT16 nrecs;			/* Number of records on page */
  INT16 anchor_flag;		/* Valid ANCHORED, ANCHORED_DONT_REUSE_SLOTS UNANCHORED_ANY_SEQUENCE,
				 * UNANCHORED_KEEP_SEQUENCE */
  INT16 alignment;		/* Alignment for records. */
  INT16 waste_align;		/* Number of bytes waste because of alignment */
  INT16 tfree;			/* Total free space on page */
  INT16 cfree;			/* Contiguous free space on page */
  INT16 foffset;		/* Byte offset from the beginning of the page to the first free byte area on the page. */
};

typedef struct slot SLOT;
struct slot
{
  INT16 roffset;		/* Byte Offset from the beginning of the page to the beginning of the record */
  INT16 rlength;		/* Length of record */
  INT16 rtype;			/* Record type described by slot. */
};

typedef struct run_struct RUN;
struct run_struct
{
  long start;
  long stop;
};

typedef struct srun SRUN;
struct srun
{
  char low_high;		/* location info LOW('L') : otherbase HIGH('H') : base */
  unsigned short tree_depth;	/* depth of this node : leaf is 1 */
  long start;
  long stop;
};

typedef struct sort_stack SORT_STACK;
struct sort_stack
{
  int top;
  SRUN *srun;
};

typedef void FIND_RUN_FN (char **, long *, SORT_STACK *, long, SORT_CMP_FUNC *, void *);
typedef void MERGE_RUN_FN (char **, char **, SORT_STACK *, SORT_CMP_FUNC *, void *);

#if !defined(NDEBUG)
static int sort_validate (char **vector, long size, SORT_CMP_FUNC * compare, void *comp_arg);
#endif

#if defined(SERVER_MODE)
/* start parallel sort */
static void sort_listfile_execute (cubthread::entry & thread_ref, SORT_PARAM * sort_param);
static int sort_copy_sort_param (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param,
				 int parallel_num);
static int sort_split_input_temp_file (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param,
				       int parallel_num);
static int sort_merge_run_for_parallel (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param,
					int parallel_num);
#endif
/* end parallel sort */

static int sort_listfile_internal (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
static int sort_inphase_sort (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, SORT_GET_FUNC * get_next,
			      void *arguments, unsigned int *total_numrecs);
static int sort_exphase_merge_elim_dup (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
static int sort_exphase_merge (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
static int sort_put_result_from_tmpfile (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
static int sort_get_avg_numpages_of_nonempty_tmpfile (SORT_PARAM * sort_param);
static void sort_return_used_resources (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, PARALLEL_TYPE parallel_type);
static int sort_add_new_file (THREAD_ENTRY * thread_p, VFID * vfid, int file_pg_cnt_est, bool force_alloc,
			      bool tde_encrypted);

static int sort_write_area (THREAD_ENTRY * thread_p, VFID * vfid, int first_page, INT32 num_pages, char *area_start,
			    bool tde_encrypted);
static int sort_read_area (THREAD_ENTRY * thread_p, VFID * vfid, int first_page, INT32 num_pages, char *area_start);

static int sort_get_num_half_tmpfiles (int tot_buffers, int input_pages);
static int sort_checkalloc_numpages_of_outfiles (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
static int sort_get_numpages_of_active_infiles (const SORT_PARAM * sort_param);
static int sort_find_inbuf_size (int tot_buffers, int in_sections);
static char *sort_retrieve_longrec (THREAD_ENTRY * thread_p, RECDES * address, RECDES * memory);
static char **sort_run_sort (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, char **base, long limit,
			     long sort_numrecs, char **otherbase, long *srun_limit);
static int sort_run_add_new (FILE_CONTENTS * file_contents, int num_pages);
static void sort_run_remove_first (FILE_CONTENTS * file_contents);
static void sort_run_flip (char **start, char **stop);
static void sort_run_find (char **source, long *top, SORT_STACK * st_p, long limit, SORT_CMP_FUNC * compare,
			   void *comp_arg, SORT_DUP_OPTION option);
static void sort_run_merge (char **low, char **high, SORT_STACK * st_p, SORT_CMP_FUNC * compare, void *comp_arg,
			    SORT_DUP_OPTION option);
static int sort_run_flush (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, int out_curfile, int *cur_page,
			   char *output_buffer, char **index_area, int numrecs, int rec_type);

static int sort_get_num_file_contents (FILE_CONTENTS * file_contents);
#if defined(CUBRID_DEBUG)
static void sort_print_file_contents (const FILE_CONTENTS * file_contents);
#endif /* CUBRID_DEBUG */

static void sort_spage_initialize (PAGE_PTR pgptr, INT16 slots_type, INT16 alignment);

static INT16 sort_spage_get_numrecs (PAGE_PTR pgptr);
static INT16 sort_spage_insert (PAGE_PTR pgptr, RECDES * recdes);
static SCAN_CODE sort_spage_get_record (PAGE_PTR pgptr, INT16 slotid, RECDES * recdes, bool peek_p);
static int sort_spage_offsetcmp (const void *s1, const void *s2);
static int sort_spage_compact (PAGE_PTR pgptr);
static INT16 sort_spage_find_free (PAGE_PTR pgptr, SLOT ** sptr, INT16 length, INT16 type, INT16 * space);
#if defined(CUBRID_DEBUG)
static INT16 sort_spage_dump_sptr (SLOT * sptr, INT16 nslots, INT16 alignment);
static void sort_spage_dump_hdr (SLOTTED_PAGE_HEADER * sphdr);
static void sort_spage_dump (PAGE_PTR pgptr, int rec_p);
#endif /* CUBRID_DEBUG */

static void sort_append (const void *pk0, const void *pk1);

/*
 * sort_spage_initialize () - Initialize a slotted page
 *   return: void
 *   pgptr(in): Pointer to slotted page
 *   slots_type(in): Flag which indicates the type of slots
 *   alignment(in): Type of alignment
 *
 * Note: A slotted page must be initialized before records are inserted on the
 *       page. The alignment indicates the valid offset where the records
 *       should be stored. This is a requirment for peeking records on pages
 *       according to their alignmnet restrictions.
 */
static void
sort_spage_initialize (PAGE_PTR pgptr, INT16 slots_type, INT16 alignment)
{
  SLOTTED_PAGE_HEADER *sphdr;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  sphdr->nslots = 0;
  sphdr->nrecs = 0;

#if defined(CUBRID_DEBUG)
  if (!spage_is_valid_anchor_type (slots_type))
    {
      (void) fprintf (stderr,
		      "sort_spage_initialize: **INTERFACE SYSTEM ERROR BAD value for slots_type = %d.\n"
		      " UNANCHORED_KEEP_SEQUENCE was assumed **\n", slots_type);
      slots_type = UNANCHORED_KEEP_SEQUENCE;
    }
  if (!(alignment == CHAR_ALIGNMENT || alignment == SHORT_ALIGNMENT || alignment == INT_ALIGNMENT
	|| alignment == LONG_ALIGNMENT || alignment == FLOAT_ALIGNMENT || alignment == DOUBLE_ALIGNMENT))
    {
      (void) fprintf (stderr,
		      "sort_spage_initialize: **INTERFACE SYSTEM ERROR BAD value = %d"
		      " for alignment. %d was assumed\n. Alignment must be"
		      " either SIZEOF char, short, int, long, float, or double", alignment, MAX_ALIGNMENT);
      alignment = MAX_ALIGNMENT;
    }
#endif /* CUBRID_DEBUG */

  sphdr->anchor_flag = slots_type;
  sphdr->tfree = DB_PAGESIZE - sizeof (SLOTTED_PAGE_HEADER);
  sphdr->cfree = sphdr->tfree;
  sphdr->foffset = sizeof (SLOTTED_PAGE_HEADER);
  sphdr->alignment = alignment;
  sphdr->waste_align = DB_WASTED_ALIGN (sphdr->foffset, alignment);

  if (sphdr->waste_align != 0)
    {
      sphdr->foffset += sphdr->waste_align;
      sphdr->cfree -= sphdr->waste_align;
      sphdr->tfree -= sphdr->waste_align;
    }
}

/*
 * sort_spage_get_numrecs () - Return the total number of records on the slotted page
 *   return:
 *   pgptr(in): Pointer to slotted page
 */
static INT16
sort_spage_get_numrecs (PAGE_PTR pgptr)
{
  SLOTTED_PAGE_HEADER *sphdr;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  return sphdr->nrecs;
}

/*
 * sort_spage_offsetcmp () - Compare the location (offset) of slots
 *   return: sp1 - sp2
 *   sp1(in): slot 1
 *   sp2(in): slot 2
 */
static int
sort_spage_offsetcmp (const void *sp1, const void *sp2)
{
  SLOT *s1, *s2;
  INT16 l1, l2;

  s1 = *(SLOT **) sp1;
  s2 = *(SLOT **) sp2;

  l1 = s1->roffset;
  l2 = s2->roffset;

  return (l1 < l2) ? -1 : (l1 == l2) ? 0 : 1;
}

/*
 * sort_spage_compact () - Compact an slotted page
 *   return: NO_ERROR
 *   pgptr(in): Pointer to slotted page
 *
 * Note: Only the records are compacted, the slots are not compacted.
 */
static int
sort_spage_compact (PAGE_PTR pgptr)
{
  int i, j;
  SLOTTED_PAGE_HEADER *sphdr;
  SLOT *sptr;
  SLOT **sortptr;
  INT16 to_offset;
  int ret = NO_ERROR;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  sortptr = (SLOT **) calloc ((unsigned) sphdr->nrecs, sizeof (SLOT *));
  if (sortptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sphdr->nrecs * sizeof (SLOT *));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Populate the array to sort and then sort it */
  sptr = (SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (SLOT));
  for (j = 0, i = 0; i < sphdr->nslots; sptr--, i++)
    {
      if (sptr->roffset != NULL_OFFSET)
	{
	  sortptr[j++] = sptr;
	}
    }

  qsort ((void *) sortptr, (size_t) sphdr->nrecs, (size_t) sizeof (SLOT *), sort_spage_offsetcmp);

  to_offset = sizeof (SLOTTED_PAGE_HEADER);
  for (i = 0; i < sphdr->nrecs; i++)
    {
      /* Make sure that the offset is aligned */
      to_offset = DB_ALIGN (to_offset, sphdr->alignment);
      if (to_offset == sortptr[i]->roffset)
	{
	  /* Record slot is already in place */
	  to_offset += sortptr[i]->rlength;
	}
      else
	{
	  /* Move the record */
	  memmove ((char *) pgptr + to_offset, (char *) pgptr + sortptr[i]->roffset, sortptr[i]->rlength);
	  sortptr[i]->roffset = to_offset;
	  to_offset += sortptr[i]->rlength;
	}
    }

  /* Make sure that the next inserted record will be aligned */
  to_offset = DB_ALIGN (to_offset, sphdr->alignment);

  sphdr->cfree = sphdr->tfree = (DB_PAGESIZE - to_offset - sphdr->nslots * sizeof (SLOT));
  sphdr->foffset = to_offset;
  free_and_init (sortptr);

  /* The page is set dirty somewhere else */

  return ret;
}

/*
 * sort_spage_find_free () - Find a free area/slot where a record of the given length
 *                  can be inserted onto the given slotted page
 *   return: A slot identifier or NULL_SLOTID
 *   pgptr(in): Pointer to slotted page
 *   sptr(out): Pointer to slotted page array pointer
 *   length(in): Length of area/record
 *   type(in): Type of record to be inserted
 *   space(out): Space used/defined
 *
 * Note: If there is not enough space on the page, an error condition is
 *       indicated and NULLSLOTID is returned.
 */
static INT16
sort_spage_find_free (PAGE_PTR pgptr, SLOT ** sptr, INT16 length, INT16 type, INT16 * space)
{
  SLOTTED_PAGE_HEADER *sphdr;
  INT16 slotid;
  INT16 waste;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  /* Calculate the wasting space that this record will introduce. We need to take in consideration the wasting space
   * when there is space saved */
  waste = DB_WASTED_ALIGN (length, sphdr->alignment);
  *space = length + waste;

  /* Quickly check for available space. We may need to check again if a slot is created (instead of reused) */
  if (*space > sphdr->tfree)
    {
      *sptr = NULL;
      *space = 0;
      return NULL_SLOTID;
    }

  /* Find a free slot. Try to reuse an unused slotid, instead of allocating a new one */

  *sptr = (SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (SLOT));

  if (sphdr->nslots == sphdr->nrecs)
    {
      slotid = sphdr->nslots;
      *sptr -= slotid;
    }
  else
    {
      for (slotid = 0; slotid < sphdr->nslots; (*sptr)--, slotid++)
	{
	  if ((*sptr)->rtype == REC_HOME || (*sptr)->rtype == REC_BIGONE)
	    {
	      ;			/* go ahead */
	    }
	  else
	    {			/* impossible case */
	      assert (false);
	      break;
	    }
	}
    }

  /* Make sure that there is enough space for the record and the slot */

  assert (slotid <= sphdr->nslots);

  if (slotid >= sphdr->nslots)
    {
      *space += sizeof (SLOT);
      /* We are allocating a new slotid. Check for available space again */
      if (*space > sphdr->tfree)
	{
	  *sptr = NULL;
	  *space = 0;
	  return NULL_SLOTID;
	}
      else if (*space > sphdr->cfree && sort_spage_compact (pgptr) != NO_ERROR)
	{
	  *sptr = NULL;
	  *space = 0;
	  return NULL_SLOTID;
	}
      /* Adjust the number of slots */
      sphdr->nslots++;
    }
  else
    {
      /* We already know that there is total space available since the slot is reused and the space was check above */
      if (*space > sphdr->cfree && sort_spage_compact (pgptr) != NO_ERROR)
	{
	  *sptr = NULL;
	  *space = 0;
	  return NULL_SLOTID;
	}
    }

  /* Now separate an empty area for the record */
  (*sptr)->roffset = sphdr->foffset;
  (*sptr)->rlength = length;
  (*sptr)->rtype = type;

  /* Adjust the header */
  sphdr->nrecs++;
  sphdr->tfree -= *space;
  sphdr->cfree -= *space;
  sphdr->foffset += length + waste;
  sphdr->waste_align += waste;

  /* The page is set dirty somewhere else */

  return slotid;
}

/*
 * sort_spage_insert () - Insert a record onto the given slotted page
 *   return: A slot identifier
 *   pgptr(in): Pointer to slotted page
 *   recdes(in): Pointer to a record descriptor
 *
 * Note: If the record does not fit on the page, an error condition is
 *       indicated and NULL_SLOTID is returned.
 */
static INT16
sort_spage_insert (PAGE_PTR pgptr, RECDES * recdes)
{
  SLOTTED_PAGE_HEADER *sphdr;
  SLOT *sptr;
  INT16 slotid;
  INT16 used_space;

  if (recdes->length > SORT_MAXREC_LENGTH)
    {
      return NULL_SLOTID;
    }

  assert (recdes->type == REC_HOME || recdes->type == REC_BIGONE);

  slotid = sort_spage_find_free (pgptr, &sptr, recdes->length, recdes->type, &used_space);
  if (slotid != NULL_SLOTID)
    {
      /* Find the free slot and insert the record */
      memcpy (((char *) pgptr + sptr->roffset), recdes->data, recdes->length);

      /* Indicate that we are spending our savings */
      sphdr = (SLOTTED_PAGE_HEADER *) pgptr;
    }

  return slotid;
}

/*
 * sort_spage_get_record () - Get specific record
 *   return: S_SUCCESS, S_DOESNT_FIT, S_DOESNT_EXIST
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of current record
 *   recdes(in): Pointer to a record descriptor
 *   peek_p(in): Indicates whether the record is going to be copied or peeked
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
 *       When ispeeking is COPY, the desired available record is
 *       read onto the area pointed by the record descriptor. If the record
 *       does not fit in such an area, the length of the record is returned
 *       as a negative value in recdes->length and an error is indicated in the
 *       return value.
 */
static SCAN_CODE
sort_spage_get_record (PAGE_PTR pgptr, INT16 slotid, RECDES * recdes, bool peek_p)
{
  SLOTTED_PAGE_HEADER *sphdr;
  SLOT *sptr;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  sptr = (SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (SLOT));

  sptr -= slotid;

  if (slotid < 0 || slotid >= sphdr->nslots || sptr->roffset == NULL_OFFSET)
    {
      recdes->length = 0;
      return S_DOESNT_EXIST;
    }

  /*
   * If peeking, the address of the data in the descriptor is set to the
   * address of the record in the buffer. Otherwise, the record is copied
   * onto the area specified by the descriptor
   */
  if (peek_p == PEEK)
    {
      recdes->area_size = -1;
      recdes->data = (char *) pgptr + sptr->roffset;
    }
  else
    {
      /* copy the record */

      if (sptr->rlength > recdes->area_size)
	{
	  /*
	   * DOES NOT FIT
	   * Give a hint to the user of the needed length. Hint is given as a
	   * negative value
	   */
	  recdes->length = -sptr->rlength;
	  return S_DOESNT_FIT;
	}

      memcpy (recdes->data, (char *) pgptr + sptr->roffset, sptr->rlength);
    }

  recdes->length = sptr->rlength;
  recdes->type = sptr->rtype;

  return S_SUCCESS;
}

#if defined(CUBRID_DEBUG)
/*
 * sort_spage_dump_sptr () - Dump the slotted page array
 *   return: Total length of records
 *   sptr(in): Pointer to slotted page pointer array
 *   nslots(in): Number of slots
 *   alignment(in): Alignment for records
 *
 * Note: The content of the record is not dumped by this function.
 *       This function is used for debugging purposes.
 */
static INT16
sort_spage_dump_sptr (SLOT * sptr, INT16 nslots, INT16 alignment)
{
  int i;
  INT16 waste;
  INT16 total_length_records = 0;

  for (i = 0; i < nslots; sptr--, i++)
    {
      assert (sptr->rtype == REC_HOME || sptr->rtype == REC_BIGONE);
      (void) fprintf (stdout, "\nSlot-id = %2d, offset = %4d, type = %s", i, sptr->roffset,
		      ((sptr->rtype == REC_HOME) ? "HOME" : (sptr->rtype ==
							     REC_BIGONE) ? "BIGONE" : "UNKNOWN-BY-CURRENT-MODULE"));

      if (sptr->roffset != NULL_OFFSET)
	{
	  total_length_records += sptr->rlength;
	  waste = DB_WASTED_ALIGN (sptr->rlength, alignment);
	  (void) fprintf (stdout, ", length = %4d, waste = %d", sptr->rlength, waste);
	}
      (void) fprintf (stdout, "\n");
    }

  return total_length_records;
}

/*
 * sort_spage_dump_hdr () - Dump an slotted page header
 *   return: void
 *   sphdr(in): Pointer to header of slotted page
 *
 * Note:  This function is used for debugging purposes.
 */
static void
sort_spage_dump_hdr (SLOTTED_PAGE_HEADER * sphdr)
{
  (void) fprintf (stdout, "NUM SLOTS = %d, NUM RECS = %d, TYPE OF SLOTS = %s,\n", sphdr->nslots, sphdr->nrecs,
		  spage_anchor_flag_string (sphdr->anchor_flag));

  (void) fprintf (stdout, "ALIGNMENT-TO = %s, WASTED AREA FOR ALIGNMENT = %d,\n",
		  spage_alignment_string (sphdr->alignment), spage_waste_align);

  (void) fprintf (stdout, "TOTAL FREE AREA = %d, CONTIGUOUS FREE AREA = %d, FREE SPACE OFFSET = %d,\n", sphdr->tfree,
		  sphdr->cfree, sphdr->foffset);
}

/*
 * sort_spage_dump () - Dump an slotted page
 *   return: void
 *   pgptr(in): Pointer to slotted page
 *   rec_p(in): If true, records are printed in ascii format, otherwise, the
 *              records are not printed
 *
 * Note: The records are printed only when the value of rec_p is true.
 *       This function is used for debugging purposes.
 */
static void
sort_spage_dump (PAGE_PTR pgptr, int rec_p)
{
  int i, j;
  SLOTTED_PAGE_HEADER *sphdr;
  SLOT *sptr;
  char *rec;
  int used_length = 0;

  sphdr = (SLOTTED_PAGE_HEADER *) pgptr;

  sort_spage_dump_hdr (sphdr);

  sptr = (SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (SLOT));

  used_length = (sizeof (SLOTTED_PAGE_HEADER) + sphdr->waste_align + sizeof (SLOT) * sphdr->nslots);
  used_length += sort_spage_dump_sptr (sptr, sphdr->nslots, sphdr->alignment);

  if (rec_p)
    {
      (void) fprintf (stdout, "\nRecords in ascii follow ...\n");
      for (i = 0; i < sphdr->nslots; sptr--, i++)
	{
	  if (sptr->roffset != NULL_OFFSET)
	    {
	      (void) fprintf (stdout, "\nSlot-id = %2d\n", i);
	      rec = (char *) pgptr + sptr->roffset;

	      for (j = 0; j < sptr->rlength; j++)
		{
		  (void) fputc (*rec++, stdout);
		}

	      (void) fprintf (stdout, "\n");
	    }
	  else
	    {
	      (void) fprintf (stdout, "\nSlot-id = %2d has been deleted\n", i);
	    }
	}
    }

  if (used_length + sphdr->tfree > DB_PAGESIZE)
    {
      (void) fprintf (stdout,
		      "sort_spage_dump: Inconsistent page \n (Used_space + tfree > DB_PAGESIZE\n (%d + %d) > %d \n "
		      " %d > %d\n", used_length, sphdr->tfree, DB_PAGESIZE, used_length + sphdr->tfree, DB_PAGESIZE);
    }

  if ((sphdr->cfree + sphdr->foffset + sizeof (SLOT) * sphdr->nslots) > DB_PAGESIZE)
    {
      (void) fprintf (stdout,
		      "sort_spage_dump: Inconsistent page\n (cfree + foffset + SIZEOF(SLOT) * nslots) > "
		      " DB_PAGESIZE\n (%d + %d + (%d * %d)) > %d\n %d > %d\n", sphdr->cfree, sphdr->foffset,
		      sizeof (SLOT), sphdr->nslots, DB_PAGESIZE,
		      (sphdr->cfree + sphdr->foffset + sizeof (SLOT) * sphdr->nslots), DB_PAGESIZE);
    }

  if (sphdr->cfree <= -(sphdr->alignment - 1))
    {
      (void) fprintf (stdout, "sort_spage_dump: Cfree %d is inconsistent in page. Cannot be < -%d\n", sphdr->cfree,
		      sphdr->alignment);
    }
}
#endif /* CUBRID_DEBUG */

/* Sorting module functions */

/*
 * sort_run_flip () - Flip a run in place
 *   return: void
 *   start(in):
 *   stop(in):
 *
 * Note: Odd runs will have middle pointer undisturbed.
 */
static void
sort_run_flip (char **start, char **stop)
{
  char *temp;

  while (start < stop)
    {
      temp = *start;
      *start = *stop;
      *stop = temp;
      start++;
      stop--;
    }

  return;
}

/*
 * sort_append () -
 *   return: void
 *   pk0(in):
 *   pk1(in):
 */
static void
sort_append (const void *pk0, const void *pk1)
{
  SORT_REC *node, *list;

  node = *(SORT_REC **) pk0;
  list = *(SORT_REC **) pk1;

  while (list->next)
    {
      list = list->next;
    }
  list->next = node;

  return;
}

/*
 * sort_run_find () - Finds the longest ascending or descending run it can
 *   return:
 *   source(in):
 *   top(in):
 *   st_p(in):
 *   limit(in):
 *   compare(in):
 *   comp_arg(in):
 *   option(in):
 *
 * Note: Flip descending run, and assign RUN start and stop
 */
static void
sort_run_find (char **source, long *top, SORT_STACK * st_p, long limit, SORT_CMP_FUNC * compare, void *comp_arg,
	       SORT_DUP_OPTION option)
{
  char **start;
  char **stop;
  char **next_stop;
  char **limit_p;
  SRUN *srun_p;

  char **dup;
  char **non_dup;
  int dup_num;
  int cmp;
  bool increasing_order;

  /* init new SRUN */
  st_p->top++;
  srun_p = &(st_p->srun[st_p->top]);
  srun_p->tree_depth = 1;
  srun_p->low_high = 'H';
  srun_p->start = *top;

  if (*top >= (limit - 1))
    {
      /* degenerate run length 1. Must go ahead and compare with length 2, because we may need to flip them */
      srun_p->stop = limit - 1;
      *top = limit;

      return;
    }

  start = &source[*top];
  stop = start + 1;
  next_stop = stop + 1;
  limit_p = &source[limit];

  dup_num = 0;

  /* have a non-trivial run of length 2 or more */
  cmp = (*compare) (start, stop, comp_arg);
  if (cmp > 0)
    {
      increasing_order = false;	/* mark as non-increasing order run */

      while (next_stop < limit_p && ((cmp = (*compare) (stop, next_stop, comp_arg)) >= 0))
	{
	  /* mark duplicate as NULL */
	  SORT_CHECK_DUPLICATE (stop, next_stop);	/* increase dup_num */

	  stop = next_stop;
	  next_stop = next_stop + 1;
	}
    }
  else
    {
      increasing_order = true;	/* mark as increasing order run */

      /* mark duplicate as NULL */
      SORT_CHECK_DUPLICATE (start, stop);	/* increase dup_num */

      /* build increasing order run */
      while (next_stop < limit_p && ((cmp = (*compare) (stop, next_stop, comp_arg)) <= 0))
	{
	  /* mark duplicate as NULL */
	  SORT_CHECK_DUPLICATE (stop, next_stop);	/* increase dup_num */

	  stop = next_stop;
	  next_stop = next_stop + 1;
	}
    }

  /* eliminate duplicates; right-shift slots */
  if (dup_num)
    {
      dup = stop - 1;
      for (non_dup = dup - 1; non_dup >= start; dup--, non_dup--)
	{
	  /* find duplicated value slot */
	  if (*dup == NULL)
	    {
	      /* find previous non-duplicated value slot */
	      for (; non_dup >= start; non_dup--)
		{
		  /* move non-duplicated value slot to duplicated value slot */
		  if (*non_dup != NULL)
		    {
		      *dup = *non_dup;
		      *non_dup = NULL;
		      break;
		    }
		}
	    }
	}
    }

  /* change non-increasing order run to increasing order run */
  if (increasing_order != true)
    {
      sort_run_flip (start + dup_num, stop);
    }

  *top += CAST_BUFLEN (stop - start);	/* advance to last visited */
  srun_p->start += dup_num;
  srun_p->stop = *top;

  (*top)++;			/* advance to next unvisited element */

  return;
}

/*
 * sort_run_merge () - Merges two runs from source to dest, updateing dest_top
 *   return:
 *   low(in):
 *   high(in):
 *   st_p(in):
 *   compare(in):
 *   comp_arg(in):
 *   option(in):
 */
static void
sort_run_merge (char **low, char **high, SORT_STACK * st_p, SORT_CMP_FUNC * compare, void *comp_arg,
		SORT_DUP_OPTION option)
{
  char dest_low_high;
  char **left_start, **right_start;
  char **left_stop, **right_stop;
  char **dest_ptr;
  SRUN *left_srun_p, *right_srun_p;
  int cmp;
  int dup_num;

  do
    {
      /* STEP 1: initialize */
      left_srun_p = &(st_p->srun[st_p->top - 1]);
      right_srun_p = &(st_p->srun[st_p->top]);

      left_srun_p->tree_depth++;

      if (left_srun_p->low_high == 'L')
	{
	  left_start = &low[left_srun_p->start];
	  left_stop = &low[left_srun_p->stop];
	}
      else
	{
	  left_start = &high[left_srun_p->start];
	  left_stop = &high[left_srun_p->stop];
	}

      if (right_srun_p->low_high == 'L')
	{
	  right_start = &low[right_srun_p->start];
	  right_stop = &low[right_srun_p->stop];
	}
      else
	{
	  right_start = &high[right_srun_p->start];
	  right_stop = &high[right_srun_p->stop];
	}

      dup_num = 0;

      /* STEP 2: check CON conditions srun follows ascending order. if (left_max < right_min) do FORWARD-CON. we use
       * '<' instead of '<=' */
      cmp = (*compare) (left_stop, right_start, comp_arg);
      if (cmp < 0)
	{
	  /* con == TRUE */
	  dest_low_high = right_srun_p->low_high;

	  if (left_srun_p->low_high == right_srun_p->low_high && left_srun_p->stop + 1 == right_srun_p->start)
	    {
	      ;
	    }
	  else
	    {
	      /* move LEFT to RIGHT's current PART */
	      if (right_srun_p->low_high == 'L')
		{
		  dest_ptr = &low[right_srun_p->start - 1];
		}
	      else
		{
		  dest_ptr = &high[right_srun_p->start - 1];
		}

	      while (left_stop >= left_start)
		{
		  /* copy LEFT */
		  *dest_ptr-- = *left_stop--;
		}
	    }
	}
      else
	{
	  /* con == FALSE do the actual merge; right-shift merge slots */
	  if (right_srun_p->low_high == 'L')
	    {
	      dest_low_high = 'H';
	      dest_ptr = &high[right_srun_p->stop];
	    }
	  else
	    {
	      dest_low_high = 'L';
	      dest_ptr = &low[right_srun_p->stop];
	    }

	  while (left_stop >= left_start && right_stop >= right_start)
	    {
	      cmp = (*compare) (left_stop, right_stop, comp_arg);
	      if (cmp == 0)
		{
		  /* eliminate duplicate */
		  if (option == SORT_DUP)
		    {
		      sort_append (left_stop, right_stop);
		    }
		  dup_num++;

		  *dest_ptr-- = *right_stop--;
		  left_stop--;
		}
	      else if (cmp > 0)
		{
		  *dest_ptr-- = *left_stop--;
		}
	      else
		{
		  *dest_ptr-- = *right_stop--;
		}
	    }

	  while (left_stop >= left_start)
	    {
	      /* copy the rest of LEFT */
	      *dest_ptr-- = *left_stop--;
	    }
	  while (right_stop >= right_start)
	    {
	      /* copy the rest of RIGHT */
	      *dest_ptr-- = *right_stop--;
	    }
	}

      /* STEP 3: reconfig SORT_STACK */
      st_p->top--;
      left_srun_p->low_high = dest_low_high;
      left_srun_p->start = right_srun_p->start - (left_srun_p->stop - left_srun_p->start + 1) + dup_num;
      left_srun_p->stop = right_srun_p->stop;

    }
  while ((st_p->top >= 1)	/* may need to merge */
	 && (st_p->srun[st_p->top - 1].tree_depth == st_p->srun[st_p->top].tree_depth));

  return;
}

/*
 * sort_run_sort () - An implementation of a run-sort algorithm
 *   return: pointer to the sorted area
 *   thread_p(in):
 *   sort_param(in): sort parameters
 *   base(in): pointer to the element at the base of the table
 *   limit(in): numrecs of before current sort
 *   sort_numrecs(in): numrecs of after privious sort
 *   otherbase(in): pointer to alternate area suffecient to store base-limit
 *   srun_limit(in): numrecs of after current sort
 *
 * Note: This sorts files by successive merging of runs.
 *
 *       This has the advantage of being liner on sorted or reversed data,
 *       and being order N log base k N, where k is the average length of a
 *       run. Note that k must be at least 2, so the worst case is N log2 N.
 *
 *       Overall, since long runs are common, this beats the pants off
 *       quick-sort.
 *
 *       This could be sped up a bit by looking for N runs, and sorting these
 *       into lists of concatennatable runs.
 */
static char **
sort_run_sort (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, char **base, long limit, long sort_numrecs,
	       char **otherbase, long *srun_limit)
{
  SORT_CMP_FUNC *compare;
  void *comp_arg;
  SORT_DUP_OPTION option;
  char **src, **dest, **result;
  SORT_STACK sr_stack, *st_p;
  long src_top = 0;
  int cnt;

  assert_release (limit == *srun_limit);

  /* exclude already sorted items */
  limit -= sort_numrecs;

  if (limit == 0 || (limit == 1 && sort_numrecs == 0))
    {
      return base;
    }

  /* init */
  compare = sort_param->cmp_fn;
  comp_arg = sort_param->cmp_arg;
  option = sort_param->option;

  src = base;
  dest = otherbase;
  result = NULL;

  st_p = &sr_stack;
  st_p->top = -1;

  cnt = (int) (log10 (ceil ((double) limit / 2.0)) / log10 (2.0)) + 2;
  if (sort_numrecs)
    {
      /* reserve space for already found srun */
      cnt += 1;
    }

  st_p->srun = (SRUN *) db_private_alloc (NULL, cnt * sizeof (SRUN));
  if (st_p->srun == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, cnt * sizeof (SRUN));
      return NULL;
    }

  do
    {
      sort_run_find (src, &src_top, st_p, limit, compare, comp_arg, option);
      if (src_top < limit)
	{
	  sort_run_find (src, &src_top, st_p, limit, compare, comp_arg, option);
	}

      while ((st_p->top >= 1)	/* may need to merge */
	     && ((src_top >= limit)	/* case 1: final merge stage */
		 || ((src_top < limit)	/* case 2: non-final merge stage */
		     && (st_p->srun[st_p->top - 1].tree_depth == st_p->srun[st_p->top].tree_depth))))
	{
	  sort_run_merge (dest, src, st_p, compare, comp_arg, option);
	}
    }
  while (src_top < limit);

  if (sort_numrecs > 0)
    {
      /* finally, merge with already found srun */
      SRUN *srun_p;

      srun_p = &(st_p->srun[++(st_p->top)]);
      srun_p->tree_depth = 1;
      srun_p->low_high = 'H';
      srun_p->start = limit;
      srun_p->stop = limit + sort_numrecs - 1;

      sort_run_merge (dest, src, st_p, compare, comp_arg, option);
    }

#if defined(CUBRID_DEBUG)
  if (limit != src_top)
    {
      printf ("Inconsistent sort test d), %ld %ld\n", limit, src_top);
    }
#endif /* CUBRID_DEBUG */

  /* include already sorted items */
  limit += sort_numrecs;

  /* save limit of non-duplicated value slot */
  *srun_limit = limit - st_p->srun[0].start;

  /* move base pointer */
  result = base + st_p->srun[0].start;

  if (st_p->srun[0].low_high == 'L')
    {
      if (option == SORT_ELIM_DUP)
	{
	  memcpy (result, otherbase + st_p->srun[0].start, (*srun_limit) * sizeof (char *));
	}
      else
	{
	  result = otherbase + st_p->srun[0].start;
	}
    }

  assert (result != NULL);

#if defined(CUBRID_DEBUG)
  src_top = 0;
  src = result;
  sort_run_find (src, &src_top, st_p, *srun_limit, compare, comp_arg, option);
  if (st_p->srun[st_p->top].stop != *srun_limit - 1)
    {
      printf ("Inconsistent sort, %ld %ld %ld\n", st_p->srun[st_p->top].start, st_p->srun[st_p->top].stop, *srun_limit);
      result = NULL;
    }
#endif /* CUBRID_DEBUG */

  db_private_free_and_init (NULL, st_p->srun);

#if !defined(NDEBUG)
  if (sort_validate (result, *srun_limit, compare, comp_arg) != NO_ERROR)
    {
      result = NULL;
    }
#endif

  return result;
}

/*
 * sort_listfile () - Perform sorting
 *   return:
 *   volid(in):  volume to keep the temporary files
 *   est_inp_pg_cnt(in): estimated number of input pages, or -1
 *   get_fn(in): user-supplied function: provides the next sort item (or,
 *               temporary record) every time it is called. This function
 *               should put the next sort item to the area pointed by the
 *               record descriptor "temp_recdes" and should return SORT_SUCCESS
 *               to acknowledge that everything is fine. However, if there is
 *               a problem it should return a corresponding code.
 *               For example, if the sort item does not fit into the
 *               "temp_recdes" area it should return SORT_REC_DOESNT_FIT; and
 *               if there are no more sort items then it should return
 *               SORT_NOMORE_RECS. In case of an error, it should return
 *               SORT_ERROR_OCCURRED,in addition to setting the corresponding
 *               error code. (Note that in this case, the sorting process
 *               will be aborted.)
 *   get_arg(in): arguments to the get_fn function
 *   put_fn(in): user-supplied function: provides the output operation to be
 *               applied on the sorted items. This function should process
 *               (in any way it chooses so) the sort item passed via the
 *               record descriptor "temp_recdes" and return NO_ERROR to be
 *               called again with the next item on the sorted order. If it
 *               returns any error code then the sorting process is aborted.
 *   put_arg(in): arguments to the put_fn function
 *   cmp_fn(in): user-supplied function for comparing records to be sorted.
 *               This function is expected to follow the strcmp() protocol
 *               for return results: -1 means the first arg precedes the
 *               second, 1 means the second precedes the first, and 0 means
 *               neither precedes the other.
 *   cmp_arg(in): arguments to the cmp_fn function
 *   option(in):
 *   limit(in):  optional arg, can represent the limit clause. If we only want
 *               the top K elements of a processed list, it makes sense to use
 *               special optimizations instead of sorting the entire list and
 *               returning the first K elements.
 *               Parameter: -1 if not to be used, > 0 if it should be taken
 *               into account. Zero is a reserved value.
 *   includes_tde_class(in): whether tde-configured class data is included or not,
 *                           it determines whehter internal temp files are 
 *                           encrypted or not.
 */
int
sort_listfile (THREAD_ENTRY * thread_p, INT16 volid, int est_inp_pg_cnt, SORT_GET_FUNC * get_fn, void *get_arg,
	       SORT_PUT_FUNC * put_fn, void *put_arg, SORT_CMP_FUNC * cmp_fn, void *cmp_arg, SORT_DUP_OPTION option,
	       int limit, bool includes_tde_class, bool is_parallel)
{

  int error = NO_ERROR;
  SORT_PARAM *sort_param = NULL;
  INT32 input_pages;
  int i;

  /* for parallel sort */
  SORT_PARAM px_sort_param[SORT_MAX_PARALLEL];	/* TO_DO : need dynamic alloc */
  QFILE_LIST_SCAN_ID *scan_id_p;
  int parallel_num = 2;		/* TO_DO : depending on the number of pages in the temp file */

  thread_set_sort_stats_active (thread_p, true);

#if defined(ENABLE_SYSTEMTAP)
  CUBRID_SORT_START ();
#endif /* ENABLE_SYSTEMTAP */

  sort_param = (SORT_PARAM *) malloc (sizeof (SORT_PARAM));
  if (sort_param == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (SORT_PARAM));
      return error;
    }

  sort_param->cmp_fn = cmp_fn;
  sort_param->cmp_arg = cmp_arg;
  sort_param->option = option;

  sort_param->get_fn = get_fn;
  sort_param->get_arg = get_arg;

  sort_param->put_fn = put_fn;
  sort_param->put_arg = put_arg;

  sort_param->limit = limit;
  sort_param->tot_tempfiles = 0;

  /* initialize memory allocable fields */
  for (i = 0; i < 2 * SORT_MAX_HALF_FILES; i++)
    {
      sort_param->temp[i].volid = NULL_VOLID;
      sort_param->file_contents[i].num_pages = NULL;
    }
  sort_param->internal_memory = NULL;

  /* initialize temp. overflow file. Real value will be assigned in sort_inphase_sort function, if long size sorting
   * records are encountered. */
  sort_param->multipage_file.volid = NULL_VOLID;
  sort_param->multipage_file.fileid = NULL_FILEID;

  /* NOTE: This volume list will not be used any more. */
  /* initialize temporary volume list */
  sort_param->vol_list.volid = volid;
  sort_param->vol_list.vol_ent_cnt = 0;
  sort_param->vol_list.vol_cnt = 0;
  sort_param->vol_list.vol_info = NULL;

  if (est_inp_pg_cnt > 0)
    {
      /* 10% of overhead and fragmentation */
      input_pages = est_inp_pg_cnt + MAX ((int) (est_inp_pg_cnt * 0.1), 2);
    }
  else
    {
      input_pages = prm_get_integer_value (PRM_ID_SR_NBUFFERS);
    }

  /* The size of a sort buffer is limited to PRM_SR_NBUFFERS. */
  sort_param->tot_buffers = MIN (prm_get_integer_value (PRM_ID_SR_NBUFFERS), input_pages);
  sort_param->tot_buffers = MAX (4, sort_param->tot_buffers);

  sort_param->internal_memory = (char *) malloc ((size_t) sort_param->tot_buffers * (size_t) DB_PAGESIZE);
  if (sort_param->internal_memory == NULL)
    {
      sort_param->tot_buffers = 4;

      sort_param->internal_memory = (char *) malloc (sort_param->tot_buffers * DB_PAGESIZE);
      if (sort_param->internal_memory == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) (sort_param->tot_buffers * DB_PAGESIZE));
	  goto cleanup;
	}
    }

  sort_param->half_files = sort_get_num_half_tmpfiles (sort_param->tot_buffers, input_pages);
  sort_param->tot_tempfiles = sort_param->half_files << 1;
  sort_param->in_half = 0;

  for (i = 0; i < sort_param->tot_tempfiles; i++)
    {
      /* Initilize temporary file identifier; real value will be set in "sort_add_new_file () */
      sort_param->temp[i].volid = NULL_VOLID;

      /* Initilize file contents list */
      sort_param->file_contents[i].num_pages = (int *) malloc (SORT_INITIAL_DYN_ARRAY_SIZE * sizeof (int));
      if (sort_param->file_contents[i].num_pages == NULL)
	{
	  sort_param->tot_tempfiles = i;
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}

      sort_param->file_contents[i].num_slots = SORT_INITIAL_DYN_ARRAY_SIZE;
      sort_param->file_contents[i].first_run = -1;
      sort_param->file_contents[i].last_run = -1;
    }

  /* Create only input temporary files make file and temporary volume page count estimates */
  sort_param->tmp_file_pgs = CEIL_PTVDIV (input_pages, sort_param->half_files);
  sort_param->tmp_file_pgs = MAX (1, sort_param->tmp_file_pgs);

  sort_param->tde_encrypted = includes_tde_class;


#if defined(SERVER_MODE)
  SORT_INFO *sort_info_p;

  /* check parallelism */
  if (sort_param->get_fn == qfile_get_next_sort_item)
    {
      /* get scan id of input file */
      sort_info_p = (SORT_INFO *) sort_param->get_arg;

      /* need to check the appropriate number of parallels depending on the number of pages */
      if (sort_info_p->input_file->page_cnt < parallel_num || sort_info_p->input_file->tuple_cnt < parallel_num
	  || parallel_num < 2)
	{
	  is_parallel = false;
	}
    }

  if (!is_parallel)
    {
      sort_param->px_max_index = 1;
      error = sort_listfile_internal (thread_p, sort_param);
    }
  else
    {
      /* copy sort_param for parallel sort */
      error = sort_copy_sort_param (thread_p, px_sort_param, sort_param, parallel_num);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      /* case of ORDER BY */
      if (sort_param->get_fn == qfile_get_next_sort_item)
	{
	  /* split input temp file. TO_DO : it can be inside sort_copy_sort_param() */
	  error = sort_split_input_temp_file (thread_p, px_sort_param, sort_param, parallel_num);
	  /* may need to revert the next page and prev page of temp file. */
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}
      else
	{
	  /* not implemented yet (group by, analytic fuction, create index) */
	  return -1;
	}

      // *INDENT-OFF*
      /* parallel execute */
      for (int i = 0; i < parallel_num; i++)
	{
	  cubthread::entry_callable_task * task =
	    new cubthread::entry_callable_task (std::
						bind (sort_listfile_execute, std::placeholders::_1, &px_sort_param[i]));
	  css_push_external_task (css_get_current_conn_entry (), task);
	}
      // *INDENT-ON*

      /* wait for threads */
      /* TO_DO : no busy wait. need to block and wake up */
      int done;
      do
	{
	  thread_sleep (10);
	  done = true;
	  for (int i = 0; i < parallel_num; i++)
	    {
	      if (px_sort_param[i].px_status != 1)
		{
		  done = false;
		  break;
		}
	    }
	  if (done)
	    {
	      break;
	    }
	}
      while (1);

      if (sort_param->get_fn == qfile_get_next_sort_item)
	{
	  sort_info_p = (SORT_INFO *) sort_param->get_arg;
	  scan_id_p = sort_info_p->s_id->s_id;

	  /* open origin temp file for read */
	  if (qfile_open_list_scan (sort_info_p->input_file, scan_id_p) != NO_ERROR)
	    {
	      assert (0);
	      return -1;
	    }
	}
      else
	{
	  /* not implemented yet (group by, analytic fuction, create index) */
	  return -1;
	}

      /* merging temp files from parallel processed */
      error = sort_merge_run_for_parallel (thread_p, px_sort_param, sort_param, parallel_num);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

#else
  /* no parallel */
  sort_param->px_max_index = 1;
  is_parallel = false;
  error = sort_listfile_internal (thread_p, sort_param);
#endif

cleanup:
#if defined(ENABLE_SYSTEMTAP)
  CUBRID_SORT_END (sort_param->total_numrecs, error);
#endif /* ENABLE_SYSTEMTAP */

  /* free sort_param */
  if (is_parallel)
    {
      for (int i = 0; i < parallel_num; i++)
	{
	  sort_return_used_resources (thread_p, &px_sort_param[i], PX_THREAD_IN_PARALLEL);
	}

      sort_return_used_resources (thread_p, sort_param, PX_MAIN_IN_PARALLEL);
    }
  else
    {
      sort_return_used_resources (thread_p, sort_param, PX_SINGLE);
    }

  thread_set_sort_stats_active (thread_p, false);

  return error;
}

#if defined(SERVER_MODE)
// *INDENT-OFF*
static void
sort_listfile_execute (cubthread::entry &thread_ref, SORT_PARAM * sort_param)
{
  QFILE_LIST_SCAN_ID t_scan_id;

  thread_ref.tran_index = sort_param->px_tran_index;
  pthread_mutex_unlock (&thread_ref.tran_index_lock);

/*  printf ("sort_listfie index = %d\n",thread_ref.index); */
  if (sort_param->get_fn == qfile_get_next_sort_item)
    {
      SORT_INFO *sort_info_p = (SORT_INFO *) sort_param->get_arg;

      /* temporarily, open file for read */
      if (qfile_open_list_scan (sort_info_p->input_file, &t_scan_id) != NO_ERROR)
	{
	  /* need to put error into sort_param */
	  return;
	}
      sort_info_p->s_id->s_id = &t_scan_id;
    }
  else
    {
      /* need to put error into sort_param */
      assert(0);
      return;
    }

  sort_listfile_internal (&thread_ref, sort_param);

  /* temporarily, close file for read */
  if (sort_param->get_fn == qfile_get_next_sort_item)
    {
      SORT_INFO *sort_info_p = (SORT_INFO *) sort_param->get_arg;

      /* temporarily, close file for read */
      qfile_close_scan (&thread_ref, sort_info_p->s_id->s_id);
    }
  else
    {
      /* need to put error into sort_param */
      assert(0);
      return;
    }
  sort_param->px_status = 1;
}
// *INDENT-ON*
#endif

/*
 * sort_listfile_internal () - Perform sorting
 *   return:
 */
int
sort_listfile_internal (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param)
{
  int error = NO_ERROR;
  int file_pg_cnt_est;
  int i;

  /*
   * Don't allocate any temp files yet, since we may not need them.
   * We'll allocate them on the fly as the need arises.
   *
   * However, indicate to file/disk manager of the approximate temporary
   * space that is going to be needed.
   */

  error = sort_inphase_sort (thread_p, sort_param, sort_param->get_fn, sort_param->get_arg, &sort_param->total_numrecs);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (sort_param->tot_runs > 1 || IS_PARALLEL_EXECUTION (sort_param))
    {
      assert (sort_param->tot_runs > 0);
      /* Create output temporary files make file and temporary volume page count estimates */
      file_pg_cnt_est = sort_get_avg_numpages_of_nonempty_tmpfile (sort_param);
      file_pg_cnt_est = MAX (1, file_pg_cnt_est);

      for (i = sort_param->half_files; i < sort_param->tot_tempfiles; i++)
	{
	  error =
	    sort_add_new_file (thread_p, &(sort_param->temp[i]), file_pg_cnt_est, true, sort_param->tde_encrypted);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      if (sort_param->option == SORT_ELIM_DUP)
	{
	  error = sort_exphase_merge_elim_dup (thread_p, sort_param);
	}
      else
	{
	  /* SORT_DUP */
	  error = sort_exphase_merge (thread_p, sort_param);
	}
    }				/* if (sort_param->tot_runs > 1) */

  return error;
}

#if !defined(NDEBUG)
/*
 * sort_validate() -
 *   return:
 *   vector(in):
 *   size(in):
 *   compare(in):
 *   comp_arg(in):
 *
 * NOTE: debugging function
 */
static int
sort_validate (char **vector, long size, SORT_CMP_FUNC * compare, void *comp_arg)
{
  long k;
  int cmp;

  assert (vector != NULL);
  assert (size >= 1);
  assert (compare != NULL);

#if 1				/* TODO - for QAF, do not delete me */
  return NO_ERROR;
#endif

  for (k = 0; k < size - 1; k++)
    {
      cmp = (*compare) (&(vector[k]), &(vector[k + 1]), comp_arg);
      if (cmp != DB_LT)
	{
	  assert (false);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}
#endif

/*
 * sort_inphase_sort () - Internal sorting phase
 *   return:
 *   sort_param(in): sort parameters
 *   get_fn(in): user-supplied function: provides the temporary record for
 *               the given input record
 *   get_arg(in): arguments for get_fn
 *   total_numrecs(out): records sorted
 *
 */
static int
sort_inphase_sort (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, SORT_GET_FUNC * get_fn, void *get_arg,
		   unsigned int *total_numrecs)
{
  /* Variables for the input file */
  SORT_STATUS status;

  /* Variables for the current output file */
  int out_curfile;
  char *output_buffer;
  int cur_page[SORT_MAX_HALF_FILES];

  /* Variables for the internal memory */
  RECDES temp_recdes;
  RECDES long_recdes;		/* Record desc. for reading in long sorting records */
  char *item_ptr;		/* Pointer to the first free location of the temp. records region of internal memory */
  long numrecs;			/* Number of records kept in the internal memory */
  long sort_numrecs;		/* Number of sort records kept in the internal memory */
  bool once_flushed = false;
  long saved_numrecs;
  char **saved_index_area;
  char **index_area;		/* Part of internal memory keeping the addresses of records */
  char **index_buff;		/* buffer area to sort indexes. */
  int i;
  int error = NO_ERROR;

#if defined (SERVER_MODE)
  int rv = NO_ERROR;
#endif /* SERVER_MODE */

  assert (sort_param->half_files <= SORT_MAX_HALF_FILES);

  /* Initialize the current pages of all temp files to 0 */
  for (i = 0; i < sort_param->half_files; i++)
    {
      cur_page[i] = 0;
    }

  sort_param->tot_runs = 0;
  out_curfile = sort_param->in_half;

  output_buffer = sort_param->internal_memory + ((long) (sort_param->tot_buffers - 1) * DB_PAGESIZE);
  assert (output_buffer > sort_param->internal_memory);

  numrecs = 0;
  sort_numrecs = 0;
  saved_numrecs = 0;
  *total_numrecs = 0;
  saved_index_area = NULL;
  item_ptr = sort_param->internal_memory + SORT_RECORD_LENGTH_SIZE;
  index_area = (char **) (output_buffer - sizeof (char *));
  index_buff = index_area - 1;
  temp_recdes.area_size = SORT_MAXREC_LENGTH;
  temp_recdes.length = 0;

  long_recdes.area_size = 0;
  long_recdes.data = NULL;

  for (;;)
    {
      if ((char *) index_buff < item_ptr)
	{
	  /* Internal memory is already full */
	  status = SORT_REC_DOESNT_FIT;
	}
      else
	{
	  /* Internal memory is not full; try to get the next item */
	  temp_recdes.data = item_ptr;
	  if (((int) ((char *) index_buff - item_ptr)) < SORT_MAXREC_LENGTH)
	    {
	      temp_recdes.area_size = (int) ((char *) index_buff - item_ptr) - (4 * sizeof (char *));
	    }

	  if (temp_recdes.area_size <= SSIZEOF (SORT_REC))
	    {
	      /* internal memory is not enough */
	      status = SORT_REC_DOESNT_FIT;
	    }
	  else
	    {
	      status = (*get_fn) (thread_p, &temp_recdes, get_arg);
	      /* There are no more input records; So, break the loop */
	      if (status == SORT_NOMORE_RECS)
		{
		  break;
		}
	    }
	}

      switch (status)
	{
	case SORT_ERROR_OCCURRED:
	  error = er_errid ();
	  assert (error != NO_ERROR);
	  goto exit_on_error;

	case SORT_REC_DOESNT_FIT:
	  if (numrecs > 0)
	    {
	      /* Perform internal sorting and flush the run */

	      index_area++;

	      index_area =
		sort_run_sort (thread_p, sort_param, index_area, numrecs, sort_numrecs, index_buff, &numrecs);
	      *total_numrecs += numrecs;

	      if (index_area == NULL || numrecs < 0)
		{
		  error = ER_FAILED;
		  goto exit_on_error;
		}

	      if (sort_param->option == SORT_ELIM_DUP)
		{
		  /* reset item_ptr */
		  item_ptr = index_area[0];
		  for (i = 1; i < numrecs; i++)
		    {
		      if (index_area[i] > item_ptr)
			{
			  item_ptr = index_area[i];
			}
		    }

		  item_ptr += DB_ALIGN (SORT_RECORD_LENGTH (item_ptr), MAX_ALIGNMENT) + SORT_RECORD_LENGTH_SIZE;
		}

	      if (sort_param->option == SORT_ELIM_DUP
		  && ((item_ptr - sort_param->internal_memory) + numrecs * SSIZEOF (SLOT) < DB_PAGESIZE)
		  && temp_recdes.length <= SORT_MAXREC_LENGTH)
		{
		  /* still, remaining key area enough; do not flush, go on internal sorting */
		  index_buff = index_area - numrecs;
		  index_area--;
		  index_buff--;	/* decrement once for pointer, */
		  index_buff--;	/* once for pointer buffer */

		  /* must keep track because index_buff is used */
		  /* to detect when sort buffer is full */
		  temp_recdes.area_size = SORT_MAXREC_LENGTH;

		  assert ((char *) index_buff >= item_ptr);
		}
	      else
		{
		  error =
		    sort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs,
				    REC_HOME);
		  if (error != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* Prepare for the next internal sorting run */

		  if (sort_param->tot_runs == 1)
		    {
		      once_flushed = true;
		      saved_numrecs = numrecs;
		      saved_index_area = index_area;
		    }

		  numrecs = 0;
		  item_ptr = sort_param->internal_memory + SORT_RECORD_LENGTH_SIZE;
		  index_area = (char **) (output_buffer - sizeof (char *));
		  index_buff = index_area - 1;
		  temp_recdes.area_size = SORT_MAXREC_LENGTH;

		  /* Switch to the next Temp file */
		  if (++out_curfile >= sort_param->half_files)
		    {
		      out_curfile = sort_param->in_half;
		    }
		}

	      /* save sorted record number at this stage */
	      sort_numrecs = numrecs;
	    }

	  /* Check if the record would fit into a single slotted page. If not, take special action for this record. */
	  if (temp_recdes.length > SORT_MAXREC_LENGTH)
	    {
	      /* TAKE CARE OF LONG RECORD as a separate RUN */

	      if (long_recdes.area_size < temp_recdes.length)
		{
		  /* read in the long record to a dynamic memory area */
		  if (long_recdes.data)
		    {
		      free_and_init (long_recdes.data);
		    }

		  long_recdes.area_size = temp_recdes.length;
		  long_recdes.data = (char *) malloc (long_recdes.area_size);

		  if (long_recdes.data == NULL)
		    {
		      error = ER_OUT_OF_VIRTUAL_MEMORY;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) long_recdes.area_size);
		      goto exit_on_error;
		    }
		}

	      /* Obtain the long record */
	      status = (*get_fn) (thread_p, &long_recdes, get_arg);

	      if (status != SORT_SUCCESS)
		{
		  /* Obtaining the long record has failed */
		  if (status == SORT_REC_DOESNT_FIT || status == SORT_NOMORE_RECS)
		    {
		      /* This should never happen */
		      error = ER_GENERIC_ERROR;
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		  else
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		    }

		  assert (error != NO_ERROR);

		  goto exit_on_error;
		}

	      /* Put the record to the multipage area & put the pointer to internal memory area */

	      /* If necessary create the multipage_file */
	      if (sort_param->multipage_file.volid == NULL_VOLID)
		{
		  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
		  /* Create the multipage file */
		  sort_param->multipage_file.volid = sort_param->temp[0].volid;

		  error = file_create_temp (thread_p, 1, &sort_param->multipage_file);
		  if (error != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      goto exit_on_error;
		    }
		  if (sort_param->tde_encrypted)
		    {
		      tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
		    }
		  if (file_apply_tde_algorithm (thread_p, &sort_param->multipage_file, tde_algo) != NO_ERROR)
		    {
		      file_temp_retire (thread_p, &sort_param->multipage_file);
		      ASSERT_ERROR ();
		      goto exit_on_error;
		    }
		}

	      /* Create a multipage record for this long record : insert to multipage_file and put the pointer as the
	       * first record in this run */
	      if (overflow_insert (thread_p, &sort_param->multipage_file, (VPID *) item_ptr, &long_recdes, FILE_TEMP)
		  != NO_ERROR)
		{
		  ASSERT_ERROR_AND_SET (error);
		  goto exit_on_error;
		}

	      /* Update the pointers */
	      SORT_RECORD_LENGTH (item_ptr) = sizeof (VPID);
	      *index_area = item_ptr;
	      numrecs++;

	      error =
		sort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs,
				REC_BIGONE);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* Prepare for the next internal sorting run */
	      numrecs = 0;
	      item_ptr = sort_param->internal_memory + SORT_RECORD_LENGTH_SIZE;
	      index_area = (char **) (output_buffer - sizeof (char *));
	      index_buff = index_area - 1;
	      temp_recdes.area_size = SORT_MAXREC_LENGTH;

	      /* Switch to the next Temp file */
	      if (++out_curfile >= sort_param->half_files)
		{
		  out_curfile = sort_param->in_half;
		}

	      /* save sorted record number at this stage */
	      sort_numrecs = numrecs;
	    }
	  break;

	case SORT_SUCCESS:
	  /* Proceed the pointers */
	  SORT_RECORD_LENGTH (item_ptr) = temp_recdes.length;
	  *index_area = item_ptr;
	  numrecs++;

	  index_area--;
	  index_buff--;		/* decrease once for pointer, once for pointer buffer */
	  index_buff--;		/* must keep track because index_buff is used to detect when sort buffer is full */

	  item_ptr += DB_ALIGN (temp_recdes.length, MAX_ALIGNMENT) + SORT_RECORD_LENGTH_SIZE;
	  break;

	default:
	  /* This should never happen */
	  error = ER_GENERIC_ERROR;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto exit_on_error;
	}
    }

  if (numrecs > 0)
    {
      /* The input file has finished; process whatever is left over in the internal memory */

      index_area++;

      index_area = sort_run_sort (thread_p, sort_param, index_area, numrecs, sort_numrecs, index_buff, &numrecs);
      *total_numrecs += numrecs;

      if (index_area == NULL || numrecs < 0)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      if (sort_param->tot_runs > 0 || IS_PARALLEL_EXECUTION (sort_param))
	{
	  /* There has been other runs produced already */

	  error =
	    sort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs, REC_HOME);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* No run has been produced yet There is no need for the merging phase; directly output the sorted temp
	   * records. */

	  for (i = 0; i < numrecs; i++)
	    {
	      /* Obtain the output record for this temporary record */
	      temp_recdes.data = index_area[i];
	      temp_recdes.length = SORT_RECORD_LENGTH (index_area[i]);

	      error = (*sort_param->put_fn) (thread_p, &temp_recdes, sort_param->put_arg);
	      if (error != NO_ERROR)
		{
		  if (error == SORT_PUT_STOP)
		    {
		      error = NO_ERROR;
		    }
		  goto exit_on_error;
		}
	    }
	}
    }
  else if (sort_param->tot_runs == 1 && !IS_PARALLEL_EXECUTION (sort_param))
    {
      if (once_flushed)
	{
	  for (i = 0; i < saved_numrecs; i++)
	    {
	      /* Obtain the output record for this temporary record */
	      temp_recdes.data = saved_index_area[i];
	      temp_recdes.length = SORT_RECORD_LENGTH (saved_index_area[i]);

	      error = (*sort_param->put_fn) (thread_p, &temp_recdes, sort_param->put_arg);
	      if (error != NO_ERROR)
		{
		  if (error == SORT_PUT_STOP)
		    {
		      error = NO_ERROR;
		    }
		  goto exit_on_error;
		}
	    }
	}
      else
	{
	  /*
	   * The only way to get here is if we had exactly one record to
	   * sort, and that record required overflow pages.  In that case we
	   * have done a ridiculous amount of work, but there doesn't seem to
	   * be an easy way to restructure the existing sort_inphase_sort code to
	   * cope with the situation.  Just go get the record and be happy...
	   */
	  error = NO_ERROR;
	  temp_recdes.data = NULL;
	  long_recdes.area_size = 0;
	  if (long_recdes.data)
	    {
	      free_and_init (long_recdes.data);
	    }

	  if (sort_read_area (thread_p, &sort_param->temp[0], 0, 1, output_buffer) != NO_ERROR
	      || sort_spage_get_record (output_buffer, 0, &temp_recdes, PEEK) != S_SUCCESS
	      || sort_retrieve_longrec (thread_p, &temp_recdes, &long_recdes) == NULL
	      || (*sort_param->put_fn) (thread_p, &long_recdes, sort_param->put_arg) != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      error = er_errid ();
	      goto exit_on_error;
	    }
	}
    }

exit_on_error:

  if (long_recdes.data)
    {
      free_and_init (long_recdes.data);
    }

  return error;
}

/*
 * sort_run_flush () - Flush run
 *   return:
 *   sort_param(in): sort parameters
 *   out_file(in): index of output file to flush the run
 *   cur_page(in): current page of each temp file (used to determine
 *                 where, within the file, the run should be flushed)
 *   output_buffer(in): output buffer to use for flushing the records
 *   index_area(in): index area keeping ordered pointers to the records
 *   numrecs(in): number of records the run includes
 *   rec_type(in): type of records; Assume that all the records of this
 *                 run has the same type. This may need to be changed
 *                 to allow individual records have different types.
 *
 * Note: This function flushes a run to the specified output file. The records
 *       of the run are loaded to the output buffer in the order imposed by
 *       the index area (i.e., on the order of pointers to these records).
 *       This buffer is written to the successive pages of the specified file
 *       when it is full.
 */
static int
sort_run_flush (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, int out_file, int *cur_page, char *output_buffer,
		char **index_area, int numrecs, int rec_type)
{
  int error = NO_ERROR;
  int run_size;
  RECDES out_recdes;
  int i;
  SORT_REC *key, *next;
  int flushed_items = 0;
  int should_continue = true;

  /* Make sure the the temp file indexed by out_file has been created; if not, create it now. */
  if (sort_param->temp[out_file].volid == NULL_VOLID)
    {
      error =
	sort_add_new_file (thread_p, &sort_param->temp[out_file], sort_param->tmp_file_pgs, false,
			   sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* Store the record type; used for REC_BIGONE record types */
  out_recdes.type = rec_type;

  run_size = 0;
  sort_spage_initialize (output_buffer, UNANCHORED_KEEP_SEQUENCE, MAX_ALIGNMENT);

  /* Insert each record to the output buffer and flush the buffer when it is full */
  for (i = 0; i < numrecs && should_continue; i++)
    {
      /* Traverse next link */
      for (key = (SORT_REC *) index_area[i]; key; key = next)
	{
	  /* If we've already flushed the required number, stop. */
	  if (sort_param->limit > 0 && flushed_items >= sort_param->limit)
	    {
	      should_continue = false;
	      break;
	    }

	  /* cut-off and save duplicate sort_key value link */
	  if (rec_type == REC_HOME)
	    {
	      next = key->next;
	    }
	  else
	    {
	      /* REC_BIGONE */
	      next = NULL;
	    }

	  out_recdes.data = (char *) key;
	  out_recdes.length = SORT_RECORD_LENGTH ((char *) key);

	  if (sort_spage_insert (output_buffer, &out_recdes) == NULL_SLOTID)
	    {
	      /* Output buffer is full */
	      error =
		sort_write_area (thread_p, &sort_param->temp[out_file], cur_page[out_file], 1, output_buffer,
				 sort_param->tde_encrypted);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      cur_page[out_file]++;
	      run_size++;
	      sort_spage_initialize (output_buffer, UNANCHORED_KEEP_SEQUENCE, MAX_ALIGNMENT);

	      if (sort_spage_insert (output_buffer, &out_recdes) == NULL_SLOTID)
		{
		  /* Slotted page module refuses to insert a short size record to an empty page. This should never
		   * happen. */
		  error = ER_GENERIC_ERROR;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  return error;
		}
	    }
	  flushed_items++;
	}
    }

  if (sort_spage_get_numrecs (output_buffer))
    {
      /* Flush the partially full output page */
      error =
	sort_write_area (thread_p, &sort_param->temp[out_file], cur_page[out_file], 1, output_buffer,
			 sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* Update sort parameters */
      cur_page[out_file]++;
      run_size++;
    }

  /* Record the insertion of the new pages of the file to global parameters */
  error = sort_run_add_new (&sort_param->file_contents[out_file], run_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  sort_param->tot_runs++;

  return NO_ERROR;
}

/*
 * sort_retrieve_longrec () -
 *   return:
 *   address(in):
 *   memory(in):
 */
static char *
sort_retrieve_longrec (THREAD_ENTRY * thread_p, RECDES * address, RECDES * memory)
{
  int needed_area_size;

  /* Find the required area for the long record */
  needed_area_size = overflow_get_length (thread_p, (VPID *) address->data);
  if (needed_area_size == -1)
    {
      return NULL;
    }

  /* If necessary allocate dynamic area for the long record */
  if (needed_area_size > memory->area_size)
    {
      /* There is already a small area; free it. */
      if (memory->data != NULL)
	{
	  free_and_init (memory->data);
	}

      /* Allocate dynamic area for this long record */
      memory->area_size = needed_area_size;
      memory->data = (char *) malloc (memory->area_size);
      if (memory->data == NULL)
	{
	  return NULL;
	}
    }

  /* Retrieve the long record */
  if (overflow_get (thread_p, (VPID *) address->data, memory, NULL) != S_SUCCESS)
    {
      return NULL;
    }

  return memory->data;
}

/*
 * sort_exphase_merge_elim_dup () -
 *   return:
 *   sort_param(in):
 */
static int
sort_exphase_merge_elim_dup (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param)
{
  /* Variables for input files */
  int act_infiles;		/* How many of input files are active */
  int pre_act_infiles;		/* Number of active input files in the previous iteration */
  int in_sectsize;		/* Size of section allocated to each active input file (in terms of number of buffers
				 * it contains) */
  int read_pages;		/* Number of pages read in to fill the input buffer */
  int in_act_bufno[SORT_MAX_HALF_FILES];	/* Active buffer in the input section */
  int in_last_buf[SORT_MAX_HALF_FILES];	/* Last full buffer of the input section */
  int act_slot[SORT_MAX_HALF_FILES];	/* Active slot of the active buffer of input section */
  int last_slot[SORT_MAX_HALF_FILES];	/* Last slot of the active buffer of the input section */

  char *in_sectaddr[SORT_MAX_HALF_FILES];	/* Beginning address of each input section */
  char *in_cur_bufaddr[SORT_MAX_HALF_FILES];	/* Address of the current buffer in each input section */

  /* Variables for output file */
  int out_half;			/* Which half of temp files is for output */
  int cur_outfile;		/* Index for output file recieving new run */
  int out_sectsize;		/* Size of the output section (in terms of number of buffer it contains) */
  int out_act_bufno;		/* Active buffer in the output section */
  int out_runsize;		/* Total pages output for the run being produced */
  char *out_sectaddr;		/* Beginning address of the output section */
  char *out_cur_bufaddr;	/* Address of the current buffer in the output section */

  /* Smallest element pointers (one for each active input file) pointing to the active temp records. If the input file
   * becomes inactive (all input is exhausted), its smallest element pointer is set to NULL */
  RECDES smallest_elem_ptr[SORT_MAX_HALF_FILES];
  RECDES long_recdes[SORT_MAX_HALF_FILES];

  int cur_page[2 * SORT_MAX_HALF_FILES];	/* Current page of each temp file */
  int num_runs;			/* Number of output runs to be produced in this stage of the merging phase; */
  int big_index;
  int error;
  int i, j;
  int temp;
  int min;
  int len;
  bool very_last_run = false;
  int act;
  int cp_pages;
  int cmp;

  SORT_CMP_FUNC *compare;
  void *compare_arg;

  SORT_REC_LIST sr_list[SORT_MAX_HALF_FILES], *min_p, *s, *p;
  int tmp_var;
  RECDES last_elem_ptr;		/* last element pointer in one page of input section */
  RECDES last_long_recdes;

  /* >0 : must find minimum record <0 : last element in the current input section is minimum record. no need to find
   * min record =0 : last element in the current input section is duplicated min record. no need to find min record
   * except last element */
  int last_elem_cmp;

  char **data1, **data2;
  SORT_REC *sort_rec;
  int first_run;

  error = NO_ERROR;

  compare = sort_param->cmp_fn;
  compare_arg = sort_param->cmp_arg;

  for (i = 0; i < SORT_MAX_HALF_FILES; i++)
    {
      in_act_bufno[i] = 0;
      in_last_buf[i] = 0;
      act_slot[i] = 0;
      last_slot[i] = 0;
      in_sectaddr[i] = NULL;
      in_cur_bufaddr[i] = NULL;

      smallest_elem_ptr[i].data = NULL;
      smallest_elem_ptr[i].area_size = 0;

      long_recdes[i].data = NULL;
      long_recdes[i].area_size = 0;
    }

  last_elem_ptr.data = NULL;
  last_elem_ptr.area_size = 0;

  last_long_recdes.data = NULL;
  last_long_recdes.area_size = 0;

  for (i = 0; i < (int) DIM (cur_page); i++)
    {
      cur_page[i] = 0;
    }

  if (sort_param->in_half == 0)
    {
      out_half = sort_param->half_files;
    }
  else
    {
      out_half = 0;
    }

  /* OUTER LOOP */

  /* for one temporary file, put result from the temp file instead of merging it. */
  if (!IS_PARALLEL_EXECUTION (sort_param) && sort_get_numpages_of_active_infiles (sort_param) == 1)
    {
      error = sort_put_result_from_tmpfile (thread_p, sort_param);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto bailout;
	}
    }

  /* While there are more than one input files with different runs to merge */
  while ((act_infiles = sort_get_numpages_of_active_infiles (sort_param)) > 1)
    {
      /* Check if output files has enough pages; if not allocate new pages */
      error = sort_checkalloc_numpages_of_outfiles (thread_p, sort_param);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto bailout;
	}

      /* Initialize the current pages of all temp files to 0 */
      for (i = 0; i < sort_param->tot_tempfiles; i++)
	{
	  cur_page[i] = 0;
	}

      /* Distribute the internal memory to the input and output sections */
      in_sectsize = sort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
      out_sectsize = sort_param->tot_buffers - in_sectsize * act_infiles;

      /* Set the address of each input section */
      for (i = 0; i < act_infiles; i++)
	{
	  in_sectaddr[i] = sort_param->internal_memory + (i * in_sectsize * DB_PAGESIZE);
	}

      /* Set the address of output section */
      out_sectaddr = sort_param->internal_memory + (act_infiles * in_sectsize * DB_PAGESIZE);

      cur_outfile = out_half;

      /* Find how many runs will be produced in this iteration */
      num_runs = 0;
      for (i = sort_param->in_half; i < sort_param->in_half + act_infiles; i++)
	{
	  len = sort_get_num_file_contents (&sort_param->file_contents[i]);
	  if (len > num_runs)
	    {
	      num_runs = len;
	    }
	}

      if (num_runs == 1)
	{
	  very_last_run = true;
	}

      /* PRODUCE RUNS */

      for (j = num_runs; j > 0; j--)
	{
	  if (!very_last_run && (j == 1))
	    {
	      /* Last iteration of the outer loop ; some of the input files might have become empty. */

	      pre_act_infiles = act_infiles;
	      act_infiles = sort_get_numpages_of_active_infiles (sort_param);
	      if (act_infiles != pre_act_infiles)
		{
		  /* Some of the active input files became inactive */

		  if (act_infiles == 1)
		    {
		      /*
		       * There is only one active input file (i.e. there is
		       * only one input run to produce the output run). So,
		       * there is no need to perform the merging actions. All
		       * needed is to copy this input run to the current output
		       * file.
		       */

		      act = -1;

		      /* Find which input file contains this last input run */
		      for (i = sort_param->in_half; i < (sort_param->in_half + pre_act_infiles); i++)
			{
			  if (sort_param->file_contents[i].first_run != -1)
			    {
			      act = i;
			      break;
			    }
			}

		      if (act == -1)
			{
			  goto bailout;
			}

		      first_run = sort_param->file_contents[act].first_run;
		      cp_pages = sort_param->file_contents[act].num_pages[first_run];

		      error = sort_run_add_new (&sort_param->file_contents[cur_outfile], cp_pages);
		      if (error != NO_ERROR)
			{
			  goto bailout;
			}

		      sort_run_remove_first (&sort_param->file_contents[act]);

		      /* Use the whole internal_memory area as both the input and output buffer areas. */
		      while (cp_pages > 0)
			{
			  if (cp_pages > sort_param->tot_buffers)
			    {
			      read_pages = sort_param->tot_buffers;
			    }
			  else
			    {
			      read_pages = cp_pages;
			    }

			  error =
			    sort_read_area (thread_p, &sort_param->temp[act], cur_page[act], read_pages,
					    sort_param->internal_memory);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  cur_page[act] += read_pages;
			  error =
			    sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile],
					     read_pages, sort_param->internal_memory, sort_param->tde_encrypted);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  cur_page[cur_outfile] += read_pages;
			  cp_pages -= read_pages;
			}

		      /* Skip the remaining operations of the PRODUCE RUNS loop */
		      continue;
		    }
		  else
		    {
		      /* There are more than one active input files; redistribute buffers */
		      in_sectsize = sort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
		      out_sectsize = sort_param->tot_buffers - in_sectsize * act_infiles;

		      /* Set the address of each input section */
		      for (i = 0; i < act_infiles; i++)
			{
			  in_sectaddr[i] = sort_param->internal_memory + (i * in_sectsize * DB_PAGESIZE);
			}

		      /* Set the address of output section */
		      out_sectaddr = sort_param->internal_memory + (act_infiles * in_sectsize * DB_PAGESIZE);
		    }
		}
	    }

	  /* PRODUCE A NEW RUN */

	  /* INITIALIZE INPUT SECTIONS AND INPUT VARIABLES */
	  for (i = 0; i < act_infiles; i++)
	    {
	      big_index = sort_param->in_half + i;
	      first_run = sort_param->file_contents[big_index].first_run;
	      read_pages = sort_param->file_contents[big_index].num_pages[first_run];

	      if (in_sectsize < read_pages)
		{
		  read_pages = in_sectsize;
		}

	      error =
		sort_read_area (thread_p, &sort_param->temp[big_index], cur_page[big_index], read_pages,
				in_sectaddr[i]);
	      if (error != NO_ERROR)
		{
		  goto bailout;
		}

	      /* Increment the current page of this input_file */
	      cur_page[big_index] += read_pages;

	      first_run = sort_param->file_contents[big_index].first_run;
	      sort_param->file_contents[big_index].num_pages[first_run] -= read_pages;

	      /* Initialize input variables */
	      in_cur_bufaddr[i] = in_sectaddr[i];
	      in_act_bufno[i] = 0;
	      in_last_buf[i] = read_pages;
	      act_slot[i] = 0;
	      last_slot[i] = sort_spage_get_numrecs (in_cur_bufaddr[i]);

	      if (sort_spage_get_record (in_cur_bufaddr[i], act_slot[i], &smallest_elem_ptr[i], PEEK) != S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[i].type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &smallest_elem_ptr[i], &long_recdes[i]) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}
	    }

	  /* linkage & init nodes */
	  for (i = 0, p = sr_list; i < (act_infiles - 1); p = p->next)
	    {
	      p->next = (SORT_REC_LIST *) ((char *) p + sizeof (SORT_REC_LIST));
	      p->rec_pos = i++;
	      p->is_duplicated = false;
	    }

	  p->next = NULL;
	  p->rec_pos = i;
	  p->is_duplicated = false;

	  /* sort sr_list */
	  for (s = sr_list; s; s = s->next)
	    {
	      for (p = s->next; p; p = p->next)
		{
		  /* compare s, p */
		  data1 = ((smallest_elem_ptr[s->rec_pos].type == REC_BIGONE)
			   ? &(long_recdes[s->rec_pos].data) : &(smallest_elem_ptr[s->rec_pos].data));

		  data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
			   ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

		  cmp = (*compare) (data1, data2, compare_arg);
		  if (cmp > 0)
		    {
		      /* swap s, p's rec_pos */
		      tmp_var = s->rec_pos;
		      s->rec_pos = p->rec_pos;
		      p->rec_pos = tmp_var;
		    }
		}
	    }

	  /* find duplicate */
	  for (s = sr_list; s && s->next; s = s->next)
	    {
	      p = s->next;

	      data1 = ((smallest_elem_ptr[s->rec_pos].type == REC_BIGONE)
		       ? &(long_recdes[s->rec_pos].data) : &(smallest_elem_ptr[s->rec_pos].data));

	      data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
		       ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

	      cmp = (*compare) (data1, data2, compare_arg);
	      if (cmp == 0)
		{
		  p->is_duplicated = true;
		}
	    }

	  /* set min_p to point the minimum record */
	  min_p = sr_list;	/* min_p->rec_pos is the min record */

	  /* last element comparison */
	  last_elem_cmp = 1;
	  p = min_p->next;	/* second smallest element */

	  if (p)
	    {
	      /* STEP 1: get last_elem */
	      if (sort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
					 &last_elem_ptr, PEEK) != S_SUCCESS)
		{
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  goto bailout;
		}

	      /* if this is a long record, retrieve it */
	      if (last_elem_ptr.type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}

	      /* STEP 2: compare last, p */
	      data1 = ((last_elem_ptr.type == REC_BIGONE) ? &(last_long_recdes.data) : &(last_elem_ptr.data));

	      data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
		       ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

	      last_elem_cmp = (*compare) (data1, data2, compare_arg);
	    }

	  /* INITIALIZE OUTPUT SECTION AND OUTPUT VARIABLES */
	  out_act_bufno = 0;
	  out_cur_bufaddr = out_sectaddr;
	  for (i = 0; i < out_sectsize; i++)
	    {
	      /* Initialize each buffer to contain a slotted page */
	      sort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE), UNANCHORED_KEEP_SEQUENCE, MAX_ALIGNMENT);
	    }

	  /* Initialize the size of next run to zero */
	  out_runsize = 0;

	  /* In parallel sort, put_fn will be performed by the parent thread. save last file index. */
	  if (very_last_run && IS_PARALLEL_EXECUTION (sort_param))
	    {
	      sort_param->px_result_file_idx = cur_outfile;
	    }

	  for (;;)
	    {
	      /* OUTPUT A RECORD */

	      /* FIND MINIMUM RECORD IN THE INPUT AREA */
	      min = min_p->rec_pos;

	      /* min_p->is_duplicated == 1 then skip duplicated sort_key record */
	      if (min_p->is_duplicated == false)
		{
		  /* we found first unique sort_key record */

		  if (very_last_run && !IS_PARALLEL_EXECUTION (sort_param))
		    {
		      /* OUTPUT THE RECORD */
		      /* Obtain the output record for this temporary record */
		      if (smallest_elem_ptr[min].type == REC_BIGONE)
			{
			  error = (*sort_param->put_fn) (thread_p, &long_recdes[min], sort_param->put_arg);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }
			}
		      else
			{
			  sort_rec = (SORT_REC *) (smallest_elem_ptr[min].data);
			  /* cut-off link used in Internal Sort */
			  sort_rec->next = NULL;
			  error = (*sort_param->put_fn) (thread_p, &smallest_elem_ptr[min], sort_param->put_arg);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }
			}
		    }
		  else
		    {
		      /* OUTPUT THE MINIMUM RECORD TO THE OUTPUT AREA */

		      /* Insert this record to the output area */
		      if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
			{
			  /* Current output buffer is full */

			  if (++out_act_bufno < out_sectsize)
			    {
			      /* There is another buffer in the output section; so insert the new record there */
			      out_cur_bufaddr += DB_PAGESIZE;

			      if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
				{
				  /*
				   * Slotted page module refuses to insert a
				   * short size record (a temporary record that
				   * was already in a slotted page) to an empty
				   * page. This should never happen.
				   */
				  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
				  error = ER_GENERIC_ERROR;
				  goto bailout;
				}
			    }
			  else
			    {
			      /* Output section is full */

			      /* Flush output section */
			      error =
				sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile],
						 out_sectsize, out_sectaddr, sort_param->tde_encrypted);
			      if (error != NO_ERROR)
				{
				  goto bailout;
				}
			      cur_page[cur_outfile] += out_sectsize;
			      out_runsize += out_sectsize;

			      /* Initialize output section and output variables */
			      out_act_bufno = 0;
			      out_cur_bufaddr = out_sectaddr;
			      for (i = 0; i < out_sectsize; i++)
				{
				  /* Initialize each buffer to contain a slotted page */
				  sort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE), UNANCHORED_KEEP_SEQUENCE,
							 MAX_ALIGNMENT);
				}

			      if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
				{
				  /*
				   * Slotted page module refuses to insert a
				   * short size record (a temporary record that
				   * was already in a slotted page) to an empty
				   * page. This should never happen.
				   */
				  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
				  error = ER_GENERIC_ERROR;
				  goto bailout;
				}
			    }
			}
		    }
		}
	      else
		{
		  /* skip output the duplicated record to the output area */
		  ;
		}

	      /* PROCEED THE smallest_elem_ptr[min] TO NEXT RECORD */
	      if (++act_slot[min] >= last_slot[min])
		{
		  /* The current input page is finished */

		  last_elem_cmp = 1;

		  if (++in_act_bufno[min] < in_last_buf[min])
		    {
		      /* Switch to the next page in the input buffer */
		      in_cur_bufaddr[min] = in_sectaddr[min] + in_act_bufno[min] * DB_PAGESIZE;
		    }
		  else
		    {		/* The input section is finished */
		      int frun;

		      big_index = sort_param->in_half + min;
		      frun = sort_param->file_contents[big_index].first_run;

		      if (sort_param->file_contents[big_index].num_pages[frun])
			{
			  /* There are still some pages in the current input run */

			  in_cur_bufaddr[min] = in_sectaddr[min];

			  read_pages = sort_param->file_contents[big_index].num_pages[frun];
			  if (in_sectsize < read_pages)
			    {
			      read_pages = in_sectsize;
			    }

			  in_last_buf[min] = read_pages;

			  error = sort_read_area (thread_p, &sort_param->temp[big_index], cur_page[big_index],
						  read_pages, in_cur_bufaddr[min]);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  /* Increment the current page of this input_file */
			  cur_page[big_index] += read_pages;

			  in_act_bufno[min] = 0;

			  frun = sort_param->file_contents[big_index].first_run;
			  sort_param->file_contents[big_index].num_pages[frun] -= read_pages;
			}
		      else
			{
			  /* Current input run on this input file has finished */
			  min_p = min_p->next;

			  if (min_p == NULL)
			    {
			      /* all "smallest_elem_ptr" are NULL; so break */
			      break;
			    }
			  else
			    {
			      /* Don't try to get the next record on this input section */
			      continue;
			    }
			}
		    }

		  act_slot[min] = 0;
		  last_slot[min] = sort_spage_get_numrecs (in_cur_bufaddr[min]);
		}

	      if (sort_spage_get_record (in_cur_bufaddr[min], act_slot[min], &smallest_elem_ptr[min], PEEK) !=
		  S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[min].type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &smallest_elem_ptr[min], &long_recdes[min]) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}

	      if ((act_slot[min_p->rec_pos] == last_slot[min_p->rec_pos] - 1) && (last_elem_cmp == 0))
		{
		  /* last duplicated element in input section page enters */
		  min_p->is_duplicated = true;
		}
	      else
		{
		  min_p->is_duplicated = false;
		}

	      /* find minimum */
	      if (last_elem_cmp <= 0)
		{
		  /* already found min */
		  ;
		}
	      else
		{
		  for (s = min_p; s; s = s->next)
		    {
		      p = s->next;
		      if (p == NULL)
			{
			  /* there is only one record */
			  break;
			}

		      /* compare s, p */
		      data1 = ((smallest_elem_ptr[s->rec_pos].type == REC_BIGONE)
			       ? &(long_recdes[s->rec_pos].data) : &(smallest_elem_ptr[s->rec_pos].data));

		      data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
			       ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

		      cmp = (*compare) (data1, data2, compare_arg);
		      if (cmp > 0)
			{
			  /* swap s, p's rec_pos */
			  tmp_var = s->rec_pos;
			  s->rec_pos = p->rec_pos;
			  p->rec_pos = tmp_var;

			  /* swap s, p's is_duplicated */
			  tmp_var = (int) s->is_duplicated;
			  s->is_duplicated = p->is_duplicated;
			  p->is_duplicated = (bool) tmp_var;
			}
		      else
			{
			  if (cmp == 0)
			    {
			      p->is_duplicated = true;	/* duplicated */
			    }

			  /* sr_list is completely sorted */
			  break;
			}
		    }

		  /* new input page is entered */
		  if (act_slot[min_p->rec_pos] == 0)
		    {
		      /* last element comparison */
		      p = min_p->next;	/* second smallest element */
		      if (p)
			{
			  /* STEP 1: get last_elem */
			  if (sort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
						     &last_elem_ptr, PEEK) != S_SUCCESS)
			    {
			      error = ER_SORT_TEMP_PAGE_CORRUPTED;
			      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			      goto bailout;
			    }

			  /* if this is a long record, retrieve it */
			  if (last_elem_ptr.type == REC_BIGONE)
			    {
			      if (sort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
				{
				  ASSERT_ERROR ();
				  error = er_errid ();
				  goto bailout;
				}
			    }

			  /* STEP 2: compare last, p */
			  data1 = ((last_elem_ptr.type == REC_BIGONE)
				   ? &(last_long_recdes.data) : &(last_elem_ptr.data));

			  data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
				   ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

			  last_elem_cmp = (*compare) (data1, data2, compare_arg);
			}
		    }
		}
	    }

	  if (!(very_last_run && !IS_PARALLEL_EXECUTION (sort_param)))
	    {
	      /* Flush whatever is left on the output section */
	      out_act_bufno++;	/* Since 0 refers to the first active buffer */

	      error =
		sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile], out_act_bufno,
				 out_sectaddr, sort_param->tde_encrypted);
	      if (error != NO_ERROR)
		{
		  goto bailout;
		}

	      cur_page[cur_outfile] += out_act_bufno;
	      out_runsize += out_act_bufno;
	    }

	  /* END UP THIS RUN */

	  /* Remove previous first_run nodes of the file_contents lists of the input files */
	  for (i = sort_param->in_half; i < sort_param->in_half + sort_param->half_files; i++)
	    {
	      sort_run_remove_first (&sort_param->file_contents[i]);
	    }

	  /* Add a new node to the file_contents list of the current output file */
	  error = sort_run_add_new (&sort_param->file_contents[cur_outfile], out_runsize);
	  if (error != NO_ERROR)
	    {
	      goto bailout;
	    }

	  /* Produce a new run */

	  /* Switch to the next out file */
	  if (++cur_outfile >= sort_param->half_files + out_half)
	    {
	      cur_outfile = out_half;
	    }
	}

      /* Exchange input and output file indices */
      temp = sort_param->in_half;
      sort_param->in_half = out_half;
      out_half = temp;
    }

bailout:

  for (i = 0; i < sort_param->half_files; i++)
    {
      if (long_recdes[i].data != NULL)
	{
	  free_and_init (long_recdes[i].data);
	}
    }

  if (last_long_recdes.data)
    {
      free_and_init (last_long_recdes.data);
    }

  return (error == SORT_PUT_STOP) ? NO_ERROR : error;
}

/*
 * sort_put_result_from_tmpfile () - put result from last temp file
 *   return:
 *   sort_param(in): sort parameters
 *
 */
static int
sort_put_result_from_tmpfile (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param)
{
  int tot_pages;
  int current_pages = 0, read_pages = 0, cur_read_pages = 0;
  int slot_num = 0;
  char *cur_pgptr;
  int result_file_idx = sort_param->px_result_file_idx;
  RECDES record = RECDES_INITIALIZER;
  RECDES long_record = RECDES_INITIALIZER;
  int error = NO_ERROR;
  SORT_REC *sort_rec;
  int tot_rows = 0;

  tot_pages = sort_param->file_contents[result_file_idx].num_pages[0];
  while (tot_pages > 0)
    {
      read_pages = (tot_pages > sort_param->tot_buffers) ? sort_param->tot_buffers : tot_pages;

      error =
	sort_read_area (thread_p, &sort_param->temp[result_file_idx], current_pages, read_pages,
			sort_param->internal_memory);
      if (error != NO_ERROR)
	{
	  goto bailout;
	}

      cur_pgptr = sort_param->internal_memory;
      cur_read_pages = read_pages;
      while (cur_read_pages > 0)
	{
	  /* read record from sort temp file */
	  slot_num = sort_spage_get_numrecs (cur_pgptr);
	  tot_rows += slot_num;
	  for (int i = 0; i < slot_num; i++)
	    {
	      if (sort_spage_get_record (cur_pgptr, i, &record, PEEK) != S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (record.type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &record, &long_record) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}
	      /* write data by put_fn */
	      if (record.type == REC_BIGONE)
		{
		  error = (*sort_param->put_fn) (thread_p, &long_record, sort_param->put_arg);
		  if (error != NO_ERROR)
		    {
		      goto bailout;
		    }
		}
	      else
		{
		  sort_rec = (SORT_REC *) (record.data);
		  /* cut-off link used in Internal Sort */
		  sort_rec->next = NULL;
		  error = (*sort_param->put_fn) (thread_p, &record, sort_param->put_arg);
		  if (error != NO_ERROR)
		    {
		      goto bailout;
		    }
		}
	    }

	  /* Switch to the next page */
	  cur_pgptr += DB_PAGESIZE;

	  cur_read_pages--;
	  current_pages++;
	}
      tot_pages -= read_pages;
    }

bailout:
  return error;
}

/*
 * sort_exphase_merge () - Merge phase
 *   return:
 *   sort_param(in): sort parameters
 *
 */
static int
sort_exphase_merge (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param)
{
  /* Variables for input files */
  int act_infiles;		/* How many of input files are active */
  int pre_act_infiles;		/* Number of active input files in the previous iteration */
  int in_sectsize;		/* Size of section allocated to each active input file (in terms of number of buffers
				 * it contains) */
  int read_pages;		/* Number of pages read in to fill the input buffer */
  int in_act_bufno[SORT_MAX_HALF_FILES];	/* Active buffer in the input section */
  int in_last_buf[SORT_MAX_HALF_FILES];	/* Last full buffer of the input section */
  int act_slot[SORT_MAX_HALF_FILES];	/* Active slot of the active buffer of input section */
  int last_slot[SORT_MAX_HALF_FILES];	/* Last slot of the active buffer of the input section */

  char *in_sectaddr[SORT_MAX_HALF_FILES];	/* Beginning address of each input section */
  char *in_cur_bufaddr[SORT_MAX_HALF_FILES];	/* Address of the current buffer in each input section */

  /* Variables for output file */
  int out_half;			/* Which half of temp files is for output */
  int cur_outfile;		/* Index for output file recieving new run */
  int out_sectsize;		/* Size of the output section (in terms of number of buffer it contains) */
  int out_act_bufno;		/* Active buffer in the output section */
  int out_runsize;		/* Total pages output for the run being produced */
  char *out_sectaddr;		/* Beginning address of the output section */
  char *out_cur_bufaddr;	/* Address of the current buffer in the output section */

  /* Smallest element pointers (one for each active input file) pointing to the active temp records. If the input file
   * becomes inactive (all input is exhausted), its smallest element pointer is set to NULL */
  RECDES smallest_elem_ptr[SORT_MAX_HALF_FILES];
  RECDES long_recdes[SORT_MAX_HALF_FILES];

  int cur_page[2 * SORT_MAX_HALF_FILES];	/* Current page of each temp file */
  int num_runs;			/* Number of output runs to be produced in this stage of the merging phase; */
  int big_index;
  int error;
  int i, j;
  int temp;
  int min;
  int len;
  bool very_last_run = false;
  int act;
  int cp_pages;

  SORT_CMP_FUNC *compare;
  void *compare_arg;

  SORT_REC_LIST sr_list[SORT_MAX_HALF_FILES], *min_p, *s, *p;
  int tmp_pos;			/* temporary value for rec_pos swapping */
  bool do_swap;			/* rec_pos swapping indicator */

  RECDES last_elem_ptr;		/* last element pointers in one page of input section */
  RECDES last_long_recdes;
  bool last_elem_is_min;	/* false: must find min record true: last element in the current input section is min
				 * record. no need to find min */
  char **data1, **data2;
  SORT_REC *sort_rec;
  int first_run;
  int cmp;

  error = NO_ERROR;

  compare = sort_param->cmp_fn;
  compare_arg = sort_param->cmp_arg;

  for (i = 0; i < SORT_MAX_HALF_FILES; i++)
    {
      in_act_bufno[i] = 0;
      in_last_buf[i] = 0;
      act_slot[i] = 0;
      last_slot[i] = 0;
      in_sectaddr[i] = NULL;
      in_cur_bufaddr[i] = NULL;

      smallest_elem_ptr[i].data = NULL;
      smallest_elem_ptr[i].area_size = 0;

      long_recdes[i].data = NULL;
      long_recdes[i].area_size = 0;
    }

  last_elem_ptr.data = NULL;
  last_elem_ptr.area_size = 0;

  last_long_recdes.data = NULL;
  last_long_recdes.area_size = 0;

  for (i = 0; i < (int) DIM (cur_page); i++)
    {
      cur_page[i] = 0;
    }

  if (sort_param->in_half == 0)
    {
      out_half = sort_param->half_files;
    }
  else
    {
      out_half = 0;
    }

  /* for one temporary file, put result from the temp file instead of merging it. */
  if (!IS_PARALLEL_EXECUTION (sort_param) && sort_get_numpages_of_active_infiles (sort_param) == 1)
    {
      error = sort_put_result_from_tmpfile (thread_p, sort_param);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto bailout;
	}
    }

  /* OUTER LOOP */

  /* While there are more than one input files with different runs to merge */
  while ((act_infiles = sort_get_numpages_of_active_infiles (sort_param)) > 1)
    {
      /* Check if output files has enough pages; if not allocate new pages */
      error = sort_checkalloc_numpages_of_outfiles (thread_p, sort_param);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto bailout;
	}

      /* Initialize the current pages of all temp files to 0 */
      for (i = 0; i < sort_param->tot_tempfiles; i++)
	{
	  cur_page[i] = 0;
	}

      /* Distribute the internal memory to the input and output sections */
      in_sectsize = sort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
      out_sectsize = sort_param->tot_buffers - in_sectsize * act_infiles;

      /* Set the address of each input section */
      for (i = 0; i < act_infiles; i++)
	{
	  in_sectaddr[i] = sort_param->internal_memory + (i * in_sectsize * DB_PAGESIZE);
	}

      /* Set the address of output section */
      out_sectaddr = sort_param->internal_memory + (act_infiles * in_sectsize * DB_PAGESIZE);

      cur_outfile = out_half;

      /* Find how many runs will be produced in this iteration */
      num_runs = 0;
      for (i = sort_param->in_half; i < sort_param->in_half + act_infiles; i++)
	{
	  len = sort_get_num_file_contents (&sort_param->file_contents[i]);
	  if (len > num_runs)
	    {
	      num_runs = len;
	    }
	}

      if (num_runs == 1)
	{
	  very_last_run = true;
	}

      /* PRODUCE RUNS */

      for (j = num_runs; j > 0; j--)
	{
	  if (!very_last_run && (j == 1))
	    {
	      /* LAST RUN OF THIS ITERATION */

	      /* Last iteration of the outer loop ; some of the input files might have become empty. */

	      pre_act_infiles = act_infiles;
	      act_infiles = sort_get_numpages_of_active_infiles (sort_param);
	      if (act_infiles != pre_act_infiles)
		{
		  /* Some of the active input files became inactive */

		  if (act_infiles == 1)
		    {
		      /* ONE ACTIVE INFILE */

		      /*
		       * There is only one active input file (i.e. there is
		       * only one input run to produce the output run). So,
		       * there is no need to perform the merging actions. All
		       * needed is to copy this input run to the current output
		       * file.
		       */
		      act = -1;

		      /* Find which input file contains this last input run */
		      for (i = sort_param->in_half; i < (sort_param->in_half + pre_act_infiles); i++)
			{
			  if (sort_param->file_contents[i].first_run != -1)
			    {
			      act = i;
			      break;
			    }
			}

		      if (act == -1)
			{
			  goto bailout;
			}

		      first_run = sort_param->file_contents[act].first_run;
		      cp_pages = sort_param->file_contents[act].num_pages[first_run];

		      error = sort_run_add_new (&sort_param->file_contents[cur_outfile], cp_pages);
		      if (error != NO_ERROR)
			{
			  goto bailout;
			}
		      sort_run_remove_first (&sort_param->file_contents[act]);

		      /* Use the whole internal_memory area as both the input and output buffer areas. */
		      while (cp_pages > 0)
			{
			  if (cp_pages > sort_param->tot_buffers)
			    {
			      read_pages = sort_param->tot_buffers;
			    }
			  else
			    {
			      read_pages = cp_pages;
			    }

			  error =
			    sort_read_area (thread_p, &sort_param->temp[act], cur_page[act], read_pages,
					    sort_param->internal_memory);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  cur_page[act] += read_pages;
			  error =
			    sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile],
					     read_pages, sort_param->internal_memory, sort_param->tde_encrypted);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  cur_page[cur_outfile] += read_pages;
			  cp_pages -= read_pages;
			}

		      /* Skip the remaining operations of the PRODUCE RUNS loop */
		      continue;
		    }
		  else
		    {
		      /* There are more than one active input files; redistribute buffers */
		      in_sectsize = sort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
		      out_sectsize = sort_param->tot_buffers - in_sectsize * act_infiles;

		      /* Set the address of each input section */
		      for (i = 0; i < act_infiles; i++)
			{
			  in_sectaddr[i] = sort_param->internal_memory + (i * in_sectsize * DB_PAGESIZE);
			}

		      /* Set the address of output section */
		      out_sectaddr = sort_param->internal_memory + (act_infiles * in_sectsize * DB_PAGESIZE);
		    }
		}
	    }

	  /* PRODUCE A NEW RUN */

	  /* INITIALIZE INPUT SECTIONS AND INPUT VARIABLES */
	  for (i = 0; i < act_infiles; i++)
	    {
	      big_index = sort_param->in_half + i;
	      first_run = sort_param->file_contents[big_index].first_run;
	      read_pages = sort_param->file_contents[big_index].num_pages[first_run];

	      if (in_sectsize < read_pages)
		{
		  read_pages = in_sectsize;
		}

	      error =
		sort_read_area (thread_p, &sort_param->temp[big_index], cur_page[big_index], read_pages,
				in_sectaddr[i]);
	      if (error != NO_ERROR)
		{
		  goto bailout;
		}

	      /* Increment the current page of this input_file */
	      cur_page[big_index] += read_pages;

	      first_run = sort_param->file_contents[big_index].first_run;
	      sort_param->file_contents[big_index].num_pages[first_run] -= read_pages;

	      /* Initialize input variables */
	      in_cur_bufaddr[i] = in_sectaddr[i];
	      in_act_bufno[i] = 0;
	      in_last_buf[i] = read_pages;
	      act_slot[i] = 0;
	      last_slot[i] = sort_spage_get_numrecs (in_cur_bufaddr[i]);

	      if (sort_spage_get_record (in_cur_bufaddr[i], act_slot[i], &smallest_elem_ptr[i], PEEK) != S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[i].type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &smallest_elem_ptr[i], &long_recdes[i]) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}
	    }

	  for (i = 0, p = sr_list; i < (act_infiles - 1); p = p->next)
	    {
	      p->next = (SORT_REC_LIST *) ((char *) p + sizeof (SORT_REC_LIST));
	      p->rec_pos = i++;
	    }
	  p->next = NULL;
	  p->rec_pos = i;

	  for (s = sr_list; s; s = s->next)
	    {
	      for (p = s->next; p; p = p->next)
		{
		  do_swap = false;

		  data1 = ((smallest_elem_ptr[s->rec_pos].type == REC_BIGONE)
			   ? &(long_recdes[s->rec_pos].data) : &(smallest_elem_ptr[s->rec_pos].data));

		  data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
			   ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

		  cmp = (*compare) (data1, data2, compare_arg);
		  if (cmp > 0)
		    {
		      do_swap = true;
		    }

		  if (do_swap)
		    {
		      tmp_pos = s->rec_pos;
		      s->rec_pos = p->rec_pos;
		      p->rec_pos = tmp_pos;
		    }
		}
	    }

	  /* set min_p to point minimum record */
	  min_p = sr_list;	/* min_p->rec_pos is min record */

	  /* last element comparison */
	  last_elem_is_min = false;
	  p = min_p->next;	/* second smallest element */

	  if (p)
	    {
	      /* STEP 1: get last_elem */
	      if (sort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
					 &last_elem_ptr, PEEK) != S_SUCCESS)
		{
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  goto bailout;
		}

	      /* if this is a long record then retrieve it */
	      if (last_elem_ptr.type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}

	      /* STEP 2: compare last, p */
	      data1 = ((last_elem_ptr.type == REC_BIGONE) ? &(last_long_recdes.data) : &(last_elem_ptr.data));

	      data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
		       ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

	      cmp = (*compare) (data1, data2, compare_arg);
	      if (cmp <= 0)
		{
		  last_elem_is_min = true;
		}
	    }

	  /* INITIALIZE OUTPUT SECTION AND OUTPUT VARIABLES */
	  out_act_bufno = 0;
	  out_cur_bufaddr = out_sectaddr;
	  for (i = 0; i < out_sectsize; i++)
	    {
	      /* Initialize each buffer to contain a slotted page */
	      sort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE), UNANCHORED_KEEP_SEQUENCE, MAX_ALIGNMENT);
	    }

	  /* Initialize the size of next run to zero */
	  out_runsize = 0;

	  /* In parallel sort, put_fn will be performed by the parent thread. save last file index. */
	  if (very_last_run && IS_PARALLEL_EXECUTION (sort_param))
	    {
	      sort_param->px_result_file_idx = cur_outfile;
	    }

	  for (;;)
	    {
	      /* OUTPUT A RECORD */

	      /* FIND MINIMUM RECORD IN THE INPUT AREA */
	      min = min_p->rec_pos;

	      if (very_last_run && !IS_PARALLEL_EXECUTION (sort_param))
		{
		  /* OUTPUT THE RECORD */
		  /* Obtain the output record for this temporary record */
		  if (smallest_elem_ptr[min].type == REC_BIGONE)
		    {
		      error = (*sort_param->put_fn) (thread_p, &long_recdes[min], sort_param->put_arg);
		      if (error != NO_ERROR)
			{
			  goto bailout;
			}
		    }
		  else
		    {
		      sort_rec = (SORT_REC *) (smallest_elem_ptr[min].data);
		      /* cut-off link used in Internal Sort */
		      sort_rec->next = NULL;
		      error = (*sort_param->put_fn) (thread_p, &smallest_elem_ptr[min], sort_param->put_arg);
		      if (error != NO_ERROR)
			{
			  goto bailout;
			}
		    }
		}
	      else
		{
		  /* OUTPUT THE MINIMUM RECORD TO THE OUTPUT AREA */

		  /* Insert this record to the output area */
		  if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
		    {
		      /* Current output buffer is full */

		      if (++out_act_bufno < out_sectsize)
			{
			  /* There is another buffer in the output section; so insert the new record there */
			  out_cur_bufaddr += DB_PAGESIZE;

			  if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
			    {
			      /*
			       * Slotted page module refuses to insert a short
			       * size record (a temporary record that was
			       * already in a slotted page) to an empty page.
			       * This should never happen.
			       */
			      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
			      error = ER_GENERIC_ERROR;
			      goto bailout;
			    }
			}
		      else
			{
			  /* Output section is full */
			  /* Flush output section */
			  error =
			    sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile],
					     out_sectsize, out_sectaddr, sort_param->tde_encrypted);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }
			  cur_page[cur_outfile] += out_sectsize;
			  out_runsize += out_sectsize;

			  /* Initialize output section and output variables */
			  out_act_bufno = 0;
			  out_cur_bufaddr = out_sectaddr;
			  for (i = 0; i < out_sectsize; i++)
			    {
			      /* Initialize each buffer to contain a slotted page */
			      sort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE), UNANCHORED_KEEP_SEQUENCE,
						     MAX_ALIGNMENT);
			    }

			  if (sort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
			    {
			      /*
			       * Slotted page module refuses to insert a short
			       * size record (a temporary record that was
			       * already in a slotted page) to an empty page.
			       * This should never happen.
			       */
			      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
			      error = ER_GENERIC_ERROR;
			      goto bailout;
			    }
			}
		    }
		}

	      /* PROCEED THE smallest_elem_ptr[min] TO NEXT RECORD */
	      if (++act_slot[min] >= last_slot[min])
		{
		  /* The current input page is finished */

		  last_elem_is_min = false;

		  if (++in_act_bufno[min] < in_last_buf[min])
		    {
		      /* Switch to the next page in the input buffer */
		      in_cur_bufaddr[min] = in_sectaddr[min] + in_act_bufno[min] * DB_PAGESIZE;
		    }
		  else
		    {
		      /* The input section is finished */
		      big_index = sort_param->in_half + min;
		      first_run = sort_param->file_contents[big_index].first_run;
		      if (sort_param->file_contents[big_index].num_pages[first_run])
			{
			  /* There are still some pages in the current input run */

			  in_cur_bufaddr[min] = in_sectaddr[min];

			  read_pages = sort_param->file_contents[big_index].num_pages[first_run];
			  if (in_sectsize < read_pages)
			    {
			      read_pages = in_sectsize;
			    }

			  in_last_buf[min] = read_pages;

			  error =
			    sort_read_area (thread_p, &sort_param->temp[big_index], cur_page[big_index], read_pages,
					    in_cur_bufaddr[min]);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  /* Increment the current page of this input_file */
			  cur_page[big_index] += read_pages;

			  in_act_bufno[min] = 0;
			  first_run = sort_param->file_contents[big_index].first_run;
			  sort_param->file_contents[big_index].num_pages[first_run] -= read_pages;
			}
		      else
			{
			  /* Current input run on this input file has finished */

			  /* remove current input run in input section. proceed to next minimum record. */
			  min_p = min_p->next;

			  if (min_p == NULL)
			    {
			      /* all "smallest_elem_ptr" are NULL; so break */
			      break;
			    }
			  else
			    {
			      /* Don't try to get the next record on this input section */
			      continue;
			    }
			}
		    }

		  act_slot[min] = 0;
		  last_slot[min] = sort_spage_get_numrecs (in_cur_bufaddr[min]);
		}

	      if (sort_spage_get_record (in_cur_bufaddr[min], act_slot[min], &smallest_elem_ptr[min], PEEK) !=
		  S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[min].type == REC_BIGONE)
		{
		  if (sort_retrieve_longrec (thread_p, &smallest_elem_ptr[min], &long_recdes[min]) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}

	      /* find minimum */
	      if (last_elem_is_min == true)
		{
		  /* already find min */
		  ;
		}
	      else
		{
		  for (s = min_p; s; s = s->next)
		    {
		      p = s->next;
		      if (p == NULL)
			{
			  /* there is only one record */
			  break;
			}

		      do_swap = false;

		      data1 = ((smallest_elem_ptr[s->rec_pos].type == REC_BIGONE)
			       ? &(long_recdes[s->rec_pos].data) : &(smallest_elem_ptr[s->rec_pos].data));

		      data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
			       ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

		      cmp = (*compare) (data1, data2, compare_arg);
		      if (cmp > 0)
			{
			  do_swap = true;
			}

		      if (do_swap)
			{
			  /* swap s, p */
			  tmp_pos = s->rec_pos;
			  s->rec_pos = p->rec_pos;
			  p->rec_pos = tmp_pos;
			}
		      else
			{
			  /* sr_list is completely sorted */
			  break;
			}
		    }

		  /* new input page is entered */
		  if (act_slot[min_p->rec_pos] == 0)
		    {
		      /* last element comparison */
		      p = min_p->next;	/* second smallest element */
		      if (p)
			{
			  /* STEP 1: get last_elem */
			  if (sort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
						     &last_elem_ptr, PEEK) != S_SUCCESS)
			    {
			      error = ER_SORT_TEMP_PAGE_CORRUPTED;
			      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			      goto bailout;
			    }

			  /* if this is a long record retrieve it */
			  if (last_elem_ptr.type == REC_BIGONE)
			    {
			      if (sort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
				{
				  ASSERT_ERROR ();
				  error = er_errid ();
				  goto bailout;
				}
			    }

			  /* STEP 2: compare last, p */
			  data1 =
			    ((last_elem_ptr.type == REC_BIGONE) ? &(last_long_recdes.data) : &(last_elem_ptr.data));

			  data2 = ((smallest_elem_ptr[p->rec_pos].type == REC_BIGONE)
				   ? &(long_recdes[p->rec_pos].data) : &(smallest_elem_ptr[p->rec_pos].data));

			  cmp = (*compare) (data1, data2, compare_arg);
			  if (cmp <= 0)
			    {
			      last_elem_is_min = true;
			    }
			}
		    }
		}
	    }

	  if (!(very_last_run && !IS_PARALLEL_EXECUTION (sort_param)))
	    {
	      /* Flush whatever is left on the output section */

	      out_act_bufno++;	/* Since 0 refers to the first active buffer */
	      error =
		sort_write_area (thread_p, &sort_param->temp[cur_outfile], cur_page[cur_outfile], out_act_bufno,
				 out_sectaddr, sort_param->tde_encrypted);
	      if (error != NO_ERROR)
		{
		  goto bailout;
		}
	      cur_page[cur_outfile] += out_act_bufno;
	      out_runsize += out_act_bufno;
	    }

	  /* END UP THIS RUN */

	  /* Remove previous first_run nodes of the file_contents lists of the input files */
	  for (i = sort_param->in_half; i < sort_param->in_half + sort_param->half_files; i++)
	    {
	      sort_run_remove_first (&sort_param->file_contents[i]);
	    }

	  /* Add a new node to the file_contents list of the current output file */
	  error = sort_run_add_new (&sort_param->file_contents[cur_outfile], out_runsize);
	  if (error != NO_ERROR)
	    {
	      goto bailout;
	    }

	  /* PRODUCE A NEW RUN */

	  /* Switch to the next out file */
	  if (++cur_outfile >= sort_param->half_files + out_half)
	    {
	      cur_outfile = out_half;
	    }
	}

      /* Exchange input and output file indices */
      temp = sort_param->in_half;
      sort_param->in_half = out_half;
      out_half = temp;
    }

bailout:

  for (i = 0; i < sort_param->half_files; i++)
    {
      if (long_recdes[i].data != NULL)
	{
	  free_and_init (long_recdes[i].data);
	}
    }

  if (last_long_recdes.data)
    {
      free_and_init (last_long_recdes.data);
    }

  return (error == SORT_PUT_STOP) ? NO_ERROR : error;
}

/* AUXILIARY FUNCTIONS */

/*
 * sort_get_avg_numpages_of_nonempty_tmpfile () - Return average number of pages
 *                                       currently occupied by nonempty
 *                                       temporary file
 *   return:
 *   sort_param(in): Sort paramater
 */
static int
sort_get_avg_numpages_of_nonempty_tmpfile (SORT_PARAM * sort_param)
{
  int f;
  int sum, i;
  int nonempty_temp_file_num = 0;

  sum = 0;
  for (i = 0; i < sort_param->tot_tempfiles; i++)
    {
      /* If the list is not empty */
      f = sort_param->file_contents[i].first_run;
      if (f > -1)
	{
	  nonempty_temp_file_num++;
	  for (; f <= sort_param->file_contents[i].last_run; f++)
	    {
	      sum += sort_param->file_contents[i].num_pages[f];
	    }
	}
    }

  return (sum / MAX (1, nonempty_temp_file_num));
}

/*
 * sort_return_used_resources () - Return system resource used for sorting
 *   return: void
 *   sort_param(in): Sort paramater
 *
 * Note: Clear the sort parameter structure by deallocating any allocated
 *       memory areas and destroying any temporary files and volumes.
 */
static void
sort_return_used_resources (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, PARALLEL_TYPE parallel_type)
{
  int k;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (sort_param == NULL)
    {
      return;			/* nop */
    }

  if (sort_param->internal_memory)
    {
      free_and_init (sort_param->internal_memory);
    }

  if (parallel_type == PX_SINGLE || parallel_type == PX_MAIN_IN_PARALLEL)
    {
      for (k = 0; k < sort_param->tot_tempfiles; k++)
	{
	  if (sort_param->temp[k].volid != NULL_VOLID)
	    {
	      (void) file_temp_retire_preserved (thread_p, &sort_param->temp[k]);
	    }
	}
    }

  if (sort_param->multipage_file.volid != NULL_VOLID)
    {
      (void) file_temp_retire_preserved (thread_p, &(sort_param->multipage_file));
    }

  if (parallel_type == PX_SINGLE || parallel_type == PX_THREAD_IN_PARALLEL)
    {
      for (k = 0; k < sort_param->tot_tempfiles; k++)
	{
	  if (sort_param->file_contents[k].num_pages != NULL)
	    {
	      free_and_init (sort_param->file_contents[k].num_pages);
	    }
	}
    }

  if (parallel_type == PX_THREAD_IN_PARALLEL)
    {
      for (int i = 0; i < sort_param->px_max_index; i++)
	{
	  if (sort_param->get_arg != NULL)
	    {
	      SORT_INFO *sort_info_p = (SORT_INFO *) sort_param->get_arg;
	      if (sort_info_p->s_id != NULL)
		{
		  db_private_free_and_init (thread_p, sort_info_p->s_id);
		}
	      if (sort_info_p->input_file != NULL)
		{
		  db_private_free_and_init (thread_p, sort_info_p->input_file);
		}
	      db_private_free_and_init (thread_p, sort_param->get_arg);
	    }
	}
    }

#if defined(SERVER_MODE)
  /* temporary disable */
  /*  rv = pthread_mutex_destroy (&(sort_param->px_mtx));
     if (rv != 0)
     {
     er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
     }
   */
#endif

  if (parallel_type == PX_SINGLE || parallel_type == PX_MAIN_IN_PARALLEL)
    {
      free_and_init (sort_param);
    }
}

/*
 * sort_add_new_file () - Create a new temporary file for sorting purposes
 *   return: NO_ERROR
 *   vfid(in): Set to the created file identifier
 *   file_pg_cnt_est(in): Estimated file page count
 *   force_alloc(in): Allocate file pages now ?
 *   tde_encrypted(in): whether the file has to be encrypted or not for TDE
 */
static int
sort_add_new_file (THREAD_ENTRY * thread_p, VFID * vfid, int file_pg_cnt_est, bool force_alloc, bool tde_encrypted)
{
  VPID new_vpid;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
  int ret = NO_ERROR;

  /* todo: sort file is a case I missed that seems to use file_find_nthpages. I don't know if it can be optimized to
   *       work without numerable files, that remains to be seen. */

  ret = file_create_temp_numerable (thread_p, file_pg_cnt_est, vfid);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }
  if (VFID_ISNULL (vfid))
    {
      assert_release (false);
      return ER_FAILED;
    }
  if (tde_encrypted)
    {
      tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
    }

  ret = file_apply_tde_algorithm (thread_p, vfid, tde_algo);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      file_temp_retire_preserved (thread_p, vfid);
      VFID_SET_NULL (vfid);
      return ret;
    }

  if (force_alloc == false)
    {
      return NO_ERROR;
    }

  /* page allocation force is specified, allocate pages for the file */
  /* todo: we don't have multiple page allocation, but allocation should be fast enough */
  for (; file_pg_cnt_est > 0; file_pg_cnt_est--)
    {
      ret = file_alloc (thread_p, vfid, NULL, NULL, &new_vpid, NULL);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  file_temp_retire_preserved (thread_p, vfid);
	  VFID_SET_NULL (vfid);
	  return ret;
	}
    }

  return NO_ERROR;
}

/*
 * sort_copy_sort_param () - copy sort param from src_param to dest_param
 *   return: NO_ERROR
 *   dest_param(in):
 *   src_param(in):
 *   parallel_num(in):
 */
static int
sort_copy_sort_param (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param, int parallel_num)
{
  int error = NO_ERROR;
  int i, j;

  /* copy from origin sort param */
  for (i = 0; i < parallel_num; i++)
    {
      memcpy (&px_sort_param[i], sort_param, sizeof (SORT_PARAM));
    }

  /* init */
  for (i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].internal_memory = NULL;
      for (j = 0; j < px_sort_param[i].tot_tempfiles; j++)
	{
	  px_sort_param[i].file_contents[j].num_pages = NULL;
	}
    }

  /* alloc new memory */
  for (i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].internal_memory = (char *) malloc ((size_t) sort_param->tot_buffers * (size_t) DB_PAGESIZE);
      if (px_sort_param[i].internal_memory == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) (sort_param->tot_buffers * DB_PAGESIZE));
	  break;
	}
      for (j = 0; j < px_sort_param[i].tot_tempfiles; j++)
	{
	  /* Initilize file contents list */
	  px_sort_param[i].file_contents[j].num_pages = (int *) malloc (SORT_INITIAL_DYN_ARRAY_SIZE * sizeof (int));
	  if (px_sort_param[i].file_contents[j].num_pages == NULL)
	    {
	      sort_param->tot_tempfiles = j;
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      break;
	    }

	  px_sort_param[i].file_contents[j].num_slots = SORT_INITIAL_DYN_ARRAY_SIZE;
	  px_sort_param[i].file_contents[j].first_run = -1;
	  px_sort_param[i].file_contents[j].last_run = -1;
	}

      /* init px variable */
      px_sort_param[i].px_status = 0;
      px_sort_param[i].px_max_index = parallel_num;
      px_sort_param[i].px_index = i + 1;
      px_sort_param[i].px_result_file_idx = 0;
      /* Copy the parent's tran_index. */
      px_sort_param[i].px_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      /* init get_arg. it'll be copied in sort_split_input_temp_file(). TO_DO : put_arg?? */
      px_sort_param[i].get_arg = NULL;
    }

  if (error != NO_ERROR)
    {
      /* free memory */
      for (i = 0; i < parallel_num; i++)
	{
	  if (px_sort_param[i].internal_memory != NULL)
	    {
	      free_and_init (px_sort_param[i].internal_memory);
	    }
	  for (j = 0; j < px_sort_param[i].tot_tempfiles; j++)
	    {
	      if (px_sort_param[i].file_contents[j].num_pages != NULL)
		{
		  free_and_init (px_sort_param[i].file_contents[j].num_pages);
		}
	    }
	}
    }

  return error;
}

/*
 * sort_split_input_temp_file () - split input temp file
 *   return: NO_ERROR
 *   px_sort_param(in):
 *   sort_param(in):
 *   parallel_num(in):
 */
static int
sort_split_input_temp_file (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param,
			    int parallel_num)
{
  /* TO_DO : Need a logic to revert it? */
  int error = NO_ERROR;
  int i = 0, j = 0, splitted_num_page;
  bool is_first_vpid = false;
  PAGE_PTR page_p;
  VPID next_vpid, prev_vpid, first_vpid[SORT_MAX_PARALLEL], last_vpid[SORT_MAX_PARALLEL];
  QFILE_LIST_SCAN_ID *scan_id_p;
  SORT_INFO *sort_info_p, *org_sort_info_p;

  /* get scan id of input file */
  sort_info_p = (SORT_INFO *) sort_param->get_arg;
  scan_id_p = sort_info_p->s_id->s_id;

  /* close input file for read. it'll be opened in parallel */
  qfile_close_scan (thread_p, scan_id_p);

  /* init vpid */
  for (i = 0; i < parallel_num; i++)
    {
      first_vpid[i] = VPID_INITIALIZER;
      last_vpid[i] = VPID_INITIALIZER;
    }

  /* page_cnt contains ovfl_page. If the length of tuple is longer than page, split by the number of tuples. */
  splitted_num_page = sort_info_p->input_file->page_cnt / parallel_num;
  splitted_num_page = MIN (splitted_num_page, sort_info_p->input_file->tuple_cnt / parallel_num);

  /* find first and last vpid for splitted file */
  i = 0;
  is_first_vpid = false;
  prev_vpid = sort_info_p->input_file->first_vpid;
  while (true)
    {
      page_p = qmgr_get_old_page (thread_p, &prev_vpid, sort_info_p->input_file->tfile_vfid);
      if (is_first_vpid)
	{
	  QFILE_PUT_PREV_VPID_NULL (page_p);
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	  is_first_vpid = false;
	}
      QFILE_GET_NEXT_VPID (&next_vpid, page_p);
      if (VPID_ISNULL (&next_vpid))
	{
	  break;
	}
      else if (++j >= splitted_num_page)
	{
	  QFILE_PUT_NEXT_VPID_NULL (page_p);
	  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	  is_first_vpid = true;
	  first_vpid[i] = next_vpid;
	  last_vpid[i] = prev_vpid;
	  i++;

	  if (i < parallel_num - 1)
	    {
	      j = 0;
	    }
	  else
	    {
	      qmgr_free_old_page_and_init (thread_p, page_p, sort_info_p->input_file->tfile_vfid);

	      /* init prev page id */
	      page_p = qmgr_get_old_page (thread_p, &next_vpid, sort_info_p->input_file->tfile_vfid);
	      QFILE_PUT_PREV_VPID_NULL (page_p);
	      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
	      qmgr_free_old_page_and_init (thread_p, page_p, sort_info_p->input_file->tfile_vfid);
	      break;
	    }
	}
      prev_vpid = next_vpid;
      qmgr_free_old_page_and_init (thread_p, page_p, sort_info_p->input_file->tfile_vfid);
    }

  /* add splitted file info */
  for (i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].get_arg = (void *) db_private_alloc (thread_p, sizeof (SORT_INFO));
      if (px_sort_param[i].get_arg == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
      sort_info_p = (SORT_INFO *) px_sort_param[i].get_arg;
      org_sort_info_p = (SORT_INFO *) sort_param->get_arg;
      memcpy (sort_info_p, org_sort_info_p, sizeof (SORT_INFO));

      sort_info_p->input_file = NULL;
      sort_info_p->s_id = NULL;

      sort_info_p->s_id = (QFILE_SORT_SCAN_ID *) db_private_alloc (thread_p, sizeof (QFILE_SORT_SCAN_ID));
      if (sort_info_p->s_id == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
      memcpy (sort_info_p->s_id, org_sort_info_p->s_id, sizeof (QFILE_SORT_SCAN_ID));

      sort_info_p->input_file = (QFILE_LIST_ID *) db_private_alloc (thread_p, sizeof (QFILE_LIST_ID));
      if (sort_info_p->input_file == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
      memcpy (sort_info_p->input_file, org_sort_info_p->input_file, sizeof (QFILE_LIST_ID));
      /* tuple_cnt and page_cnt put approximately. */
      /* It can be put precisely through the page header, but not have to be precise for later process. */
      sort_info_p->input_file->tuple_cnt = org_sort_info_p->input_file->tuple_cnt / parallel_num;
      sort_info_p->input_file->page_cnt = splitted_num_page;
      sort_info_p->input_file->first_vpid = (i == 0) ? org_sort_info_p->input_file->first_vpid : first_vpid[i - 1];
      sort_info_p->input_file->last_vpid =
	(i == parallel_num - 1) ? org_sort_info_p->input_file->last_vpid : last_vpid[i];
    }

cleanup:
  if (error != NO_ERROR)
    {
      /* free memory */
      for (i = 0; i < parallel_num; i++)
	{
	  if (px_sort_param[i].get_arg != NULL)
	    {
	      sort_info_p = (SORT_INFO *) px_sort_param[i].get_arg;
	      if (sort_info_p->s_id != NULL)
		{
		  db_private_free_and_init (thread_p, sort_info_p->s_id);
		}
	      if (sort_info_p->input_file != NULL)
		{
		  db_private_free_and_init (thread_p, sort_info_p->input_file);
		}
	      db_private_free_and_init (thread_p, px_sort_param[i].get_arg);
	    }
	}
    }

  return error;
}

/*
 * sort_merge_run_for_parallel () - merge run for parallel
 *   return: NO_ERROR
 *   px_sort_param(in):
 *   sort_param(in):
 *   parallel_num(in):
 */
static int
sort_merge_run_for_parallel (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param,
			     int parallel_num)
{
  int error = NO_ERROR;
  int i = 0;

  /* TO_DO : Up to 4 in parallel. Expansion required. SORT_MAX_HALF_FILES */
  if (parallel_num > 4)
    {
      assert (0);
    }

  /* copy temp file from parallel to main */
  for (i = 0; i < parallel_num; i++)
    {
      /* free num_pages */
      free_and_init (sort_param->file_contents[i].num_pages);

      /* copy temp file and file contents */
      sort_param->temp[i] = px_sort_param[i].temp[px_sort_param[i].px_result_file_idx];
      sort_param->file_contents[i] = px_sort_param[i].file_contents[px_sort_param[i].px_result_file_idx];
    }

  /* init file info */
  sort_param->px_result_file_idx = 0;
  sort_param->half_files = parallel_num;
  sort_param->tot_tempfiles = parallel_num * 2;
  sort_param->in_half = 0;

  /* Create output temporary files make file and temporary volume page count estimates */
  int file_pg_cnt_est = sort_get_avg_numpages_of_nonempty_tmpfile (sort_param);
  file_pg_cnt_est = MAX (1, file_pg_cnt_est);

  for (i = sort_param->half_files; i < sort_param->tot_tempfiles; i++)
    {
      error = sort_add_new_file (thread_p, &(sort_param->temp[i]), file_pg_cnt_est, true, sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* Merge the parallel processed results. */
  sort_param->px_max_index = 1;
  if (sort_param->option == SORT_ELIM_DUP)
    {
      error = sort_exphase_merge_elim_dup (thread_p, sort_param);
    }
  else
    {
      /* SORT_DUP */
      error = sort_exphase_merge (thread_p, sort_param);
    }

  return error;
}

/*
 * sort_write_area () - Write memory area to disk
 *   return:
 *   vfid(in): file identifier to write the pages contained in the area
 *   first_page(in): first page to be written on the file
 *   num_pages(in): size of the memory area in terms of number of pages it
 *                  accommodates
 *   area_start(in): beginning address of the area
 *
 * Note: This function writes the contents of the given memory area to the
 *       specified file starting from the given page. Before doing so, however,
 *       it checks the size of the file and, if necessary, allocates new pages.
 *       If new pages are needed but the disk is full, an error code is
 *       returned.
 */
static int
sort_write_area (THREAD_ENTRY * thread_p, VFID * vfid, int first_page, INT32 num_pages, char *area_start,
		 bool tde_encrypted)
{
  PAGE_PTR page_ptr = NULL;
  VPID vpid;
  INT32 page_no;
  int i;
  int ret = NO_ERROR;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

  if (tde_encrypted)
    {
      ret = file_get_tde_algorithm (thread_p, vfid, PGBUF_UNCONDITIONAL_LATCH, &tde_algo);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  /* initializations */
  page_no = first_page;

  /* Flush pages buffered in the given area to the specified file */

  page_ptr = (PAGE_PTR) area_start;

  for (i = 0; i < num_pages; i++)
    {
      /* file is automatically expanded if page is not allocated (as long as it is missing only one page) */
      ret = file_numerable_find_nth (thread_p, vfid, page_no++, true, NULL, NULL, &vpid);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ret;
	}
      if (pgbuf_copy_from_area (thread_p, &vpid, 0, DB_PAGESIZE, page_ptr, true, tde_algo) == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  return ret;
	}

      page_ptr += DB_PAGESIZE;
    }

  return NO_ERROR;
}

/*
 * sort_read_area () - Read memory area from disk
 *   return:
 *   vfid(in): file identifier to read the pages from
 *   first_page(in): first page to be read from the file
 *   num_pages(in): size of the memory area in terms of number of pages it
 *                  accommodates
 *   area_start(in): beginning address of the area
 *
 * Note: This function reads in successive pages of the specified file into
 *       the given memory area until this area becomes full.
 */
static int
sort_read_area (THREAD_ENTRY * thread_p, VFID * vfid, int first_page, INT32 num_pages, char *area_start)
{
  PAGE_PTR page_ptr = NULL;
  VPID vpid;
  INT32 page_no;
  int i;
  int ret = NO_ERROR;

  vpid.volid = vfid->volid;
  page_no = first_page;

  /* Flush pages buffered in the given area to the specified file */

  page_ptr = (PAGE_PTR) area_start;

  for (i = 0; i < num_pages; i++)
    {
      ret = file_numerable_find_nth (thread_p, vfid, page_no++, false, NULL, NULL, &vpid);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ret;
	}
      if (pgbuf_copy_to_area (thread_p, &vpid, 0, DB_PAGESIZE, page_ptr, true) == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  return ret;
	}

      page_ptr += DB_PAGESIZE;
    }

  return NO_ERROR;
}

/*
 * sort_get_num_half_tmpfiles () - Determines the number of temporary files to be used
 *                        during the sorting process
 *   return:
 *   tot_buffers(in): total number of buffers in the buffer pool area
 *   input_pages(in): size of the input file in terms of number of pages it
 *                    occupies
 *
 */
static int
sort_get_num_half_tmpfiles (int tot_buffers, int input_pages)
{
  int half_files = tot_buffers - 1;
  int exp_num_runs;

  /* If there is an estimate on number of input pages */
  if (input_pages > 0)
    {
      /* Conservatively estimate number of runs that will be produced */
      exp_num_runs = CEIL_PTVDIV (input_pages, tot_buffers) + 1;

      if (exp_num_runs < half_files)
	{
	  if ((exp_num_runs > (tot_buffers / 2)))
	    {
	      half_files = exp_num_runs;
	    }
	  else
	    {
	      half_files = (tot_buffers / 2);
	    }
	}
    }

  /* Precaution against underestimation on exp_num_runs and having too few files to merge them */
  if (half_files < SORT_MIN_HALF_FILES)
    {
      return SORT_MIN_HALF_FILES;
    }

  if (half_files < SORT_MAX_HALF_FILES)
    {
      return half_files;
    }
  else
    {
      /* Precaution against saturation (i.e. having too many files) */
      return SORT_MAX_HALF_FILES;
    }
}

/*
 * sort_checkalloc_numpages_of_outfiles () - Check sizes of output files
 *   return: error code
 *   sort_param(in): sort parameters
 *
 * Note: This function determines how many pages will be needed by each output
 *       file of the current stage of the merging phase. This is done by going
 *       over the file_contents lists of the input files and determining how
 *       many pages they will eventually contribute to each output file.
 *       (Again, this is an estimate, not an exact number on the size of output
 *       files.) It then checks whether these output files have that many pages
 *       already. If some of them need more pages, it allocates new pages.
 */
static int
sort_checkalloc_numpages_of_outfiles (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param)
{
  int out_file;
  int out_half;
  int needed_pages[2 * SORT_MAX_HALF_FILES];
  int contains;
  int alloc_pages;
  int i, j;

  int error_code = NO_ERROR;

  for (i = 0; i < (int) DIM (needed_pages); i++)
    {
      needed_pages[i] = 0;
    }

  if (sort_param->in_half == 0)
    {
      out_half = sort_param->half_files;
    }
  else
    {
      out_half = 0;
    }

  /* Estimate the sizes of all new runs to be flushed on output files */
  for (i = sort_param->in_half; i < sort_param->in_half + sort_param->half_files; i++)
    {
      out_file = out_half;

      /* If the list is not empty */
      j = sort_param->file_contents[i].first_run;
      if (j > -1)
	{
	  for (; j <= sort_param->file_contents[i].last_run; j++)
	    {
	      needed_pages[out_file] += sort_param->file_contents[i].num_pages[j];

	      if (++out_file >= out_half + sort_param->half_files)
		{
		  out_file = out_half;
		}
	    }
	}
    }

  /* Allocate enough pages to each output file We don't initialize pages during allocation since we do not care the
   * state of the pages after a rollback or system crashes. Nothing need to be log on the page. The pages are
   * initialized at a later time. */

  /* Files are traversed in reverse order, in order to destroy unnecessary files first. It is expected that returned
   * pages will be reused by the next allocation. */
  for (i = out_half + sort_param->half_files - 1; i >= out_half; i--)
    {
      if (needed_pages[i] > 0)
	{
	  assert (!VFID_ISNULL (&sort_param->temp[i]));
	  error_code = file_get_num_user_pages (thread_p, &sort_param->temp[i], &contains);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  alloc_pages = (needed_pages[i] - contains);
	  if (alloc_pages > 0)
	    {
	      error_code = file_alloc_multiple (thread_p, &sort_param->temp[i], NULL, NULL, alloc_pages, NULL);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	    }
	}
      else
	{
	  /* If there is a file not to be used anymore, destroy it in order to reuse spaces. */
	  if (!VFID_ISNULL (&sort_param->temp[i]))
	    {
	      error_code = file_temp_retire_preserved (thread_p, &sort_param->temp[i]);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	      VFID_SET_NULL (&sort_param->temp[i]);
	    }
	}
    }
  return NO_ERROR;
}

/*
 * sort_get_numpages_of_active_infiles () - Find number of active input files
 *   return:
 *   sort_param(in): sort parameters
 *
 * Note: This function determines how many of the input files still
 *       have input runs (active) to participate in while the merging
 *       process which produces larger size runs. For this purpose,
 *       it checks the file_contents list of each input file. Once the
 *       first file with no remaining input runs (unactive) is found,
 *       it is concluded that all the remaining input temporary files
 *       are also inactive (because of balanced distribution of runs to
 *       the files).
 */
static int
sort_get_numpages_of_active_infiles (const SORT_PARAM * sort_param)
{
  int i;

  for (i = sort_param->in_half; i < sort_param->in_half + sort_param->half_files; i++)
    {
      if (sort_param->file_contents[i].first_run == -1)
	{
	  break;
	}
    }

  return (i - sort_param->in_half);
}

/*
 * sort_find_inbuf_size () - Distribute buffers
 *   return:
 *   tot_buffers(in): number of total buffers in the buffer pool area
 *   in_sections(in): number of input sections into which this buffer pool area
 *                    should be divided into (in other words, the number of
 *                    active input files)
 *
 * Note: This function distributes the buffers of the buffer pool area
 *       (i.e., the internal memory) among the active input files and
 *       the output file. Recall that each active input file and the
 *       output file will have a section in the buffer pool area.
 *       This function returns the size of each input section in terms
 *       of number of buffers it occupies. Naturally, the output
 *       section will have the remaining buffers.
 *
 *       Note that when the input runs are merged together the
 *       number of read operations is (roughly) equal to the
 *       number of write operations. For that reason this function
 *       reserves roughly half of the buffers for the output section
 *       and distributes the remaining ones evenly among the input
 *       sections, as each input run is approximately the same size.
 */
static int
sort_find_inbuf_size (int tot_buffers, int in_sections)
{
  int in_sectsize;

  /* Allocate half of the total buffers to output buffer area */
  in_sectsize = (tot_buffers / (in_sections << 1));
  if (in_sectsize != 0)
    {
      return in_sectsize;
    }
  else
    {
      return 1;
    }
}

/*
 * sort_run_add_new () - Adds a new node to the end of the given list
 *   return: NO_ERROR
 *   file_contents(in): which list to add
 *   num_pages(in): what value to put for the new run
 */
static int
sort_run_add_new (FILE_CONTENTS * file_contents, int num_pages)
{
  int new_total_elements;
  int ret = NO_ERROR;

  if (file_contents->first_run == -1)
    {
      /* This is an empty list */
      file_contents->first_run = 0;
      file_contents->last_run = 0;
    }
  else
    {
      file_contents->last_run++;
    }

  /* If there is no room in the dynamic array to keep the next element of the list; expand the dynamic array. */
  if (file_contents->last_run >= file_contents->num_slots)
    {
      new_total_elements = ((int) (((float) file_contents->num_slots * SORT_EXPAND_DYN_ARRAY_RATIO) + 0.5));
      file_contents->num_pages = (int *) realloc (file_contents->num_pages, new_total_elements * sizeof (int));
      if (file_contents->num_pages == NULL)
	{
	  return ER_FAILED;
	}
      file_contents->num_slots = new_total_elements;
    }

  /* Put the "num_pages" info to the "last_run" slot of the array */
  file_contents->num_pages[file_contents->last_run] = num_pages;

  return ret;
}

/*
 * sort_run_remove_first () - Removes the first run of the given file contents list
 *   return: void
 *   file_contents(in): which list to remove from
 */
static void
sort_run_remove_first (FILE_CONTENTS * file_contents)
{
  /* If the list is not empty */
  if (file_contents->first_run != -1)
    {
      /* remove the first element of the list */
      if (++file_contents->first_run > file_contents->last_run)
	{
	  /* the list is empty now; indicate so */
	  file_contents->first_run = -1;
	}
    }
}

/*
 * sort_get_num_file_contents () - Returns the number of elements kept in the
 *                           given linked list
 *   return:
 *   file_contents(in): which list
 */
static int
sort_get_num_file_contents (FILE_CONTENTS * file_contents)
{
  /* If the list is not empty */
  if (file_contents->first_run != -1)
    {
      return (file_contents->last_run - file_contents->first_run + 1);
    }
  else
    {
      /* empty list */
      return (0);
    }
}

#if defined(CUBRID_DEBUG)
/*
 * sort_print_file_contents () - Prints the elements of the given file contents list
 *   return: void
 *   file_contents(in): which list to print
 *
 * Note: It is used for debugging purposes.
 */
static void
sort_print_file_contents (const FILE_CONTENTS * file_contents)
{
  int j;

  /* If the list is not empty */
  j = file_contents->first_run;
  if (j > -1)
    {
      fprintf (stdout, "File contents:\n");
      for (; j <= file_contents->last_run; j++)
	{
	  fprintf (stdout, " Run with %3d pages\n", file_contents->num_pages[j]);
	}
    }
  else
    {
      fprintf (stdout, "Empty file:\n");
    }
}
#endif /* CUBRID_DEBUG */
