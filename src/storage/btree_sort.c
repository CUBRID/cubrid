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
 * btree_sort.c - External sort dedicated to the b+tree bulk loader (CBRD-27235)
 *
 * This is the index-build copy of the external sort engine that used to be shared with the query
 * processor (external_sort.c).  It sorts the (key, OID) records produced by btree_sort_get_next ()
 * and feeds them to btree_construct_leafs (), either serially or through the parallel index-leaf
 * pipeline (per-worker sector scans -> per-worker runs -> key-range shards -> parallel leaf build).
 *
 * The engine phases (private slotted page, natural merge run generation, k-way merge, temp file
 * I/O, merge work queue) are kept structurally aligned with external_sort.c on purpose: the
 * convergence checkpoint of CBRD-27235 diffs the two engines function by function.  Everything
 * the query processor needs but the index build does not (SORT_ELIM_DUP merge, top-N limit,
 * ORDER BY / GROUP BY / ANALYTIC parallel arms, trace statistics) is intentionally absent.
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
#include "btree_sort.h"
#include "btree_load.h"
#include "file_manager.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "log_manager.h"
#include "disk_manager.h"
#include "overflow_file.h"
#include "boot_sr.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#include "server_support.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "object_representation.h"
#include "px_worker_manager.hpp"
#include "px_callable_task.hpp"
#include "xasl.h"
#include "xasl_unpack_info.hpp"
#include "ftab_set.hpp"
#include <functional>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Upper limit on the half of the total number of the temporary files.
 * The exact upper limit on total number of temp files is twice this number.
 * (i.e., this number specifies the upper limit on the number of total input
 * or total output files at each stage of the merging process.
 */
#define BTSORT_MAX_HALF_FILES      4
#define BTSORT_MAX_TOT_FILES      (BTSORT_MAX_HALF_FILES * 2)

/* Lower limit on the half of the total number of the temporary files.
 * The exact lower limit on total number of temp files is twice this number.
 * (i.e., this number specifies the lower limit on the number of total input
 * or total output files at each stage of the merging process.
 */
#define BTSORT_MIN_HALF_FILES      2

/* Initial size of the dynamic array that keeps the file contents list */
#define BTSORT_INITIAL_DYN_ARRAY_SIZE 30

/* Expansion Ratio of the dynamic array that keeps the file contents list */
#define BTSORT_EXPAND_DYN_ARRAY_RATIO 1.5

/* Largest record an empty run page accepts: the aligned offset past the header that
 * btsort_spage_initialize leaves, less the slot and the alignment waste find_free charges. */
#define BTSORT_MAXREC_LENGTH             \
        ((ssize_t) DB_ALIGN_BELOW (DB_PAGESIZE - DB_ALIGN ((int) sizeof (BTSORT_SPAGE_HEADER), \
                                                          (int) MAX_ALIGNMENT) \
                                   - (int) sizeof (BTSORT_PAGE_SLOT), (int) MAX_ALIGNMENT))

/* Smallest record area worth offering to the input function; below this the sort buffer is treated as full
 * (an OID plus a short key needs less, this only keeps a sliver at the end of the buffer from being offered). */
#define BTSORT_MIN_REC_AREA  ((ssize_t) (3 * sizeof (INT64)))

/* Index build always keeps duplicate keys: equal-key records are chained onto the first one and the freed
 * slot is compacted away (see btsort_run_find / btsort_run_merge). */
#define BTSORT_CHECK_DUPLICATE(a, b)  \
    do {                          \
        if (cmp == 0) {           \
            btsort_append(a, b);  \
            *(a) = NULL;          \
            dup_num++;            \
        }                         \
    } while (0)

/* parallel merge: runs consumed per dispatched merge task */
#define BTSORT_PX_MERGE_FILES	4
#define BTSORT_MAX_PARALLEL	PRM_MAX_PARALLELISM
#define BTSORT_IS_PARALLEL(t)	((t)->px_parallel_num > 1)

// *INDENT-OFF*
#define BTSORT_EXECUTE_PARALLEL(num, px_sort_param, function)  \
    do {                          \
	for (int i = 0; i < num; i++) {				\
	    parallel_query::callable_task * task =		\
	       new parallel_query::callable_task (sort_param->px_worker_manager, std::		\
	          bind (function, std::placeholders::_1, &px_sort_param[i]));	\
	    sort_param->px_worker_manager->push_task(task);	\
	  }	\
    } while (0)
// *INDENT-ON*

#define BTSORT_WAIT_PARALLEL(parallel_num, sort_param, px_sort_param) \
    do {                          \
	  pthread_mutex_lock (sort_param->px_mtx); \
      while (1) \
	{ \
	  int done = true;  \
	  for (int i = 0; i < parallel_num; i++)  \
	    {  \
	      if (px_sort_param[i].px_status == BTSORT_PX_PROGRESS)  \
		{  \
		  done = false; \
		  break; \
		} \
	      else if (px_sort_param[i].px_status == BTSORT_PX_ERR_FAILED) \
		{ \
		  error = ER_FAILED; \
		} \
	    } \
	  if (done) \
	    { \
	      break; \
	    } \
	  pthread_cond_wait (sort_param->complete_cond, sort_param->px_mtx); \
	} \
      pthread_mutex_unlock (sort_param->px_mtx); \
      sort_param->px_worker_manager->wait_workers (); \
    } while (0)

enum btsort_px_status
{
  BTSORT_PX_ERR_FAILED = -1,
  BTSORT_PX_DONE = 0,
  BTSORT_PX_PROGRESS
};
typedef enum btsort_px_status BTSORT_PX_STATUS;

enum btsort_parallel_type
{
  BTSORT_PX_SINGLE = 0,
  BTSORT_PX_MAIN_IN_PARALLEL = 1,
  BTSORT_PX_THREAD_IN_PARALLEL
};
typedef enum btsort_parallel_type BTSORT_PARALLEL_TYPE;

typedef struct btsort_result_run BTSORT_RESULT_RUN;
struct btsort_result_run
{
  VFID temp_file;
  int num_pages;
};

/* In-memory link used to chain records with an equal sort key.  The record produced by btree_sort_get_next ()
 * reserves its first sizeof (char *) bytes for this link (bt_load_put_buf_to_record). */
typedef struct btsort_rec BTSORT_REC;
struct btsort_rec
{
  BTSORT_REC *next;		/* forward link for duplicate sort_key value */
};

typedef struct btsort_file_contents BTSORT_FILE_CONTENTS;
struct btsort_file_contents
{				/* node of the file_contents linked list */
  int *num_pages;		/* Dynamic array whose elements keep the sizes of the runs contained in the file in
				 * terms of number of slotted pages it occupies */
  int num_slots;		/* Total number of elements the array has */
  int first_run;		/* The index of the array element keeping the size of the first run of the file. */
  int last_run;			/* The index of the array element keeping the size of the last run of the file. */
};

typedef struct btsort_index_shard BTSORT_INDEX_SHARD;
struct btsort_index_shard
{
  int start_page;
  int start_slot;
  int end_page;
  int end_slot;
};
typedef struct btsort_px_merge_input BTSORT_PX_MERGE_INPUT;
struct btsort_px_merge_input
{
  VFID temp;			/* source worker run */
  int npages;			/* run length in pages */
  BTSORT_INDEX_SHARD range;	/* this shard's [start, end) slice of the run */
  FILE_FIND_NTH_CURSOR cursor;	/* this shard's page lookup position in the run above. every shard owns its own
				 * BTSORT_PX_MERGE_INPUT array, so shards reading the same run do not share it. */
};

typedef struct btsort_param BTSORT_PARAM;
struct btsort_param
{
  VFID temp[BTSORT_MAX_TOT_FILES];	/* Temporary file identifiers */
  /* Where the previous page lookup landed in each temp file's user page table. Sorting walks a temp file in page
   * order, so remembering the position turns each lookup into a single page access instead of a walk of the whole
   * table. This lives in BTSORT_PARAM because every parallel worker gets its own copy, which is what makes it usable
   * while several workers read the same file. Reset it whenever temp[i] changes. */
  FILE_FIND_NTH_CURSOR temp_cursor[BTSORT_MAX_TOT_FILES];
  VFID multipage_file;		/* Temporary file for multi page sorting records */
  BTSORT_FILE_CONTENTS file_contents[BTSORT_MAX_TOT_FILES];	/* Contents of each temporary file */

  bool tde_encrypted;		/* whether related temp files are encrypted (TDE) or not */

  char *internal_memory;	/* Internal_memory used for internal sorting phase and as input/output buffers for temp
				 * files during merging phase */
  int tot_runs;			/* Total number of runs */
  int tot_buffers;		/* Size of internal memory used in terms of number of buffers it occupies */
  int tot_tempfiles;		/* Total number of temporary files */
  int half_files;		/* Half number of temporary files */
  int in_half;			/* Which half of temp files is for input */

  /* Comparison function to use in the internal sorting and the merging phases */
  BTSORT_CMP_FUNC *cmp_fn;
  void *cmp_arg;

  /* input function to apply on temporary records */
  BTSORT_GET_FUNC *get_fn;
  void *get_arg;

  /* output function to apply on temporary records */
  BTSORT_PUT_FUNC *put_fn;
  void *put_arg;

  /* Estimated number of pages in each temp file (used in initialization) */
  int tmp_file_pgs;

  /* total number of recordes */
  unsigned int total_numrecs;

  /* support parallelism */
  BTSORT_PX_STATUS px_status;
  int px_result_file_idx;
  THREAD_ENTRY *px_orig_thread_p;
  BTSORT_PARAM *ori_sort_param;
  int px_parallel_num;
  BTSORT_RESULT_RUN *px_result_run;
    parallel_query::worker_manager * px_worker_manager;
    cuberr::context * main_error_context;
  bool px_error_published;	/* first-error-wins guard: set (under px_mtx) by the first failing worker that
				 * publishes its error context into main_error_context; later failures keep quiet
				 * so the root cause is not overwritten (lives on the main BTSORT_PARAM only) */
  BTSORT_PX_MERGE_INPUT *px_merge_inputs;	/* index-leaf shard put: key-range slice of each worker run */
  int px_merge_n_inputs;
#if defined(SERVER_MODE)
  pthread_mutex_t *px_mtx;	/* px_status mutex */
  pthread_cond_t *complete_cond;	/* complete condition */
#endif
};

typedef struct btsort_rec_list BTSORT_REC_LIST;
struct btsort_rec_list
{
  struct btsort_rec_list *next;	/* next sorted record item */
  int rec_pos;			/* record position */
};				/* Sort record list */

typedef struct btsort_spage_header BTSORT_SPAGE_HEADER;
struct btsort_spage_header
{
  INT16 nslots;			/* Number of allocated slots for the page */
  INT16 nrecs;			/* Number of records on page */
  INT16 alignment;		/* Alignment for records. */
  INT16 tfree;			/* Total free space on page */
  INT16 foffset;		/* Byte offset from the beginning of the page to the first free byte area on the page. */
};

typedef struct btsort_page_slot BTSORT_PAGE_SLOT;
struct btsort_page_slot
{
  INT16 roffset;		/* Byte Offset from the beginning of the page to the beginning of the record */
  INT16 rlength;		/* Length of record */
  INT16 rtype;			/* Record type described by slot. */
};

typedef struct btsort_srun BTSORT_SRUN;
struct btsort_srun
{
  char low_high;		/* location info LOW('L') : otherbase HIGH('H') : base */
  unsigned short tree_depth;	/* depth of this node : leaf is 1 */
  long start;
  long stop;
};

typedef struct btsort_stack BTSORT_STACK;
struct btsort_stack
{
  int top;
  BTSORT_SRUN *srun;
};

/* parallel merge work queue (defined with the merge queue functions below) */
typedef struct btsort_merge_queue_ctx BTSORT_MERGE_QUEUE_CTX;

/* engine phases */
static void btsort_spage_initialize (PAGE_PTR pgptr);
static INT16 btsort_spage_get_numrecs (PAGE_PTR pgptr);
static INT16 btsort_spage_find_free (PAGE_PTR pgptr, BTSORT_PAGE_SLOT ** sptr, INT16 length, INT16 type, INT16 * space);
static INT16 btsort_spage_insert (PAGE_PTR pgptr, RECDES * recdes);
static SCAN_CODE btsort_spage_get_record (PAGE_PTR pgptr, INT16 slotid, RECDES * recdes, bool peek_p);
static void btsort_run_flip (char **start, char **stop);
static void btsort_append (const void *pk0, const void *pk1);
static void btsort_run_find (char **source, long *top, BTSORT_STACK * st_p, long limit, BTSORT_CMP_FUNC * compare,
			     void *comp_arg);
static void btsort_run_merge (char **low, char **high, BTSORT_STACK * st_p, BTSORT_CMP_FUNC * compare, void *comp_arg);
static char **btsort_run_sort (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, char **base, long limit,
			       char **otherbase, long *srun_limit);
static void btsort_listfile_execute (cubthread::entry & thread_ref, BTSORT_PARAM * sort_param);
static int btsort_listfile_internal (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param);
static int btsort_inphase_sort (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, BTSORT_GET_FUNC * get_fn,
				void *get_arg, unsigned int *total_numrecs);
static int btsort_run_flush (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, int out_file, int *cur_page,
			     char *output_buffer, char **index_area, int numrecs, int rec_type);
static char *btsort_retrieve_longrec (THREAD_ENTRY * thread_p, RECDES * address, RECDES * memory);
static int btsort_exphase_merge (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param);
static int btsort_get_avg_numpages_of_nonempty_tmpfile (BTSORT_PARAM * sort_param);
static void btsort_return_used_resources (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param,
					  BTSORT_PARALLEL_TYPE parallel_type);
static int btsort_add_new_file (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor,
				int file_pg_cnt_est, bool force_alloc, bool tde_encrypted);
static int btsort_write_area (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor, int first_page,
			      INT32 num_pages, char *area_start, bool tde_encrypted);
static int btsort_read_area (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor, int first_page,
			     INT32 num_pages, char *area_start);
static int btsort_get_num_half_tmpfiles (int tot_buffers);
static int btsort_checkalloc_numpages_of_outfiles (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param);
static int btsort_get_numpages_of_active_infiles (const BTSORT_PARAM * sort_param);
static int btsort_find_inbuf_size (int tot_buffers, int in_sections);
static int btsort_run_add_new (BTSORT_FILE_CONTENTS * file_contents, int num_pages);
static void btsort_run_remove_first (BTSORT_FILE_CONTENTS * file_contents);
static int btsort_get_num_file_contents (BTSORT_FILE_CONTENTS * file_contents);
#if defined(SERVER_MODE)
static int btsort_put_result_from_tmpfile (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, int start_pagenum);
static int btsort_copy_sort_param (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param,
				   int parallel_num);
static int btsort_index_page_decode_key (THREAD_ENTRY * thread_p, char *pgbuf, int slot, LOAD_ARGS * load_args,
					 DB_VALUE * key);
static int btsort_index_run_decode_key_at (THREAD_ENTRY * thread_p, VFID * temp, char *iomem, LOAD_ARGS * load_args,
					   int page, int slot, DB_VALUE * key);
static int btsort_px_run_lower_bound (THREAD_ENTRY * thread_p, VFID * temp, int npages, char *iomem,
				      LOAD_ARGS * load_args, TP_DOMAIN * key_type, DB_VALUE * splitter, int *page_out,
				      int *slot_out);
static int btsort_px_select_splitters (THREAD_ENTRY * thread_p, VFID * run_temp, const int *run_npages, int n_runs,
				       char *iomem, LOAD_ARGS * load_args, TP_DOMAIN * key_type, int parallel_num,
				       DB_VALUE * splitters, int *n_splitters);
static void btsort_px_free_shard_inputs (BTSORT_PX_MERGE_INPUT ** shard_inputs, int n_shards);
static int btsort_px_slice_runs_index_leaf (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param,
					    BTSORT_PARAM * sort_param, int parallel_num,
					    BTSORT_PX_MERGE_INPUT ** shard_inputs, int *n_runs_out, int *n_shards_out,
					    INT64 * total_pages_out);
static bool btsort_px_merge_cursor_done (const BTSORT_PX_MERGE_INPUT * input, int page, int slot);
static int btsort_px_merge_cursor_fetch (THREAD_ENTRY * thread_p, char *pgbuf, int slot, RECDES * rec,
					 RECDES * longrec);
static bool btsort_px_merge_cursor_less (BTSORT_CMP_FUNC * compare, void *compare_arg, RECDES * cur_rec,
					 RECDES * long_rec, int a, int b);
static void btsort_px_merge_heap_sift_down (int *heap, int heap_n, int pos, BTSORT_CMP_FUNC * compare,
					    void *compare_arg, RECDES * cur_rec, RECDES * long_rec);
static void btsort_merge_queue_enqueue (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_RESULT_RUN run);
static BTSORT_RESULT_RUN btsort_merge_queue_dequeue (BTSORT_MERGE_QUEUE_CTX * qctx);
static int btsort_merge_queue_acquire_ctx (BTSORT_MERGE_QUEUE_CTX * qctx);
static void btsort_merge_queue_release_ctx (BTSORT_MERGE_QUEUE_CTX * qctx, int idx);
static void btsort_merge_queue_setup_ctx (int pool_idx, BTSORT_MERGE_QUEUE_CTX * qctx, const BTSORT_RESULT_RUN * runs,
					  int k);
static void btsort_merge_queue_ctx_init (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * sort_param,
					 BTSORT_PARAM * px_sort_param, int parallel_num);
static void btsort_merge_queue_ctx_destroy (BTSORT_MERGE_QUEUE_CTX * qctx);
static void btsort_merge_queue_enqueue_initial_runs (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * px_sort_param,
						     int parallel_num);
static int btsort_merge_queue_run (THREAD_ENTRY * thread_p, BTSORT_MERGE_QUEUE_CTX * qctx);
static void btsort_merge_queue_stage_final_run (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * dst);
static void btsort_merge_queue_try_dispatch (BTSORT_MERGE_QUEUE_CTX * qctx);
static void btsort_merge_nruns_queue_cb (cubthread::entry & thread_ref, BTSORT_PARAM * ctx,
					 BTSORT_MERGE_QUEUE_CTX * qctx);
static void btsort_put_result_index_leaf (cubthread::entry & thread_ref, BTSORT_PARAM * sort_param);
static BT_LOAD_PX_OUTCOME btsort_px_construct_index_leaf (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param,
							  BTSORT_PARAM * sort_param, int parallel_num);
static int btsort_merge_run_for_parallel_index_leaf_build (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param,
							   BTSORT_PARAM * sort_param, int parallel_num);
static int btsort_merge_nruns (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param);
static int btsort_compute_parallel_degree (bool no_logging_build, int n_data_pages, int n_sects);
static int btsort_check_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param);
static int btsort_start_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param);
static int btsort_end_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param);
#endif /* SERVER_MODE */

/*
 * btsort_spage_initialize () - Initialize a run page
 *   return: void
 *   pgptr(in): Pointer to the page
 *
 * Note: A run page must be initialized before records are inserted on it. Every run page appends its records
 *       in order and keeps them MAX_ALIGNMENT-aligned; no slot is ever freed or reused.
 */
static void
btsort_spage_initialize (PAGE_PTR pgptr)
{
  BTSORT_SPAGE_HEADER *sphdr;
  INT16 waste;

  sphdr = (BTSORT_SPAGE_HEADER *) pgptr;

  sphdr->nslots = 0;
  sphdr->nrecs = 0;
  sphdr->alignment = MAX_ALIGNMENT;

  /* the first record starts at the first aligned offset past the header */
  waste = DB_WASTED_ALIGN (sizeof (BTSORT_SPAGE_HEADER), MAX_ALIGNMENT);
  sphdr->foffset = sizeof (BTSORT_SPAGE_HEADER) + waste;
  sphdr->tfree = DB_PAGESIZE - sphdr->foffset;
}

/*
 * btsort_spage_get_numrecs () - Return the total number of records on the slotted page
 *   return:
 *   pgptr(in): Pointer to slotted page
 */
static INT16
btsort_spage_get_numrecs (PAGE_PTR pgptr)
{
  BTSORT_SPAGE_HEADER *sphdr;

  sphdr = (BTSORT_SPAGE_HEADER *) pgptr;

  return sphdr->nrecs;
}

/*
 * btsort_spage_find_free () - Find a free area/slot where a record of the given length
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
btsort_spage_find_free (PAGE_PTR pgptr, BTSORT_PAGE_SLOT ** sptr, INT16 length, INT16 type, INT16 * space)
{
  BTSORT_SPAGE_HEADER *sphdr;
  INT16 slotid;
  INT16 waste;

  sphdr = (BTSORT_SPAGE_HEADER *) pgptr;

  /* Calculate the wasting space that this record will introduce. We need to take in consideration the wasting space
   * when there is space saved */
  waste = DB_WASTED_ALIGN (length, sphdr->alignment);
  *space = length + waste;

  /* Runs are append-only: the record always takes a new slot at the end of the slot array. Make
   * sure the record and its slot both fit. */
  *space += sizeof (BTSORT_PAGE_SLOT);
  if (*space > sphdr->tfree)
    {
      *sptr = NULL;
      *space = 0;
      return NULL_SLOTID;
    }

  slotid = sphdr->nslots;
  *sptr = (BTSORT_PAGE_SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (BTSORT_PAGE_SLOT)) - slotid;
  sphdr->nslots++;

  /* Now separate an empty area for the record */
  (*sptr)->roffset = sphdr->foffset;
  (*sptr)->rlength = length;
  (*sptr)->rtype = type;

  /* Adjust the header */
  sphdr->nrecs++;
  sphdr->tfree -= *space;
  sphdr->foffset += length + waste;

  /* The page is set dirty somewhere else */

  return slotid;
}

/*
 * btsort_spage_insert () - Insert a record onto the given slotted page
 *   return: A slot identifier
 *   pgptr(in): Pointer to slotted page
 *   recdes(in): Pointer to a record descriptor
 *
 * Note: If the record does not fit on the page, an error condition is
 *       indicated and NULL_SLOTID is returned.
 */
static INT16
btsort_spage_insert (PAGE_PTR pgptr, RECDES * recdes)
{
  BTSORT_PAGE_SLOT *sptr;
  INT16 slotid;
  INT16 used_space;

  if (recdes->length > BTSORT_MAXREC_LENGTH)
    {
      return NULL_SLOTID;
    }

  assert (recdes->type == REC_HOME || recdes->type == REC_BIGONE);

  slotid = btsort_spage_find_free (pgptr, &sptr, recdes->length, recdes->type, &used_space);
  if (slotid != NULL_SLOTID)
    {
      /* Find the free slot and insert the record */
      memcpy (((char *) pgptr + sptr->roffset), recdes->data, recdes->length);
    }

  return slotid;
}

/*
 * btsort_spage_get_record () - Get specific record
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
btsort_spage_get_record (PAGE_PTR pgptr, INT16 slotid, RECDES * recdes, bool peek_p)
{
  BTSORT_SPAGE_HEADER *sphdr;
  BTSORT_PAGE_SLOT *sptr;

  sphdr = (BTSORT_SPAGE_HEADER *) pgptr;

  sptr = (BTSORT_PAGE_SLOT *) ((char *) pgptr + DB_PAGESIZE - sizeof (BTSORT_PAGE_SLOT));

  sptr -= slotid;

  /* runs are append-only: a slot is never freed, so a live slotid always has a record */
  if (slotid < 0 || slotid >= sphdr->nslots)
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

/*
 * btsort_run_flip () - Flip a run in place
 *   return: void
 *   start(in):
 *   stop(in):
 *
 * Note: Odd runs will have middle pointer undisturbed.
 */
static void
btsort_run_flip (char **start, char **stop)
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
 * btsort_append () -
 *   return: void
 *   pk0(in):
 *   pk1(in):
 */
static void
btsort_append (const void *pk0, const void *pk1)
{
  BTSORT_REC *node, *list;

  node = *(BTSORT_REC **) pk0;
  list = *(BTSORT_REC **) pk1;

  while (list->next)
    {
      list = list->next;
    }
  list->next = node;

  return;
}

/*
 * btsort_run_find () - Finds the longest ascending or descending run it can
 *   return:
 *   source(in):
 *   top(in):
 *   st_p(in):
 *   limit(in):
 *   compare(in):
 *   comp_arg(in):
 *
 * Note: Flip descending run, and assign RUN start and stop
 */
static void
btsort_run_find (char **source, long *top, BTSORT_STACK * st_p, long limit, BTSORT_CMP_FUNC * compare, void *comp_arg)
{
  char **start;
  char **stop;
  char **next_stop;
  char **limit_p;
  BTSORT_SRUN *srun_p;

  char **dup;
  char **non_dup;
  int dup_num;
  int cmp;
  bool increasing_order;

  /* init new BTSORT_SRUN */
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
	  BTSORT_CHECK_DUPLICATE (stop, next_stop);	/* increase dup_num */

	  stop = next_stop;
	  next_stop = next_stop + 1;
	}
    }
  else
    {
      increasing_order = true;	/* mark as increasing order run */

      /* mark duplicate as NULL */
      BTSORT_CHECK_DUPLICATE (start, stop);	/* increase dup_num */

      /* build increasing order run */
      while (next_stop < limit_p && ((cmp = (*compare) (stop, next_stop, comp_arg)) <= 0))
	{
	  /* mark duplicate as NULL */
	  BTSORT_CHECK_DUPLICATE (stop, next_stop);	/* increase dup_num */

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
      btsort_run_flip (start + dup_num, stop);
    }

  *top += CAST_BUFLEN (stop - start);	/* advance to last visited */
  srun_p->start += dup_num;
  srun_p->stop = *top;

  (*top)++;			/* advance to next unvisited element */

  return;
}

/*
 * btsort_run_merge () - Merges two runs from source to dest, updateing dest_top
 *   return:
 *   low(in):
 *   high(in):
 *   st_p(in):
 *   compare(in):
 *   comp_arg(in):
 */
static void
btsort_run_merge (char **low, char **high, BTSORT_STACK * st_p, BTSORT_CMP_FUNC * compare, void *comp_arg)
{
  char dest_low_high;
  char **left_start, **right_start;
  char **left_stop, **right_stop;
  char **dest_ptr;
  BTSORT_SRUN *left_srun_p, *right_srun_p;
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
		  /* chain duplicate onto the left record */
		  btsort_append (left_stop, right_stop);
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

      /* STEP 3: reconfig BTSORT_STACK */
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
 * btsort_run_sort () - An implementation of a run-sort algorithm
 *   return: pointer to the sorted area
 *   thread_p(in):
 *   sort_param(in): sort parameters
 *   base(in): pointer to the element at the base of the table
 *   limit(in): numrecs of before current sort
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
btsort_run_sort (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, char **base, long limit, char **otherbase,
		 long *srun_limit)
{
  BTSORT_CMP_FUNC *compare;
  void *comp_arg;
  char **src, **dest, **result;
  BTSORT_STACK sr_stack, *st_p;
  long src_top = 0;
  int cnt;

  assert_release (limit == *srun_limit);

  if (limit <= 1)
    {
      return base;
    }

  /* init */
  compare = sort_param->cmp_fn;
  comp_arg = sort_param->cmp_arg;

  src = base;
  dest = otherbase;
  result = NULL;

  st_p = &sr_stack;
  st_p->top = -1;

  cnt = (int) (log10 (ceil ((double) limit / 2.0)) / log10 (2.0)) + 2;

  st_p->srun = (BTSORT_SRUN *) db_private_alloc (NULL, cnt * sizeof (BTSORT_SRUN));
  if (st_p->srun == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, cnt * sizeof (BTSORT_SRUN));
      return NULL;
    }

  do
    {
      btsort_run_find (src, &src_top, st_p, limit, compare, comp_arg);
      if (src_top < limit)
	{
	  btsort_run_find (src, &src_top, st_p, limit, compare, comp_arg);
	}

      while ((st_p->top >= 1)	/* may need to merge */
	     && ((src_top >= limit)	/* case 1: final merge stage */
		 || ((src_top < limit)	/* case 2: non-final merge stage */
		     && (st_p->srun[st_p->top - 1].tree_depth == st_p->srun[st_p->top].tree_depth))))
	{
	  btsort_run_merge (dest, src, st_p, compare, comp_arg);
	}
    }
  while (src_top < limit);

  /* save limit of non-duplicated value slot */
  *srun_limit = limit - st_p->srun[0].start;

  /* move base pointer */
  result = base + st_p->srun[0].start;

  if (st_p->srun[0].low_high == 'L')
    {
      result = otherbase + st_p->srun[0].start;
    }

  assert (result != NULL);

  db_private_free_and_init (NULL, st_p->srun);

  return result;
}

/*
 * btree_sort () - External sort entry point for the b+tree bulk loader
 *   return: NO_ERROR or error code
 *   get_fn(in): input function (btree_sort_get_next)
 *   get_arg(in): SORT_ARGS *
 *   put_fn(in): output function (btree_construct_leafs)
 *   put_arg(in): LOAD_ARGS *
 *   cmp_fn(in): key comparison function
 *   cmp_arg(in): SORT_ARGS *
 *   includes_tde_class(in): whether any scanned class is TDE-encrypted (temp files follow)
 *
 * Note: The former sort_listfile() of external_sort.c shared with the query processor.  Only the SORT_INDEX_LEAF shape
 *       survives here: duplicates are always kept (SORT_DUP), there is no top-N limit, and the buffer is
 *       sized from the sort buffer parameter (the loader never passes an input page estimate).
 */
int
btree_sort (THREAD_ENTRY * thread_p, BTSORT_GET_FUNC * get_fn, void *get_arg, BTSORT_PUT_FUNC * put_fn,
	    void *put_arg, BTSORT_CMP_FUNC * cmp_fn, void *cmp_arg, bool includes_tde_class)
{
  int error = NO_ERROR;
  BTSORT_PARAM ori_sort_param;
  BTSORT_PARAM *sort_param = &ori_sort_param;
  int i;

  /* for parallel sort */
  BTSORT_PARAM *px_sort_param = NULL;
#if defined(SERVER_MODE)
  pthread_mutex_t px_mtx;	/* px_status mutex */
  pthread_cond_t complete_cond;	/* complete condition */
#endif

#if defined(SERVER_MODE)
  if (pthread_mutex_init (&px_mtx, NULL) != 0)
    {
      error = ER_CSS_PTHREAD_MUTEX_INIT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }
  if (pthread_cond_init (&complete_cond, NULL) != 0)
    {
      error = ER_CSS_PTHREAD_COND_INIT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      pthread_mutex_destroy (&px_mtx);
      return error;
    }
  sort_param->px_mtx = &px_mtx;
  sort_param->complete_cond = &complete_cond;
#endif /* SERVER_MODE */

  sort_param->cmp_fn = cmp_fn;
  sort_param->cmp_arg = cmp_arg;

  sort_param->get_fn = get_fn;
  sort_param->get_arg = get_arg;

  sort_param->put_fn = put_fn;
  sort_param->put_arg = put_arg;

  sort_param->tot_tempfiles = 0;

  /* initialize memory allocable fields */
  for (i = 0; i < BTSORT_MAX_TOT_FILES; i++)
    {
      sort_param->temp[i].volid = NULL_VOLID;
      file_find_nth_cursor_reset (&sort_param->temp_cursor[i]);
      sort_param->file_contents[i].num_pages = NULL;
    }
  sort_param->internal_memory = NULL;

  /* initialize temp. overflow file. Real value will be assigned in btsort_inphase_sort function, if long size sorting
   * records are encountered. */
  sort_param->multipage_file.volid = NULL_VOLID;
  sort_param->multipage_file.fileid = NULL_FILEID;

  /* index_build_buffer_size is entered in bytes and kept in IO pages (PRM_SIZE_UNIT |
   * PRM_DIFFER_UNIT); the loader gives no size estimate of its input. Four pages is the minimum of
   * the parameter and the floor the code needs: one page goes to the output buffer and the slots
   * grow down from it. */
  sort_param->tot_buffers = MAX (4, prm_get_integer_value (PRM_ID_INDEX_BUILD_BUFFER_SIZE));

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

  sort_param->half_files = btsort_get_num_half_tmpfiles (sort_param->tot_buffers);
  sort_param->tot_tempfiles = sort_param->half_files << 1;
  sort_param->in_half = 0;

  for (i = 0; i < BTSORT_MAX_TOT_FILES; i++)
    {
      /* Initilize temporary file identifier; real value will be set in "btsort_add_new_file () */
      sort_param->temp[i].volid = NULL_VOLID;
      file_find_nth_cursor_reset (&sort_param->temp_cursor[i]);

      /* Initilize file contents list */
      sort_param->file_contents[i].num_pages = (int *) malloc (BTSORT_INITIAL_DYN_ARRAY_SIZE * sizeof (int));
      if (sort_param->file_contents[i].num_pages == NULL)
	{
	  sort_param->tot_tempfiles = i;
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) (BTSORT_INITIAL_DYN_ARRAY_SIZE * sizeof (int)));
	  goto cleanup;
	}

      sort_param->file_contents[i].num_slots = BTSORT_INITIAL_DYN_ARRAY_SIZE;
      sort_param->file_contents[i].first_run = -1;
      sort_param->file_contents[i].last_run = -1;
    }

  /* Size estimate of an input temp file: a one-run share of the buffer. The files grow as runs
   * are flushed. */
  sort_param->tmp_file_pgs = CEIL_PTVDIV (sort_param->tot_buffers, sort_param->half_files);

  sort_param->tde_encrypted = includes_tde_class;
  sort_param->px_error_published = false;
  sort_param->px_merge_inputs = NULL;
  sort_param->px_merge_n_inputs = 0;

  tde_er_log ("btree_sort(): tde_encrypted = %d\n", sort_param->tde_encrypted);

#if defined(SERVER_MODE)
  /* check the number of parallel process */
  sort_param->px_parallel_num = btsort_check_parallelism (thread_p, sort_param);

  if (sort_param->px_parallel_num <= 1)
    {
      SORT_ARGS *sort_args_p = (SORT_ARGS *) sort_param->get_arg;

      /* single process */
      sort_param->px_parallel_num = 1;

      /* no-redo builds are restricted to genuinely parallel construction. This is the single-process
       * shape (parallelism threshold not met, or no workers could be reserved) -- demote to a fully
       * logged build now, strictly before btree_create_file()/any content page write below, so the
       * legacy build that follows logs normally end to end and no replay barrier record is appended
       * for it. */
      bt_load_demote_to_logged ((LOAD_ARGS *) sort_param->put_arg);
      memset (&sort_args_p->hfscan_cache, 0, sizeof (HEAP_SCANCACHE));
      memset (&sort_args_p->attr_info, 0, sizeof (HEAP_CACHE_ATTRINFO));
      if (bt_load_heap_scancache_start_for_attrinfo (thread_p, sort_args_p, NULL, NULL, true) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      log_sysop_start (thread_p);
      if (btree_create_file (thread_p, &sort_args_p->class_ids[0], sort_args_p->attr_ids[0],
			     sort_args_p->btid->sys_btid) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  log_sysop_abort (thread_p);
	  bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args_p, NULL, NULL);
	  error = ER_FAILED;
	  goto cleanup;
	}
      vacuum_log_add_dropped_file (thread_p, &sort_args_p->btid->sys_btid->vfid, NULL,
				   VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

      error = btsort_listfile_internal (thread_p, sort_param);

      bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args_p, NULL, NULL);
      if (error != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	}
    }
  else
    {
      px_sort_param = (BTSORT_PARAM *) malloc (sizeof (BTSORT_PARAM) * sort_param->px_parallel_num);
      if (px_sort_param == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (BTSORT_PARAM));
	  goto cleanup;
	}

      /* parallel process */
      error = btsort_start_parallelism (thread_p, px_sort_param, sort_param);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      /* execute parallel sort */
      BTSORT_EXECUTE_PARALLEL (sort_param->px_parallel_num, px_sort_param, btsort_listfile_execute);

      /* wait for threads */
      BTSORT_WAIT_PARALLEL (sort_param->px_parallel_num, sort_param, px_sort_param);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      error = btsort_end_parallelism (thread_p, px_sort_param, sort_param);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

#else
  /* single process for stand alone mode */
  sort_param->px_parallel_num = 1;
  error = btsort_listfile_internal (thread_p, sort_param);
#endif

cleanup:
#if defined(SERVER_MODE)
  pthread_mutex_destroy (&px_mtx);
  pthread_cond_destroy (&complete_cond);
#endif

  /* free sort_param */
#if defined(SERVER_MODE)
  if (sort_param->px_parallel_num > 1)
    {
      for (int i = 0; i < sort_param->px_parallel_num; i++)
	{
	  btsort_return_used_resources (thread_p, &px_sort_param[i], BTSORT_PX_THREAD_IN_PARALLEL);
	}

      btsort_return_used_resources (thread_p, sort_param, BTSORT_PX_MAIN_IN_PARALLEL);
      sort_param->px_worker_manager->release_workers ();
      free_and_init (px_sort_param);
    }
  else
#endif
    {
      btsort_return_used_resources (thread_p, sort_param, BTSORT_PX_SINGLE);
    }

  return error;
}

#if defined(SERVER_MODE)
/*
 * btsort_listfile_execute () - Parallel worker body: sort this worker's heap sectors into one run
 *   return: void (status is published through sort_param->px_status)
 *   thread_ref(in): worker thread
 *   sort_param(in): this worker's sort parameters (get_arg is a per-worker SORT_ARGS copy)
 */
static void
btsort_listfile_execute (cubthread::entry & thread_ref, BTSORT_PARAM * sort_param)
{
  THREAD_ENTRY *thread_p = &thread_ref;
  BTSORT_PX_STATUS px_status;
  PRED_EXPR_WITH_CONTEXT *filter_pred = NULL;
  FILTER_INDEX_INFO filter_index_info = { NULL, 0 };
  FUNCTION_INDEX_INFO func_index_info;
  XASL_UNPACK_INFO *func_unpack_info = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  SORT_ARGS *sort_args_p = (SORT_ARGS *) sort_param->get_arg;

  thread_p->push_resource_tracks ();

  thread_ref.tran_index = sort_param->px_orig_thread_p->tran_index;
  thread_ref.m_px_orig_thread_entry = sort_param->px_orig_thread_p;
  thread_ref.conn_entry = sort_param->px_orig_thread_p->conn_entry;
  if (sort_param->px_orig_thread_p->on_trace)
    {
      thread_ref.on_trace = true;
      perfmon_initialize_parallel_stats (&thread_ref);
    }

  func_index_info.expr_stream = NULL;
  func_index_info.expr_stream_size = 0;

  if (sort_args_p->filter != NULL)
    {
      filter_index_info = *sort_args_p->filter_index_info;
    }

  if (sort_args_p->func_index_info != NULL)
    {
      func_index_info = *sort_args_p->func_index_info;
    }

  sort_args_p->filter = NULL;
  sort_args_p->func_index_info = NULL;

  if (btree_load_filter_pred_function_info
      (thread_p, sort_args_p, &filter_pred, &filter_index_info, &func_index_info, &func_unpack_info,
       &single_node_type) != NO_ERROR)
    {
      px_status = BTSORT_PX_ERR_FAILED;
      goto cleanup;
    }

  sort_args_p->attrinfo_inited = false;
  sort_args_p->scancache_inited = false;
  memset (&sort_args_p->hfscan_cache, 0, sizeof (HEAP_SCANCACHE));
  memset (&sort_args_p->attr_info, 0, sizeof (HEAP_CACHE_ATTRINFO));
  if (bt_load_heap_scancache_start_for_attrinfo (thread_p, sort_args_p, NULL, NULL, true) != NO_ERROR)
    {
      px_status = BTSORT_PX_ERR_FAILED;
      goto cleanup;
    }

  if (btsort_listfile_internal (&thread_ref, sort_param) != NO_ERROR)
    {
      px_status = BTSORT_PX_ERR_FAILED;
    }
  else
    {
      px_status = BTSORT_PX_DONE;
    }

cleanup:
  bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args_p, NULL, NULL);
  bt_load_clear_pred_and_unpack (thread_p, sort_args_p, func_unpack_info);

  if (sort_param->px_orig_thread_p->on_trace)
    {
      perfmon_destroy_parallel_stats (&thread_ref);
    }

  thread_p->pop_resource_tracks ();

  /* done */
  pthread_mutex_lock (sort_param->px_mtx);
  sort_param->px_status = px_status;
  if (px_status == BTSORT_PX_ERR_FAILED)
    {
      /* first-error-wins: only the first failing worker publishes its error context into the main
       * thread's context; a later failure must not overwrite the root cause. */
      if (sort_param->ori_sort_param == NULL || !sort_param->ori_sort_param->px_error_published)
	{
	  if (sort_param->ori_sort_param != NULL)
	    {
	      sort_param->ori_sort_param->px_error_published = true;
	    }
	  sort_param->main_error_context->get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	}
    }
  thread_ref.m_px_orig_thread_entry = NULL;
  pthread_cond_signal (sort_param->complete_cond);
  pthread_mutex_unlock (sort_param->px_mtx);
}
#endif

/*
 * btsort_listfile_internal () - Perform sorting
 *   return:
 */
static int
btsort_listfile_internal (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param)
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
  error =
    btsort_inphase_sort (thread_p, sort_param, sort_param->get_fn, sort_param->get_arg, &sort_param->total_numrecs);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (sort_param->tot_runs > 1)
    {
      assert (sort_param->tot_runs > 0);
      /* Create output temporary files make file and temporary volume page count estimates */
      file_pg_cnt_est = btsort_get_avg_numpages_of_nonempty_tmpfile (sort_param);
      file_pg_cnt_est = MAX (1, file_pg_cnt_est);

      for (i = sort_param->half_files; i < sort_param->tot_tempfiles; i++)
	{
	  error =
	    btsort_add_new_file (thread_p, &(sort_param->temp[i]), &sort_param->temp_cursor[i], file_pg_cnt_est, true,
				 sort_param->tde_encrypted);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      error = btsort_exphase_merge (thread_p, sort_param);
    }				/* if (sort_param->tot_runs > 1) */

  return error;
}

/*
 * btsort_inphase_sort () - Internal sorting phase
 *   return:
 *   sort_param(in): sort parameters
 *   get_fn(in): user-supplied function: provides the temporary record for
 *               the given input record
 *   get_arg(in): arguments for get_fn
 *   total_numrecs(out): records sorted
 *
 */
static int
btsort_inphase_sort (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, BTSORT_GET_FUNC * get_fn, void *get_arg,
		     unsigned int *total_numrecs)
{
  /* Variables for the input file */
  BTSORT_STATUS status;

  /* Variables for the current output file */
  int out_curfile;
  char *output_buffer;
  int cur_page[BTSORT_MAX_HALF_FILES];

  /* Variables for the internal memory */
  RECDES temp_recdes;
  RECDES long_recdes;		/* Record desc. for reading in long sorting records */
  char *item_ptr;		/* Pointer to the first free location of the temp. records region of internal memory */
  long numrecs;			/* Number of records kept in the internal memory */
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

  assert (sort_param->half_files <= BTSORT_MAX_HALF_FILES);

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
  saved_numrecs = 0;
  *total_numrecs = 0;
  saved_index_area = NULL;
  item_ptr = sort_param->internal_memory + BTSORT_RECORD_LENGTH_SIZE;
  index_area = (char **) (output_buffer - sizeof (char *));
  index_buff = index_area - 1;
  temp_recdes.area_size = BTSORT_MAXREC_LENGTH;
  temp_recdes.length = 0;

  long_recdes.area_size = 0;
  long_recdes.data = NULL;

  for (;;)
    {
      if ((char *) index_buff < item_ptr)
	{
	  /* Internal memory is already full */
	  status = BTSORT_REC_DOESNT_FIT;
	}
      else
	{
	  /* Internal memory is not full; try to get the next item */
	  temp_recdes.data = item_ptr;
	  if (((int) ((char *) index_buff - item_ptr)) < BTSORT_MAXREC_LENGTH)
	    {
	      temp_recdes.area_size = (int) ((char *) index_buff - item_ptr) - (4 * sizeof (char *));
	    }

	  if (temp_recdes.area_size <= BTSORT_MIN_REC_AREA)
	    {
	      /* internal memory is not enough */
	      status = BTSORT_REC_DOESNT_FIT;
	    }
	  else
	    {
	      status = (*get_fn) (thread_p, &temp_recdes, get_arg);
	      /* There are no more input records; So, break the loop */
	      if (status == BTSORT_NOMORE_RECS)
		{
		  break;
		}
	    }
	}

      switch (status)
	{
	case BTSORT_ERROR_OCCURRED:
	  error = er_errid ();
	  assert (error != NO_ERROR);
	  goto exit_on_error;

	case BTSORT_REC_DOESNT_FIT:
	  if (numrecs > 0)
	    {
	      /* Perform internal sorting and flush the run */

	      index_area++;

	      index_area = btsort_run_sort (thread_p, sort_param, index_area, numrecs, index_buff, &numrecs);
	      *total_numrecs += numrecs;

	      if (index_area == NULL || numrecs < 0)
		{
		  error = ER_FAILED;
		  goto exit_on_error;
		}

	      error =
		btsort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs,
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
	      item_ptr = sort_param->internal_memory + BTSORT_RECORD_LENGTH_SIZE;
	      index_area = (char **) (output_buffer - sizeof (char *));
	      index_buff = index_area - 1;
	      temp_recdes.area_size = BTSORT_MAXREC_LENGTH;

	      /* Switch to the next Temp file */
	      if (++out_curfile >= sort_param->half_files)
		{
		  out_curfile = sort_param->in_half;
		}
	    }

	  /* Check if the record would fit into a single slotted page. If not, take special action for this record. */
	  if (temp_recdes.length > BTSORT_MAXREC_LENGTH)
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

	      if (status != BTSORT_SUCCESS)
		{
		  /* Obtaining the long record has failed */
		  if (status == BTSORT_REC_DOESNT_FIT || status == BTSORT_NOMORE_RECS)
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
		      if (file_apply_tde_algorithm (thread_p, &sort_param->multipage_file, tde_algo) != NO_ERROR)
			{
			  file_temp_retire (thread_p, &sort_param->multipage_file);
			  ASSERT_ERROR ();
			  goto exit_on_error;
			}
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
	      BTSORT_RECORD_LENGTH (item_ptr) = sizeof (VPID);
	      *index_area = item_ptr;
	      numrecs++;

	      error =
		btsort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs,
				  REC_BIGONE);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* Prepare for the next internal sorting run */
	      numrecs = 0;
	      item_ptr = sort_param->internal_memory + BTSORT_RECORD_LENGTH_SIZE;
	      index_area = (char **) (output_buffer - sizeof (char *));
	      index_buff = index_area - 1;
	      temp_recdes.area_size = BTSORT_MAXREC_LENGTH;

	      /* Switch to the next Temp file */
	      if (++out_curfile >= sort_param->half_files)
		{
		  out_curfile = sort_param->in_half;
		}
	    }
	  break;

	case BTSORT_SUCCESS:
	  /* Proceed the pointers */
	  BTSORT_RECORD_LENGTH (item_ptr) = temp_recdes.length;
	  *index_area = item_ptr;
	  numrecs++;

	  index_area--;
	  index_buff--;		/* decrease once for pointer, once for pointer buffer */
	  index_buff--;		/* must keep track because index_buff is used to detect when sort buffer is full */

	  item_ptr += DB_ALIGN (temp_recdes.length, MAX_ALIGNMENT) + BTSORT_RECORD_LENGTH_SIZE;
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

      index_area = btsort_run_sort (thread_p, sort_param, index_area, numrecs, index_buff, &numrecs);
      *total_numrecs += numrecs;

      if (index_area == NULL || numrecs < 0)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      if (sort_param->tot_runs > 0 || BTSORT_IS_PARALLEL (sort_param))
	{
	  /* There has been other runs produced already */

	  error =
	    btsort_run_flush (thread_p, sort_param, out_curfile, cur_page, output_buffer, index_area, numrecs,
			      REC_HOME);
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
	      temp_recdes.length = BTSORT_RECORD_LENGTH (index_area[i]);

	      error = (*sort_param->put_fn) (thread_p, &temp_recdes, sort_param->put_arg);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}
    }
  else if (sort_param->tot_runs == 1 && !BTSORT_IS_PARALLEL (sort_param))
    {
      if (once_flushed)
	{
	  for (i = 0; i < saved_numrecs; i++)
	    {
	      /* Obtain the output record for this temporary record */
	      temp_recdes.data = saved_index_area[i];
	      temp_recdes.length = BTSORT_RECORD_LENGTH (saved_index_area[i]);

	      error = (*sort_param->put_fn) (thread_p, &temp_recdes, sort_param->put_arg);
	      if (error != NO_ERROR)
		{
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
	   * be an easy way to restructure the existing btsort_inphase_sort code to
	   * cope with the situation.  Just go get the record and be happy...
	   */
	  error = NO_ERROR;
	  temp_recdes.data = NULL;
	  long_recdes.area_size = 0;
	  if (long_recdes.data)
	    {
	      free_and_init (long_recdes.data);
	    }

	  if (btsort_read_area (thread_p, &sort_param->temp[0], &sort_param->temp_cursor[0], 0, 1, output_buffer) !=
	      NO_ERROR || btsort_spage_get_record (output_buffer, 0, &temp_recdes, PEEK) != S_SUCCESS
	      || btsort_retrieve_longrec (thread_p, &temp_recdes, &long_recdes) == NULL
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
 * btsort_run_flush () - Flush run
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
btsort_run_flush (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, int out_file, int *cur_page, char *output_buffer,
		  char **index_area, int numrecs, int rec_type)
{
  int error = NO_ERROR;
  int run_size;
  RECDES out_recdes;
  int i;
  BTSORT_REC *key, *next;
  int flushed_items = 0;
  int should_continue = true;

  /* Make sure the the temp file indexed by out_file has been created; if not, create it now. */
  if (sort_param->temp[out_file].volid == NULL_VOLID)
    {
      error =
	btsort_add_new_file (thread_p, &sort_param->temp[out_file], &sort_param->temp_cursor[out_file],
			     sort_param->tmp_file_pgs, false, sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* Store the record type; used for REC_BIGONE record types */
  out_recdes.type = rec_type;

  run_size = 0;
  btsort_spage_initialize (output_buffer);

  /* Insert each record to the output buffer and flush the buffer when it is full */
  for (i = 0; i < numrecs && should_continue; i++)
    {
      /* Traverse next link */
      for (key = (BTSORT_REC *) index_area[i]; key; key = next)
	{
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
	  out_recdes.length = BTSORT_RECORD_LENGTH ((char *) key);

	  if (btsort_spage_insert (output_buffer, &out_recdes) == NULL_SLOTID)
	    {
	      /* Output buffer is full */
	      error =
		btsort_write_area (thread_p, &sort_param->temp[out_file], &sort_param->temp_cursor[out_file],
				   cur_page[out_file], 1, output_buffer, sort_param->tde_encrypted);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      cur_page[out_file]++;
	      run_size++;
	      btsort_spage_initialize (output_buffer);

	      if (btsort_spage_insert (output_buffer, &out_recdes) == NULL_SLOTID)
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

  if (btsort_spage_get_numrecs (output_buffer))
    {
      /* Flush the partially full output page */
      error =
	btsort_write_area (thread_p, &sort_param->temp[out_file], &sort_param->temp_cursor[out_file],
			   cur_page[out_file], 1, output_buffer, sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* Update sort parameters */
      cur_page[out_file]++;
      run_size++;
    }

  /* Record the insertion of the new pages of the file to global parameters */
  error = btsort_run_add_new (&sort_param->file_contents[out_file], run_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  sort_param->tot_runs++;

  return NO_ERROR;
}

/*
 * btsort_retrieve_longrec () -
 *   return:
 *   address(in):
 *   memory(in):
 */
static char *
btsort_retrieve_longrec (THREAD_ENTRY * thread_p, RECDES * address, RECDES * memory)
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

#if defined(SERVER_MODE)
/*
 * btsort_put_result_from_tmpfile () - put result from last temp file
 *   return:
 *   sort_param(in): sort parameters
 *
 */
static int
btsort_put_result_from_tmpfile (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, int start_pagenum)
{
  int tot_pages;
  int current_pages = start_pagenum, read_pages = 0, cur_read_pages = 0;
  int slot_num = 0;
  char *cur_pgptr;
  int result_file_idx = sort_param->px_result_file_idx;
  RECDES record = RECDES_INITIALIZER;
  RECDES long_record = RECDES_INITIALIZER;
  int error = NO_ERROR;
  BTSORT_REC *sort_rec;

  tot_pages = sort_param->file_contents[result_file_idx].num_pages[0];
  while (tot_pages > 0)
    {
      read_pages = (tot_pages > sort_param->tot_buffers) ? sort_param->tot_buffers : tot_pages;

      error =
	btsort_read_area (thread_p, &sort_param->temp[result_file_idx], &sort_param->temp_cursor[result_file_idx],
			  current_pages, read_pages, sort_param->internal_memory);
      if (error != NO_ERROR)
	{
	  goto bailout;
	}

      cur_pgptr = sort_param->internal_memory;
      cur_read_pages = read_pages;
      while (cur_read_pages > 0)
	{
	  /* read record from sort temp file */
	  slot_num = btsort_spage_get_numrecs (cur_pgptr);
	  for (int i = 0; i < slot_num; i++)
	    {
	      if (btsort_spage_get_record (cur_pgptr, i, &record, PEEK) != S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (record.type == REC_BIGONE)
		{
		  if (btsort_retrieve_longrec (thread_p, &record, &long_record) == NULL)
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
		  sort_rec = (BTSORT_REC *) (record.data);
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
  if (long_record.data != NULL)
    {
      free_and_init (long_record.data);
    }
  return error;
}

/*
 * btsort_index_page_decode_key () - decode the index key of the record at slot of an already-loaded run page.
 *   BIGONE records are assembled from the multipage file first.  The key value owns its memory
 *   (bt_load_decode_sort_record_key reads it with copy semantics), so it stays valid after the page buffer
 *   is overwritten; the caller must pr_clear_value() it.
 */
static int
btsort_index_page_decode_key (THREAD_ENTRY * thread_p, char *pgbuf, int slot, LOAD_ARGS * load_args, DB_VALUE * key)
{
  RECDES record = RECDES_INITIALIZER;
  RECDES long_record = RECDES_INITIALIZER;
  int error;

  if (btsort_spage_get_record (pgbuf, slot, &record, PEEK) != S_SUCCESS)
    {
      return ER_SORT_TEMP_PAGE_CORRUPTED;
    }
  if (record.type == REC_BIGONE)
    {
      if (btsort_retrieve_longrec (thread_p, &record, &long_record) == NULL)
	{
	  return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	}
      error = bt_load_decode_sort_record_key (thread_p, &long_record, load_args, key);
      free_and_init (long_record.data);
    }
  else
    {
      error = bt_load_decode_sort_record_key (thread_p, &record, load_args, key);
    }
  return error;
}

static int
btsort_index_run_decode_key_at (THREAD_ENTRY * thread_p, VFID * temp, FILE_FIND_NTH_CURSOR * cursor, char *iomem,
				LOAD_ARGS * load_args, int page, int slot, DB_VALUE * key)
{
  int error = btsort_read_area (thread_p, temp, cursor, page, 1, iomem);
  if (error != NO_ERROR)
    {
      return error;
    }
  return btsort_index_page_decode_key (thread_p, iomem, slot, load_args, key);
}

/*
 * btsort_px_run_lower_bound () - first (page, slot) of the run whose key is >= splitter; (npages, 0) when every
 *   record's key is smaller.  Records with equal keys never straddle the returned position in different
 *   directions, so partitioning every run by the same splitter keys keeps each duplicate-key group whole.
 */
static int
btsort_px_run_lower_bound (THREAD_ENTRY * thread_p, VFID * temp, FILE_FIND_NTH_CURSOR * cursor, int npages,
			   char *iomem, LOAD_ARGS * load_args, TP_DOMAIN * key_type, DB_VALUE * splitter, int *page_out,
			   int *slot_out)
{
  DB_VALUE key;
  int lo, hi, first_ge_page;
  int cmp;
  int error;

  db_make_null (&key);

  /* first page whose FIRST key is >= splitter */
  lo = 0;
  hi = npages - 1;
  first_ge_page = npages;
  while (lo <= hi)
    {
      int mid = lo + (hi - lo) / 2;
      error = btsort_index_run_decode_key_at (thread_p, temp, cursor, iomem, load_args, mid, 0, &key);
      if (error != NO_ERROR)
	{
	  return error;
	}
      cmp = btree_compare_key (splitter, &key, key_type, 0, 1, NULL);
      pr_clear_value (&key);
      if (cmp == DB_GT)
	{
	  lo = mid + 1;
	}
      else if (cmp == DB_EQ || cmp == DB_LT)
	{
	  first_ge_page = mid;
	  hi = mid - 1;
	}
      else
	{
	  return ER_FAILED;
	}
    }

  if (first_ge_page == 0)
    {
      *page_out = 0;
      *slot_out = 0;
      return NO_ERROR;
    }

  /* the boundary lies inside page (first_ge_page - 1) or exactly at (first_ge_page, 0) */
  {
    int p = first_ge_page - 1;
    int nrecs, first_ge_slot;

    error = btsort_read_area (thread_p, temp, cursor, p, 1, iomem);
    if (error != NO_ERROR)
      {
	return error;
      }
    nrecs = btsort_spage_get_numrecs (iomem);
    if (nrecs <= 0)
      {
	assert (false);
	return ER_SORT_TEMP_PAGE_CORRUPTED;
      }
    /* slot 0's key is known < splitter (page binary search) */
    lo = 1;
    hi = nrecs - 1;
    first_ge_slot = nrecs;
    while (lo <= hi)
      {
	int mid = lo + (hi - lo) / 2;
	error = btsort_index_page_decode_key (thread_p, iomem, mid, load_args, &key);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	cmp = btree_compare_key (splitter, &key, key_type, 0, 1, NULL);
	pr_clear_value (&key);
	if (cmp == DB_GT)
	  {
	    lo = mid + 1;
	  }
	else if (cmp == DB_EQ || cmp == DB_LT)
	  {
	    first_ge_slot = mid;
	    hi = mid - 1;
	  }
	else
	  {
	    return ER_FAILED;
	  }
      }
    if (first_ge_slot >= nrecs)
      {
	*page_out = p + 1;
	*slot_out = 0;
      }
    else
      {
	*page_out = p;
	*slot_out = first_ge_slot;
      }
  }
  return NO_ERROR;
}

/* oversampling factor for global weighted-quantile splitter selection: each run contributes up to
 * c * (parallel_num - 1) page-proportional key samples. */
#define BTSORT_PX_SPLITTER_OVERSAMPLE 4

typedef struct btsort_px_splitter_cand BTSORT_PX_SPLITTER_CAND;
struct btsort_px_splitter_cand
{
  DB_VALUE key;			/* decoded sample key; owned here until moved into splitters[] */
  INT64 weight;			/* fixed-point page mass: (run_npages << 16) / n_pos of its run */
  bool moved;			/* ownership transferred to splitters[] -- must not be cleared here */
};

typedef struct btsort_px_key_group BTSORT_PX_KEY_GROUP;
struct btsort_px_key_group
{
  int cand_idx;			/* representative candidate (first in merge order); key stays in cand[] */
  INT64 weight;			/* accumulated weight of all equal-key candidates */
};

/*
 * btsort_px_select_splitters () - choose up to parallel_num - 1 strictly increasing distinct splitter keys from
 *   page-proportional samples of EVERY run (weighted global quantiles).  A single hot key group whose weight
 *   reaches W / parallel_num is atomically isolated into a dedicated shard (its start/end boundary keys are
 *   force-emitted under a reserved budget), which is the optimum reachable under group atomicity.
 *   Correctness never depends on where splitters come from: every run is cut by the same keys with
 *   btsort_px_run_lower_bound, so no duplicate-key group is ever split.  Any decode/compare error fails the
 *   parallel build (no serial fallback).  Selected keys own their memory; the caller clears them.
 */
static int
btsort_px_select_splitters (THREAD_ENTRY * thread_p, VFID * run_temp, const int *run_npages, int n_runs,
			    char *iomem, LOAD_ARGS * load_args, TP_DOMAIN * key_type, int parallel_num,
			    DB_VALUE * splitters, int *n_splitters)
{
  BTSORT_PX_SPLITTER_CAND *cand = NULL;
  BTSORT_PX_KEY_GROUP *grp = NULL;
  int *hot_order = NULL;
  bool *forced = NULL;
  bool *iso = NULL;
  int cand_begin[BTSORT_MAX_PARALLEL], cand_end[BTSORT_MAX_PARALLEL], head[BTSORT_MAX_PARALLEL];
  /* one page lookup position per run. this runs on the main thread only, so plain locals are enough. */
  FILE_FIND_NTH_CURSOR run_cursor[BTSORT_MAX_PARALLEL];
  int n_cand = 0, k = 0, n_hot = 0, n_iso = 0, used = 0, m = 0;
  const int B = parallel_num - 1;
  INT64 W = 0, iso_mass = 0, thr;
  int error = NO_ERROR;

  *n_splitters = 0;
  assert (n_runs >= 1 && n_runs <= BTSORT_MAX_PARALLEL && parallel_num >= 2);

  for (int r = 0; r < n_runs; r++)
    {
      int n_pos = MIN (BTSORT_PX_SPLITTER_OVERSAMPLE * B, run_npages[r]);
      assert (n_pos >= 1);
      file_find_nth_cursor_reset (&run_cursor[r]);
      cand_begin[r] = n_cand;
      n_cand += n_pos;
      cand_end[r] = n_cand;
    }

  /* cand must be allocated AND fully initialized before any other allocation can fail: the common cleanup
   * path reads cand[i].moved and clears cand[i].key for every element, so reaching it with a partially
   * initialized cand[] would touch garbage. */
  cand = (BTSORT_PX_SPLITTER_CAND *) malloc ((size_t) n_cand * sizeof (BTSORT_PX_SPLITTER_CAND));
  if (cand == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) n_cand * sizeof (BTSORT_PX_SPLITTER_CAND));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }
  for (int i = 0; i < n_cand; i++)
    {
      db_make_null (&cand[i].key);
      cand[i].moved = false;
    }
  grp = (BTSORT_PX_KEY_GROUP *) malloc ((size_t) n_cand * sizeof (BTSORT_PX_KEY_GROUP));
  hot_order = (int *) malloc ((size_t) n_cand * sizeof (int));
  forced = (bool *) malloc (((size_t) n_cand + 1) * sizeof (bool));
  iso = (bool *) malloc ((size_t) n_cand * sizeof (bool));
  if (grp == NULL || hot_order == NULL || forced == NULL || iso == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) n_cand * sizeof (BTSORT_PX_KEY_GROUP));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }
  memset (forced, 0, ((size_t) n_cand + 1) * sizeof (bool));
  memset (iso, 0, (size_t) n_cand * sizeof (bool));

  /* candidate collection: page-proportional slot-0 keys; per-run candidate lists are ascending by key
   * because each run is sorted. */
  for (int r = 0; r < n_runs; r++)
    {
      int n_pos = cand_end[r] - cand_begin[r];
      INT64 w = (((INT64) run_npages[r]) << 16) / n_pos;
      for (int i = 0; i < n_pos; i++)
	{
	  int page = (int) (((INT64) run_npages[r] * i) / n_pos);
	  BTSORT_PX_SPLITTER_CAND *c = &cand[cand_begin[r] + i];
	  error = btsort_index_run_decode_key_at (thread_p, &run_temp[r], &run_cursor[r], iomem, load_args, page, 0,
						  &c->key);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	  c->weight = w;
	  W += w;
	}
      head[r] = cand_begin[r];
    }

  /* n_runs-way merge of the per-run ascending lists, aggregating equal keys into distinct groups grp[0..k).
   * Every comparison can fail immediately (DB_UNK -> build failure) -- no error-blind qsort comparator. */
  for (int done = 0; done < n_cand; done++)
    {
      int best = -1;
      for (int r = 0; r < n_runs; r++)
	{
	  int cmp;
	  if (head[r] >= cand_end[r])
	    {
	      continue;
	    }
	  if (best < 0)
	    {
	      best = r;
	      continue;
	    }
	  cmp = btree_compare_key (&cand[head[r]].key, &cand[head[best]].key, key_type, 0, 1, NULL);
	  if (cmp == DB_LT)
	    {
	      best = r;		/* DB_EQ keeps the lower run index: stable and deterministic */
	    }
	  else if (cmp != DB_GT && cmp != DB_EQ)
	    {
	      error = ER_FAILED;
	      goto cleanup;
	    }
	}
      assert (best >= 0);
      {
	int idx = head[best]++;
	if (k > 0)
	  {
	    int cmp = btree_compare_key (&cand[grp[k - 1].cand_idx].key, &cand[idx].key, key_type, 0, 1, NULL);
	    if (cmp == DB_EQ)
	      {
		grp[k - 1].weight += cand[idx].weight;
		continue;
	      }
	    if (cmp != DB_LT)
	      {
		error = ER_FAILED;
		goto cleanup;
	      }
	  }
	grp[k].cand_idx = idx;
	grp[k].weight = cand[idx].weight;
	k++;
      }
    }
  assert (k >= 1);

  /* Pass 1 -- atomic hot admission (all-or-nothing per hot group; shared boundaries counted once).
   * Priority: weight descending, ties by ascending group index -- deterministic. */
  thr = W / parallel_num;
  if (thr > 0)
    {
      for (int j = 0; j < k; j++)
	{
	  if (grp[j].weight >= thr)
	    {
	      hot_order[n_hot++] = j;
	    }
	}
    }
  for (int a = 1; a < n_hot; a++)
    {
      int j = hot_order[a];
      int b = a - 1;
      while (b >= 0 && (grp[hot_order[b]].weight < grp[j].weight
			|| (grp[hot_order[b]].weight == grp[j].weight && hot_order[b] > j)))
	{
	  hot_order[b + 1] = hot_order[b];
	  b--;
	}
      hot_order[b + 1] = j;
    }
  for (int a = 0; a < n_hot; a++)
    {
      int j = hot_order[a];
      int need = (j > 0 && !forced[j] ? 1 : 0) + (j < k - 1 && !forced[j + 1] ? 1 : 0);
      if (used + need > B)
	{
	  continue;		/* not isolated at all -- ordinary mass; partial isolation is prohibited */
	}
      if (j > 0)
	{
	  forced[j] = true;
	}
      if (j < k - 1)
	{
	  forced[j + 1] = true;
	}
      used += need;
      iso[j] = true;
      n_iso++;
      iso_mass += grp[j].weight;
    }

  /* Pass 2 -- boundary emission.  Invariant m + F <= B reserves budget so every forced boundary is emitted
   * (no silent partial isolation); every group's mass is attributed exactly once regardless of emission. */
  {
    int last_j = -1, F = used, attributed = 0, S = parallel_num - n_iso;
    INT64 M = W - iso_mass, acc = 0, consumed = 0;

    for (int j = 1; j < k; j++)
      {
	bool take_f, take_g;
	if (!iso[j - 1])
	  {
	    acc += grp[j - 1].weight;
	    attributed++;
	  }
	take_f = forced[j];
	take_g = (!take_f && S >= 2 && acc >= M / S && m < B - F);
	if (!take_f && !take_g)
	  {
	    continue;
	  }
	assert (j > last_j && m < B);
	if (m > 0)
	  {
	    /* strictly-increasing check BEFORE the move: splitters[m - 1] owns the previous key, the source
	     * candidate is untouched.  Distinct ascending groups make a non-DB_LT result impossible unless
	     * the comparison itself failed -- treat it as a hard error either way. */
	    int cmp = btree_compare_key (&splitters[m - 1], &cand[grp[j].cand_idx].key, key_type, 0, 1, NULL);
	    if (cmp != DB_LT)
	      {
		error = ER_FAILED;
		goto cleanup;
	      }
	  }
	splitters[m] = cand[grp[j].cand_idx].key;	/* struct move -- source is never referenced again */
	cand[grp[j].cand_idx].moved = true;
	m++;
	last_j = j;
	if (take_f)
	  {
	    F--;
	  }
	if (acc > 0)
	  {
	    consumed += acc;	/* a non-hot segment just became a shard */
	    M -= acc;
	    S = MAX (S - 1, 1);
	    acc = 0;
	  }
      }
    if (!iso[k - 1])
      {
	attributed++;		/* the last group needs no boundary; it belongs to the last shard */
      }
    attributed += n_iso;
    assert (F == 0);		/* forced subset of emitted: m + F <= B held throughout */
    assert (m <= B);
    assert (attributed == k);	/* every group attributed exactly once */
    assert (consumed + acc + (iso[k - 1] ? 0 : grp[k - 1].weight) + iso_mass == W);	/* mass conservation */
  }

  /* m == 0 (no usable splitter: every sampled candidate fell into one key group) -> caller demotes to the legacy
   * serial path.  Candidates are only the slot-0 keys of page-proportional positions, so this does not imply that
   * the runs hold a single distinct key. */
  *n_splitters = m;

cleanup:
  if (cand != NULL)
    {
      for (int i = 0; i < n_cand; i++)
	{
	  if (!cand[i].moved)
	    {
	      pr_clear_value (&cand[i].key);
	    }
	}
      free_and_init (cand);
    }
  if (grp != NULL)
    {
      free_and_init (grp);
    }
  if (hot_order != NULL)
    {
      free_and_init (hot_order);
    }
  if (forced != NULL)
    {
      free_and_init (forced);
    }
  if (iso != NULL)
    {
      free_and_init (iso);
    }
  /* on error, keys already moved into splitters[] are cleared by the caller's common exit path */
  return error;
}

static void
btsort_px_free_shard_inputs (BTSORT_PX_MERGE_INPUT ** shard_inputs, int n_shards)
{
  for (int s = 0; s < n_shards; s++)
    {
      if (shard_inputs[s] != NULL)
	{
	  free_and_init (shard_inputs[s]);
	}
    }
}

/*
 * btsort_px_slice_runs_index_leaf () - key-partition every worker run into up to parallel_num shards without
 *   merging them first.  On success with *n_shards_out >= 2, shard_inputs[s] is a malloc'ed array of
 *   *n_runs_out slices ([start, end) coordinates per run); otherwise nothing is allocated and the caller
 *   falls back to the legacy single-run path.
 */
static int
btsort_px_slice_runs_index_leaf (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param,
				 int parallel_num, BTSORT_PX_MERGE_INPUT ** shard_inputs, int *n_runs_out,
				 int *n_shards_out, INT64 * total_pages_out)
{
  VFID run_temp[BTSORT_MAX_PARALLEL];
  int run_npages[BTSORT_MAX_PARALLEL];
  /* one page lookup position per run, see btsort_px_select_splitters */
  FILE_FIND_NTH_CURSOR run_cursor[BTSORT_MAX_PARALLEL];
  DB_VALUE splitters[BTSORT_MAX_PARALLEL];
  int *bp_page = NULL, *bp_slot = NULL;
  LOAD_ARGS *load_args = (LOAD_ARGS *) sort_param->put_arg;
  TP_DOMAIN *key_type = ((SORT_ARGS *) sort_param->get_arg)->key_type;
  char *iomem = sort_param->internal_memory;
  int n_runs = 0, m = 0, n_shards;
  INT64 total_pages = 0;
  int error = NO_ERROR;

  *n_runs_out = 0;
  *n_shards_out = 0;
  *total_pages_out = 0;

  for (int i = 0; i < parallel_num; i++)
    {
      int idx = px_sort_param[i].px_result_file_idx;
      int npages = px_sort_param[i].file_contents[idx].num_pages[0];
      if (npages <= 0)
	{
	  continue;
	}
      run_temp[n_runs] = px_sort_param[i].temp[idx];
      run_npages[n_runs] = npages;
      file_find_nth_cursor_reset (&run_cursor[n_runs]);
      total_pages += npages;
      n_runs++;
    }
  *n_runs_out = n_runs;
  *total_pages_out = total_pages;
  if (n_runs == 0)
    {
      return NO_ERROR;
    }

  for (int i = 0; i < parallel_num; i++)
    {
      db_make_null (&splitters[i]);
    }

  error = btsort_px_select_splitters (thread_p, run_temp, run_npages, n_runs, iomem, load_args, key_type,
				      parallel_num, splitters, &m);
  if (error != NO_ERROR)
    {
      goto end;
    }
  if (m == 0)
    {
      *n_shards_out = 1;
      goto end;
    }

  bp_page = (int *) malloc ((size_t) n_runs * m * sizeof (int));
  bp_slot = (int *) malloc ((size_t) n_runs * m * sizeof (int));
  if (bp_page == NULL || bp_slot == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) n_runs * m * sizeof (int));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  for (int r = 0; r < n_runs && error == NO_ERROR; r++)
    {
      for (int b = 0; b < m; b++)
	{
	  error =
	    btsort_px_run_lower_bound (thread_p, &run_temp[r], &run_cursor[r], run_npages[r], iomem, load_args,
				       key_type, &splitters[b], &bp_page[r * m + b], &bp_slot[r * m + b]);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  assert (b == 0 || bp_page[r * m + b - 1] < bp_page[r * m + b]
		  || (bp_page[r * m + b - 1] == bp_page[r * m + b] && bp_slot[r * m + b - 1] <= bp_slot[r * m + b]));
	}
    }
  if (error != NO_ERROR)
    {
      goto end;
    }

  n_shards = m + 1;
  for (int s = 0; s < n_shards; s++)
    {
      BTSORT_PX_MERGE_INPUT *arr = (BTSORT_PX_MERGE_INPUT *) malloc ((size_t) n_runs * sizeof (BTSORT_PX_MERGE_INPUT));
      if (arr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) n_runs * sizeof (BTSORT_PX_MERGE_INPUT));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  btsort_px_free_shard_inputs (shard_inputs, s);
	  goto end;
	}
      for (int r = 0; r < n_runs; r++)
	{
	  arr[r].temp = run_temp[r];
	  arr[r].npages = run_npages[r];
	  /* every shard walks its own slice of this run, so it needs its own page lookup position */
	  file_find_nth_cursor_reset (&arr[r].cursor);
	  if (s == 0)
	    {
	      arr[r].range.start_page = 0;
	      arr[r].range.start_slot = 0;
	    }
	  else
	    {
	      arr[r].range.start_page = bp_page[r * m + s - 1];
	      arr[r].range.start_slot = bp_slot[r * m + s - 1];
	    }
	  if (s == n_shards - 1)
	    {
	      arr[r].range.end_page = run_npages[r];
	      arr[r].range.end_slot = 0;
	    }
	  else
	    {
	      arr[r].range.end_page = bp_page[r * m + s];
	      arr[r].range.end_slot = bp_slot[r * m + s];
	    }
	}
      shard_inputs[s] = arr;
    }
  *n_shards_out = n_shards;

end:
  for (int i = 0; i < parallel_num; i++)
    {
      pr_clear_value (&splitters[i]);
    }
  if (bp_page != NULL)
    {
      free_and_init (bp_page);
    }
  if (bp_slot != NULL)
    {
      free_and_init (bp_slot);
    }
  return error;
}

/*
 * merge-put cursor primitives for btsort_put_result_index_leaf.  A slice is the half-open coordinate interval
 * [start, end); end_slot == 0 means the slice ends at the last record of end_page - 1.
 */
static bool
btsort_px_merge_cursor_done (const BTSORT_PX_MERGE_INPUT * input, int page, int slot)
{
  return page > input->range.end_page || (page == input->range.end_page && slot >= input->range.end_slot);
}

static int
btsort_px_merge_cursor_fetch (THREAD_ENTRY * thread_p, char *pgbuf, int slot, RECDES * rec, RECDES * longrec)
{
  if (btsort_spage_get_record (pgbuf, slot, rec, PEEK) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
      return ER_SORT_TEMP_PAGE_CORRUPTED;
    }
  if (rec->type == REC_BIGONE)
    {
      /* longrec's buffer is grown/reused across records by btsort_retrieve_longrec; freed once by the caller */
      if (btsort_retrieve_longrec (thread_p, rec, longrec) == NULL)
	{
	  return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	}
    }
  return NO_ERROR;
}

static bool
btsort_px_merge_cursor_less (BTSORT_CMP_FUNC * compare, void *compare_arg, RECDES * cur_rec, RECDES * long_rec,
			     int a, int b)
{
  char **data1 = (cur_rec[a].type == REC_BIGONE) ? &long_rec[a].data : &cur_rec[a].data;
  char **data2 = (cur_rec[b].type == REC_BIGONE) ? &long_rec[b].data : &cur_rec[b].data;
  return (*compare) (data1, data2, compare_arg) < 0;
}

static void
btsort_px_merge_heap_sift_down (int *heap, int heap_n, int pos, BTSORT_CMP_FUNC * compare, void *compare_arg,
				RECDES * cur_rec, RECDES * long_rec)
{
  for (;;)
    {
      int smallest = pos;
      int l = 2 * pos + 1;
      int r = 2 * pos + 2;
      int tmp;
      if (l < heap_n && btsort_px_merge_cursor_less (compare, compare_arg, cur_rec, long_rec, heap[l], heap[smallest]))
	{
	  smallest = l;
	}
      if (r < heap_n && btsort_px_merge_cursor_less (compare, compare_arg, cur_rec, long_rec, heap[r], heap[smallest]))
	{
	  smallest = r;
	}
      if (smallest == pos)
	{
	  break;
	}
      tmp = heap[pos];
      heap[pos] = heap[smallest];
      heap[smallest] = tmp;
      pos = smallest;
    }
}
#endif /* SERVER_MODE */

/*
 * btsort_exphase_merge () - Merge phase
 *   return:
 *   sort_param(in): sort parameters
 *
 */
static int
btsort_exphase_merge (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param)
{
  /* Variables for input files */
  int act_infiles;		/* How many of input files are active */
  int pre_act_infiles;		/* Number of active input files in the previous iteration */
  int in_sectsize;		/* Size of section allocated to each active input file (in terms of number of buffers
				 * it contains) */
  int read_pages;		/* Number of pages read in to fill the input buffer */
  int in_act_bufno[BTSORT_MAX_HALF_FILES];	/* Active buffer in the input section */
  int in_last_buf[BTSORT_MAX_HALF_FILES];	/* Last full buffer of the input section */
  int act_slot[BTSORT_MAX_HALF_FILES];	/* Active slot of the active buffer of input section */
  int last_slot[BTSORT_MAX_HALF_FILES];	/* Last slot of the active buffer of the input section */

  char *in_sectaddr[BTSORT_MAX_HALF_FILES];	/* Beginning address of each input section */
  char *in_cur_bufaddr[BTSORT_MAX_HALF_FILES];	/* Address of the current buffer in each input section */

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
  RECDES smallest_elem_ptr[BTSORT_MAX_HALF_FILES];
  RECDES long_recdes[BTSORT_MAX_HALF_FILES];

  int cur_page[2 * BTSORT_MAX_HALF_FILES];	/* Current page of each temp file */
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

  BTSORT_CMP_FUNC *compare;
  void *compare_arg;

  BTSORT_REC_LIST sr_list[BTSORT_MAX_HALF_FILES], *min_p, *s, *p;
  int tmp_pos;			/* temporary value for rec_pos swapping */
  bool do_swap;			/* rec_pos swapping indicator */

  RECDES last_elem_ptr;		/* last element pointers in one page of input section */
  RECDES last_long_recdes;
  bool last_elem_is_min;	/* false: must find min record true: last element in the current input section is min
				 * record. no need to find min */
  char **data1, **data2;
  BTSORT_REC *sort_rec;
  int first_run;
  int cmp;

  error = NO_ERROR;

  compare = sort_param->cmp_fn;
  compare_arg = sort_param->cmp_arg;

  for (i = 0; i < BTSORT_MAX_HALF_FILES; i++)
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

  /* While there are more than one input files with different runs to merge */
  while ((act_infiles = btsort_get_numpages_of_active_infiles (sort_param)) > 1)
    {
      /* Check if output files has enough pages; if not allocate new pages */
      error = btsort_checkalloc_numpages_of_outfiles (thread_p, sort_param);
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
      in_sectsize = btsort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
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
	  len = btsort_get_num_file_contents (&sort_param->file_contents[i]);
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
	      act_infiles = btsort_get_numpages_of_active_infiles (sort_param);
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

		      error = btsort_run_add_new (&sort_param->file_contents[cur_outfile], cp_pages);
		      if (error != NO_ERROR)
			{
			  goto bailout;
			}
		      btsort_run_remove_first (&sort_param->file_contents[act]);

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
			    btsort_read_area (thread_p, &sort_param->temp[act], &sort_param->temp_cursor[act],
					      cur_page[act], read_pages, sort_param->internal_memory);
			  if (error != NO_ERROR)
			    {
			      goto bailout;
			    }

			  cur_page[act] += read_pages;
			  error =
			    btsort_write_area (thread_p, &sort_param->temp[cur_outfile],
					       &sort_param->temp_cursor[cur_outfile], cur_page[cur_outfile], read_pages,
					       sort_param->internal_memory, sort_param->tde_encrypted);
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
		      in_sectsize = btsort_find_inbuf_size (sort_param->tot_buffers, act_infiles);
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
		btsort_read_area (thread_p, &sort_param->temp[big_index], &sort_param->temp_cursor[big_index],
				  cur_page[big_index], read_pages, in_sectaddr[i]);
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
	      last_slot[i] = btsort_spage_get_numrecs (in_cur_bufaddr[i]);

	      if (btsort_spage_get_record (in_cur_bufaddr[i], act_slot[i], &smallest_elem_ptr[i], PEEK) != S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[i].type == REC_BIGONE)
		{
		  if (btsort_retrieve_longrec (thread_p, &smallest_elem_ptr[i], &long_recdes[i]) == NULL)
		    {
		      ASSERT_ERROR ();
		      error = er_errid ();
		      goto bailout;
		    }
		}
	    }

	  for (i = 0, p = sr_list; i < (act_infiles - 1); p = p->next)
	    {
	      p->next = (BTSORT_REC_LIST *) ((char *) p + sizeof (BTSORT_REC_LIST));
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
	      if (btsort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
					   &last_elem_ptr, PEEK) != S_SUCCESS)
		{
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  goto bailout;
		}

	      /* if this is a long record then retrieve it */
	      if (last_elem_ptr.type == REC_BIGONE)
		{
		  if (btsort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
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
	      btsort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE));
	    }

	  /* Initialize the size of next run to zero */
	  out_runsize = 0;

	  /* In parallel sort, put_fn will be performed by the parent thread. save last file index. */
	  if (very_last_run && BTSORT_IS_PARALLEL (sort_param))
	    {
	      sort_param->px_result_file_idx = cur_outfile;
	    }

	  for (;;)
	    {
	      /* OUTPUT A RECORD */

	      /* FIND MINIMUM RECORD IN THE INPUT AREA */
	      min = min_p->rec_pos;

	      if (very_last_run && !BTSORT_IS_PARALLEL (sort_param))
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
		      sort_rec = (BTSORT_REC *) (smallest_elem_ptr[min].data);
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
		  if (btsort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
		    {
		      /* Current output buffer is full */

		      if (++out_act_bufno < out_sectsize)
			{
			  /* There is another buffer in the output section; so insert the new record there */
			  out_cur_bufaddr += DB_PAGESIZE;

			  if (btsort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
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
			    btsort_write_area (thread_p, &sort_param->temp[cur_outfile],
					       &sort_param->temp_cursor[cur_outfile], cur_page[cur_outfile],
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
			      btsort_spage_initialize (out_sectaddr + (i * DB_PAGESIZE));
			    }

			  if (btsort_spage_insert (out_cur_bufaddr, &smallest_elem_ptr[min]) == NULL_SLOTID)
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
			    btsort_read_area (thread_p, &sort_param->temp[big_index],
					      &sort_param->temp_cursor[big_index], cur_page[big_index], read_pages,
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
		  last_slot[min] = btsort_spage_get_numrecs (in_cur_bufaddr[min]);
		}

	      if (btsort_spage_get_record (in_cur_bufaddr[min], act_slot[min], &smallest_elem_ptr[min], PEEK) !=
		  S_SUCCESS)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_SORT_TEMP_PAGE_CORRUPTED, 0);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  goto bailout;
		}

	      /* If this is a long record retrieve it */
	      if (smallest_elem_ptr[min].type == REC_BIGONE)
		{
		  if (btsort_retrieve_longrec (thread_p, &smallest_elem_ptr[min], &long_recdes[min]) == NULL)
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
			  if (btsort_spage_get_record (in_cur_bufaddr[min_p->rec_pos], (last_slot[min_p->rec_pos] - 1),
						       &last_elem_ptr, PEEK) != S_SUCCESS)
			    {
			      error = ER_SORT_TEMP_PAGE_CORRUPTED;
			      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			      goto bailout;
			    }

			  /* if this is a long record retrieve it */
			  if (last_elem_ptr.type == REC_BIGONE)
			    {
			      if (btsort_retrieve_longrec (thread_p, &last_elem_ptr, &last_long_recdes) == NULL)
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

	  if (!(very_last_run && !BTSORT_IS_PARALLEL (sort_param)))
	    {
	      /* Flush whatever is left on the output section */

	      out_act_bufno++;	/* Since 0 refers to the first active buffer */
	      error =
		btsort_write_area (thread_p, &sort_param->temp[cur_outfile], &sort_param->temp_cursor[cur_outfile],
				   cur_page[cur_outfile], out_act_bufno, out_sectaddr, sort_param->tde_encrypted);
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
	      btsort_run_remove_first (&sort_param->file_contents[i]);
	    }

	  /* Add a new node to the file_contents list of the current output file */
	  error = btsort_run_add_new (&sort_param->file_contents[cur_outfile], out_runsize);
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

  return error;
}

/*
 * btsort_get_avg_numpages_of_nonempty_tmpfile () - Return average number of pages
 *                                       currently occupied by nonempty
 *                                       temporary file
 *   return:
 *   sort_param(in): Sort paramater
 */
static int
btsort_get_avg_numpages_of_nonempty_tmpfile (BTSORT_PARAM * sort_param)
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
 * btsort_return_used_resources () - Free the sort buffer, temp files and (for a worker) its SORT_ARGS copy
 *   return: void
 *   sort_param(in): sort parameters to release
 *   parallel_type(in): BTSORT_PX_SINGLE, BTSORT_PX_MAIN_IN_PARALLEL or BTSORT_PX_THREAD_IN_PARALLEL
 */
static void
btsort_return_used_resources (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param, BTSORT_PARALLEL_TYPE parallel_type)
{
  int k;

  if (sort_param == NULL)
    {
      return;			/* nop */
    }

  if (sort_param->internal_memory)
    {
      free_and_init (sort_param->internal_memory);
    }

  if (parallel_type == BTSORT_PX_SINGLE || parallel_type == BTSORT_PX_MAIN_IN_PARALLEL)
    {
      for (k = 0; k < sort_param->tot_tempfiles; k++)
	{
	  if (sort_param->temp[k].volid != NULL_VOLID)
	    {
	      (void) file_temp_retire (thread_p, &sort_param->temp[k]);
	      file_find_nth_cursor_reset (&sort_param->temp_cursor[k]);
	    }
	}
    }

  if (sort_param->multipage_file.volid != NULL_VOLID)
    {
      (void) file_temp_retire (thread_p, &(sort_param->multipage_file));
    }

  for (k = 0; k < BTSORT_MAX_TOT_FILES; k++)
    {
      if (sort_param->file_contents[k].num_pages != NULL)
	{
	  free_and_init (sort_param->file_contents[k].num_pages);
	}
    }

  if (parallel_type == BTSORT_PX_THREAD_IN_PARALLEL)
    {
#if defined (SERVER_MODE)
      if (sort_param->get_arg != NULL)
	{
	  SORT_ARGS *sort_args_p = (SORT_ARGS *) sort_param->get_arg;
	  if (sort_args_p->ftab_sets != NULL)
	    {
	      sort_args_p->ftab_sets->~vector ();
	      free_and_init (sort_args_p->ftab_sets);
	    }
	  free_and_init (sort_param->get_arg);
	}
#endif /* SERVER_MODE */
    }
}

/*
 * btsort_add_new_file () - Create a new temporary file for sorting purposes
 *   return: NO_ERROR
 *   vfid(in): Set to the created file identifier
 *   file_pg_cnt_est(in): Estimated file page count
 *   force_alloc(in): Allocate file pages now ?
 *   tde_encrypted(in): whether the file has to be encrypted or not for TDE
 */
static int
btsort_add_new_file (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor, int file_pg_cnt_est,
		     bool force_alloc, bool tde_encrypted)
{
  VPID new_vpid;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
  int ret = NO_ERROR;

  /* todo: sort file is a case I missed that seems to use file_find_nthpages. I don't know if it can be optimized to
   *       work without numerable files, that remains to be seen. */

  /* the slot is being handed a different file (a temporary file identifier is handed out again after the previous
   * one was retired), so a position remembered for the old occupant must not survive here. */
  file_find_nth_cursor_reset (cursor);

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
      ret = file_apply_tde_algorithm (thread_p, vfid, tde_algo);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  file_temp_retire (thread_p, vfid);
	  VFID_SET_NULL (vfid);
	  return ret;
	}
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
	  file_temp_retire (thread_p, vfid);
	  VFID_SET_NULL (vfid);
	  return ret;
	}
    }

  return NO_ERROR;
}

#if defined(SERVER_MODE)
/*
 * btsort_copy_sort_param () - copy sort param from src_param to dest_param
 *   return: NO_ERROR
 *   dest_param(in):
 *   src_param(in):
 *   parallel_num(in):
 */
static int
btsort_copy_sort_param (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param,
			int parallel_num)
{
  int error = NO_ERROR;
  int i, j;

  /* init stats */
  sort_param->ori_sort_param = NULL;

  /* copy from origin sort param */
  for (i = 0; i < parallel_num; i++)
    {
      memcpy (&px_sort_param[i], sort_param, sizeof (BTSORT_PARAM));
    }

  /* init */
  for (i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].internal_memory = NULL;
      /* The memcpy above shallow-copies main's caller-owned pointers into
       * every worker slot. If a later alloc in this function fails, cleanup
       * runs btsort_return_used_resources(BTSORT_PX_THREAD_IN_PARALLEL) which would
       * then free those pointers (e.g. main's stack-allocated GROUPBY_STATE)
       * as if they belonged to the worker. Null them out up front so the
       * cleanup short-circuits on the failure path. */
      px_sort_param[i].get_arg = NULL;
      px_sort_param[i].put_arg = NULL;
      px_sort_param[i].ori_sort_param = NULL;
      for (j = 0; j < BTSORT_MAX_TOT_FILES; j++)
	{
	  px_sort_param[i].file_contents[j].num_pages = NULL;
	  /* the copy above brought main's page lookup positions along. workers sort into their own files. */
	  file_find_nth_cursor_reset (&px_sort_param[i].temp_cursor[j]);
	}
    }

  /* For parallel sort, tot_buffers must be at least 8 (bump up if currently < 8) */
  if (sort_param->tot_buffers < 8)
    {
      char *new_internal_memory;
      int saved_tot_buffers = sort_param->tot_buffers;

      sort_param->tot_buffers = 8;
      new_internal_memory =
	(char *) realloc (sort_param->internal_memory, (size_t) sort_param->tot_buffers * (size_t) DB_PAGESIZE);
      if (new_internal_memory == NULL)
	{
	  sort_param->tot_buffers = saved_tot_buffers;
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) (8 * DB_PAGESIZE));
	  goto clear;
	}
      sort_param->internal_memory = new_internal_memory;
    }

  /* alloc new memory */
  for (i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].internal_memory = (char *) malloc ((size_t) sort_param->tot_buffers * (size_t) DB_PAGESIZE);
      if (px_sort_param[i].internal_memory == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) (sort_param->tot_buffers * DB_PAGESIZE));
	  goto clear;
	}
      px_sort_param[i].tot_buffers = sort_param->tot_buffers;	/* match allocated size (may be 8 for parallel) */
      for (j = 0; j < BTSORT_MAX_TOT_FILES; j++)
	{
	  /* Initilize file contents list */
	  px_sort_param[i].file_contents[j].num_pages = (int *) calloc (BTSORT_INITIAL_DYN_ARRAY_SIZE, sizeof (int));
	  if (px_sort_param[i].file_contents[j].num_pages == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		      (size_t) (BTSORT_INITIAL_DYN_ARRAY_SIZE * sizeof (int)));
	      goto clear;
	    }

	  px_sort_param[i].file_contents[j].num_slots = BTSORT_INITIAL_DYN_ARRAY_SIZE;
	  px_sort_param[i].file_contents[j].first_run = -1;
	  px_sort_param[i].file_contents[j].last_run = -1;
	}

      /* init px variable */
      px_sort_param[i].px_status = BTSORT_PX_PROGRESS;
      px_sort_param[i].px_parallel_num = parallel_num;
      px_sort_param[i].px_result_file_idx = 0;
      /* Copy the parent's thread_p. */
      px_sort_param[i].px_orig_thread_p =
	thread_p->m_px_orig_thread_entry ? thread_p->m_px_orig_thread_entry : thread_p;
      px_sort_param[i].ori_sort_param = sort_param;
      px_sort_param[i].main_error_context = &cuberr::context::get_thread_local_context ();
    }

clear:
  if (error != NO_ERROR)
    {
      /* free memory */
      for (i = 0; i < parallel_num; i++)
	{
	  if (px_sort_param[i].internal_memory != NULL)
	    {
	      free_and_init (px_sort_param[i].internal_memory);
	    }
	  for (j = 0; j < BTSORT_MAX_TOT_FILES; j++)
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

typedef struct btsort_merge_queue_ctx BTSORT_MERGE_QUEUE_CTX;
struct btsort_merge_queue_ctx
{
  BTSORT_RESULT_RUN run_queue[BTSORT_MAX_PARALLEL];
  int queue_head;
  int queue_tail;
  int queue_size;
  int in_flight;
  bool has_error;
  pthread_mutex_t mtx;
  pthread_cond_t done_cond;
  BTSORT_PARAM *sort_param;
  BTSORT_PARAM *px_sort_param;
  int pool_size;
  bool ctx_in_use[BTSORT_MAX_PARALLEL / 2 + 1];
  BTSORT_RESULT_RUN ctx_results[BTSORT_MAX_PARALLEL / 2 + 1];
};

static void
btsort_merge_queue_enqueue (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_RESULT_RUN run)
{
  qctx->run_queue[qctx->queue_tail] = run;
  qctx->queue_tail = (qctx->queue_tail + 1) % BTSORT_MAX_PARALLEL;
  qctx->queue_size++;
}

static BTSORT_RESULT_RUN
btsort_merge_queue_dequeue (BTSORT_MERGE_QUEUE_CTX * qctx)
{
  BTSORT_RESULT_RUN run = qctx->run_queue[qctx->queue_head];
  qctx->queue_head = (qctx->queue_head + 1) % BTSORT_MAX_PARALLEL;
  qctx->queue_size--;
  return run;
}

static int
btsort_merge_queue_acquire_ctx (BTSORT_MERGE_QUEUE_CTX * qctx)
{
  int i;
  for (i = 0; i < qctx->pool_size; i++)
    {
      if (!qctx->ctx_in_use[i])
	{
	  qctx->ctx_in_use[i] = true;
	  return i;
	}
    }
  return -1;
}

static void
btsort_merge_queue_release_ctx (BTSORT_MERGE_QUEUE_CTX * qctx, int idx)
{
  qctx->ctx_in_use[idx] = false;
}

static void
btsort_merge_queue_setup_ctx (int pool_idx, BTSORT_MERGE_QUEUE_CTX * qctx, const BTSORT_RESULT_RUN * runs, int k)
{
  int j;
  BTSORT_PARAM *ctx = &qctx->px_sort_param[pool_idx];

  assert (k >= 2 && k <= BTSORT_PX_MERGE_FILES);
  ctx->half_files = k;
  ctx->tot_tempfiles = k * 2;
  ctx->in_half = 0;
  ctx->px_result_file_idx = 0;
  for (j = 0; j < k; j++)
    {
      ctx->temp[j] = runs[j].temp_file;
      file_find_nth_cursor_reset (&ctx->temp_cursor[j]);
      ctx->file_contents[j].num_pages[0] = runs[j].num_pages;
      ctx->file_contents[j].first_run = 0;
      ctx->file_contents[j].last_run = 0;
    }
  for (j = k; j < k * 2; j++)
    {
      ctx->temp[j].volid = NULL_VOLID;
      file_find_nth_cursor_reset (&ctx->temp_cursor[j]);
      ctx->file_contents[j].first_run = -1;
      ctx->file_contents[j].last_run = -1;
    }
  ctx->px_result_run = &qctx->ctx_results[pool_idx];
  /* Clear stale value from a previous merge that reused this pool slot, so the
   * callback error branch can reliably detect whether btsort_merge_nruns produced
   * an output run that needs to be retired. */
  VFID_SET_NULL (&ctx->px_result_run->temp_file);
  ctx->px_result_run->num_pages = 0;
}

static void
btsort_merge_queue_ctx_init (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * sort_param,
			     BTSORT_PARAM * px_sort_param, int parallel_num)
{
  memset (qctx, 0, sizeof (*qctx));
  pthread_mutex_init (&qctx->mtx, NULL);
  pthread_cond_init (&qctx->done_cond, NULL);
  qctx->sort_param = sort_param;
  qctx->px_sort_param = px_sort_param;
  qctx->pool_size = parallel_num / BTSORT_PX_MERGE_FILES + 1;
}

static void
btsort_merge_queue_ctx_destroy (BTSORT_MERGE_QUEUE_CTX * qctx)
{
  pthread_mutex_destroy (&qctx->mtx);
  pthread_cond_destroy (&qctx->done_cond);
}

static void
btsort_merge_queue_enqueue_initial_runs (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * px_sort_param, int parallel_num)
{
  int i;
  for (i = 0; i < parallel_num; i++)
    {
      int npages = px_sort_param[i].file_contents[px_sort_param[i].px_result_file_idx].num_pages[0];
      if (npages > 0)
	{
	  BTSORT_RESULT_RUN run;
	  run.temp_file = px_sort_param[i].temp[px_sort_param[i].px_result_file_idx];
	  run.num_pages = npages;
	  btsort_merge_queue_enqueue (qctx, run);
	}
    }
}

/*
 * btsort_merge_queue_run () - drive the dispatcher + wait loop until all merges
 *   complete. On return the queue holds 0 or 1 run depending on input;
 *   the caller stages the final run.
 *
 *   On error, the queue is fully drained and every remaining run's temp file
 *   is retired. Workers do not retire temp[k] for BTSORT_PX_THREAD_IN_PARALLEL, so the
 *   queue is the sole owner of every enqueued run (initial or intermediate)
 *   once dispatch begins.
 */
static int
btsort_merge_queue_run (THREAD_ENTRY * thread_p, BTSORT_MERGE_QUEUE_CTX * qctx)
{
  pthread_mutex_lock (&qctx->mtx);
  while (qctx->queue_size >= 2)
    {
      btsort_merge_queue_try_dispatch (qctx);
    }
  /* On error path workers may still enqueue successful results, so queue_size > 1
   * can persist after in_flight reaches 0. Drain in-flight only and ignore the
   * queue when has_error is set to avoid deadlock. */
  while (qctx->in_flight > 0 || (!qctx->has_error && qctx->queue_size > 1))
    {
      pthread_cond_wait (&qctx->done_cond, &qctx->mtx);
    }

  if (qctx->has_error)
    {
      /* Drain the queue and retire each remaining run's temp file. */
      while (qctx->queue_size > 0)
	{
	  BTSORT_RESULT_RUN run = btsort_merge_queue_dequeue (qctx);
	  if (run.temp_file.volid != NULL_VOLID)
	    {
	      (void) file_temp_retire (thread_p, &run.temp_file);
	    }
	}
    }
  pthread_mutex_unlock (&qctx->mtx);

  /* Match BTSORT_WAIT_PARALLEL: ensure the worker pool drains any task accounting
   * that may still be pending after the callback returned. in_flight reaches 0
   * the moment a callback unlocks qctx->mtx, before the worker_manager fully
   * retires the task. */
  qctx->sort_param->px_worker_manager->wait_workers ();

  return qctx->has_error ? ER_FAILED : NO_ERROR;
}

static void
btsort_merge_queue_stage_final_run (BTSORT_MERGE_QUEUE_CTX * qctx, BTSORT_PARAM * dst)
{
  BTSORT_RESULT_RUN final_run = btsort_merge_queue_dequeue (qctx);
  dst->px_result_file_idx = 0;
  dst->temp[0] = final_run.temp_file;
  file_find_nth_cursor_reset (&dst->temp_cursor[0]);
  dst->file_contents[0].num_pages[0] = final_run.num_pages;
  dst->file_contents[0].first_run = 0;
  dst->file_contents[0].last_run = 0;
}

static void
btsort_merge_queue_try_dispatch (BTSORT_MERGE_QUEUE_CTX * qctx)
{
  BTSORT_RESULT_RUN runs[BTSORT_PX_MERGE_FILES];
  int k, j;
  int pool_idx;
  BTSORT_PARAM *ctx;
  parallel_query::callable_task * task;

  if (qctx->has_error)
    {
      if (qctx->in_flight == 0)
	{
	  pthread_cond_signal (&qctx->done_cond);
	}
      return;
    }

  if (qctx->queue_size >= 2)
    {
      /* Greedy k-way: consume up to BTSORT_PX_MERGE_FILES runs per merge to
       * minimize merge-tree depth (= fan-in I/O). k=2 is the smallest valid
       * merge and is used whenever the queue currently holds fewer than K. */
      k = MIN (qctx->queue_size, BTSORT_PX_MERGE_FILES);
      for (j = 0; j < k; j++)
	{
	  runs[j] = btsort_merge_queue_dequeue (qctx);
	}
      pool_idx = btsort_merge_queue_acquire_ctx (qctx);
      /* pool_size = parallel_num/BTSORT_PX_MERGE_FILES + 1 bounds simultaneous
       * merges, so acquire_ctx should never fail under the current invariant. */
      assert (pool_idx >= 0);
      btsort_merge_queue_setup_ctx (pool_idx, qctx, runs, k);
      qctx->in_flight++;
      ctx = &qctx->px_sort_param[pool_idx];
      task = new parallel_query::callable_task (qctx->sort_param->px_worker_manager,
						std::bind (btsort_merge_nruns_queue_cb,
							   std::placeholders::_1, ctx, qctx));
      qctx->sort_param->px_worker_manager->push_task (task);
    }
  else if (qctx->in_flight == 0)
    {
      pthread_cond_signal (&qctx->done_cond);
    }
}

static void
btsort_merge_nruns_queue_cb (cubthread::entry & thread_ref, BTSORT_PARAM * ctx, BTSORT_MERGE_QUEUE_CTX * qctx)
{
  THREAD_ENTRY *thread_p = &thread_ref;
  int error;
  int pool_idx;

  thread_ref.tran_index = ctx->px_orig_thread_p->tran_index;
  thread_ref.m_px_orig_thread_entry = ctx->px_orig_thread_p;
  thread_ref.conn_entry = ctx->px_orig_thread_p->conn_entry;

  thread_p->push_resource_tracks ();

  if (thread_is_on_trace (ctx->px_orig_thread_p))
    {
      thread_ref.on_trace = true;
      perfmon_initialize_parallel_stats (&thread_ref);
    }

  error = btsort_merge_nruns (thread_p, ctx);

  if (thread_is_on_trace (ctx->px_orig_thread_p))
    {
      perfmon_destroy_parallel_stats (&thread_ref);
    }
  thread_p->pop_resource_tracks ();

  pthread_mutex_lock (&qctx->mtx);
  if (error != NO_ERROR)
    {
      if (!qctx->has_error)
	{
	  qctx->has_error = true;
	  ctx->main_error_context->get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	}
      /* The failed merge's result run is not enqueued; queue drain on error
       * will not see it. Retire it here to avoid leaking the temp file. */
      if (ctx->px_result_run->temp_file.volid != NULL_VOLID)
	{
	  (void) file_temp_retire (thread_p, &ctx->px_result_run->temp_file);
	  VFID_SET_NULL (&ctx->px_result_run->temp_file);
	}
    }
  else
    {
      BTSORT_RESULT_RUN result = *ctx->px_result_run;
      btsort_merge_queue_enqueue (qctx, result);
    }
  pool_idx = (int) (ctx - qctx->px_sort_param);
  btsort_merge_queue_release_ctx (qctx, pool_idx);
  thread_ref.m_px_orig_thread_entry = NULL;
  qctx->in_flight--;
  btsort_merge_queue_try_dispatch (qctx);
  pthread_mutex_unlock (&qctx->mtx);
}

static void
btsort_put_result_index_leaf (cubthread::entry & thread_ref, BTSORT_PARAM * sort_param)
{
  THREAD_ENTRY *thread_p = &thread_ref;
  LOAD_ARGS *load_args = (LOAD_ARGS *) sort_param->put_arg;
  BTSORT_PX_MERGE_INPUT *inputs = sort_param->px_merge_inputs;
  const int n_inputs = sort_param->px_merge_n_inputs;
  BTSORT_CMP_FUNC *compare = sort_param->cmp_fn;
  void *compare_arg = sort_param->cmp_arg;
  char *buffers = NULL;
  int *heap = NULL;
  int *cur_page = NULL;
  int *cur_slot = NULL;
  int *nrecs = NULL;
  RECDES *cur_rec = NULL;
  RECDES *long_rec = NULL;
  int heap_n = 0;
  int c;
  int error = NO_ERROR;

  thread_ref.tran_index = sort_param->px_orig_thread_p->tran_index;
  thread_ref.m_px_orig_thread_entry = sort_param->px_orig_thread_p;
  thread_ref.conn_entry = sort_param->px_orig_thread_p->conn_entry;
  thread_p->push_resource_tracks ();

  assert (inputs != NULL && n_inputs > 0);

  buffers = (char *) malloc ((size_t) n_inputs * DB_PAGESIZE);
  heap = (int *) malloc ((size_t) n_inputs * sizeof (int));
  cur_page = (int *) malloc ((size_t) n_inputs * sizeof (int));
  cur_slot = (int *) malloc ((size_t) n_inputs * sizeof (int));
  nrecs = (int *) malloc ((size_t) n_inputs * sizeof (int));
  cur_rec = (RECDES *) calloc ((size_t) n_inputs, sizeof (RECDES));
  long_rec = (RECDES *) calloc ((size_t) n_inputs, sizeof (RECDES));
  if (buffers == NULL || heap == NULL || cur_page == NULL || cur_slot == NULL || nrecs == NULL || cur_rec == NULL
      || long_rec == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) n_inputs * (DB_PAGESIZE + 4 * sizeof (int) + 2 * sizeof (RECDES)));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* open one cursor per non-empty slice and seed the min-heap */
  for (c = 0; c < n_inputs && error == NO_ERROR; c++)
    {
      char *pgbuf = buffers + (size_t) c * DB_PAGESIZE;

      cur_page[c] = inputs[c].range.start_page;
      cur_slot[c] = inputs[c].range.start_slot;
      if (btsort_px_merge_cursor_done (&inputs[c], cur_page[c], cur_slot[c]))
	{
	  continue;
	}
      error = btsort_read_area (thread_p, &inputs[c].temp, &inputs[c].cursor, cur_page[c], 1, pgbuf);
      if (error != NO_ERROR)
	{
	  break;
	}
      nrecs[c] = btsort_spage_get_numrecs (pgbuf);
      if (cur_slot[c] >= nrecs[c])
	{
	  assert (false);
	  error = ER_SORT_TEMP_PAGE_CORRUPTED;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	}
      error = btsort_px_merge_cursor_fetch (thread_p, pgbuf, cur_slot[c], &cur_rec[c], &long_rec[c]);
      if (error != NO_ERROR)
	{
	  break;
	}
      heap[heap_n++] = c;
    }
  for (c = heap_n / 2 - 1; c >= 0 && error == NO_ERROR; c--)
    {
      btsort_px_merge_heap_sift_down (heap, heap_n, c, compare, compare_arg, cur_rec, long_rec);
    }

  /* merge-put: pop the global minimum, feed it to the shard loader, advance that cursor, restore the heap */
  while (heap_n > 0 && error == NO_ERROR)
    {
      char *pgbuf;

      c = heap[0];
      pgbuf = buffers + (size_t) c *DB_PAGESIZE;
      if (cur_rec[c].type == REC_BIGONE)
	{
	  error = bt_load_worker_put_range (thread_p, load_args, &long_rec[c]);
	}
      else
	{
	  ((BTSORT_REC *) cur_rec[c].data)->next = NULL;
	  error = bt_load_worker_put_range (thread_p, load_args, &cur_rec[c]);
	}
      if (error != NO_ERROR)
	{
	  break;
	}

      cur_slot[c]++;
      if (cur_slot[c] >= nrecs[c])
	{
	  cur_page[c]++;
	  cur_slot[c] = 0;
	}
      if (btsort_px_merge_cursor_done (&inputs[c], cur_page[c], cur_slot[c]))
	{
	  heap[0] = heap[--heap_n];
	}
      else
	{
	  if (cur_slot[c] == 0)
	    {
	      error = btsort_read_area (thread_p, &inputs[c].temp, &inputs[c].cursor, cur_page[c], 1, pgbuf);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      nrecs[c] = btsort_spage_get_numrecs (pgbuf);
	      if (nrecs[c] <= 0)
		{
		  assert (false);
		  error = ER_SORT_TEMP_PAGE_CORRUPTED;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  break;
		}
	    }
	  error = btsort_px_merge_cursor_fetch (thread_p, pgbuf, cur_slot[c], &cur_rec[c], &long_rec[c]);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      if (heap_n > 0)
	{
	  btsort_px_merge_heap_sift_down (heap, heap_n, 0, compare, compare_arg, cur_rec, long_rec);
	}
    }

  if (long_rec != NULL)
    {
      for (c = 0; c < n_inputs; c++)
	{
	  if (long_rec[c].data != NULL)
	    {
	      free_and_init (long_rec[c].data);
	    }
	}
      free_and_init (long_rec);
    }
  if (cur_rec != NULL)
    {
      free_and_init (cur_rec);
    }
  if (nrecs != NULL)
    {
      free_and_init (nrecs);
    }
  if (cur_slot != NULL)
    {
      free_and_init (cur_slot);
    }
  if (cur_page != NULL)
    {
      free_and_init (cur_page);
    }
  if (heap != NULL)
    {
      free_and_init (heap);
    }
  if (buffers != NULL)
    {
      free_and_init (buffers);
    }

  if (error == NO_ERROR)
    {
      error = bt_load_worker_close_shard (thread_p, load_args);
    }
  error = bt_load_worker_epilogue (thread_p, load_args, error);
  thread_p->pop_resource_tracks ();

  pthread_mutex_lock (sort_param->px_mtx);
  sort_param->px_status = error == NO_ERROR ? BTSORT_PX_DONE : BTSORT_PX_ERR_FAILED;
  if (error != NO_ERROR)
    {
      /* first-error-wins: only the first failing shard publishes its error context (e.g. the dedicated
       * vacuum-notification-limit error of a no-logging build) into the main thread's context. */
      if (sort_param->ori_sort_param == NULL || !sort_param->ori_sort_param->px_error_published)
	{
	  if (sort_param->ori_sort_param != NULL)
	    {
	      sort_param->ori_sort_param->px_error_published = true;
	    }
	  sort_param->main_error_context->get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	}
    }
  thread_ref.m_px_orig_thread_entry = NULL;
  pthread_cond_signal (sort_param->complete_cond);
  pthread_mutex_unlock (sort_param->px_mtx);
}

static BT_LOAD_PX_OUTCOME
btsort_px_construct_index_leaf (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param,
				int parallel_num)
{
  SORT_ARGS *sort_args = (SORT_ARGS *) sort_param->get_arg;
  LOAD_ARGS *main_load_args = (LOAD_ARGS *) sort_param->put_arg;
  LOAD_ARGS *shard_load_args[BTSORT_MAX_PARALLEL] = { NULL };
  BTSORT_PX_MERGE_INPUT *shard_inputs[BTSORT_MAX_PARALLEL] = { NULL };
  BT_LOAD_PROVIDER *provider = NULL;
  int n_shards = 0;
  int n_runs = 0;
  int error = NO_ERROR;
  bool file_sysop_open = false;
  INT64 total_pages_64 = 0;
  int total_pages, est_main_pages, est_ovf_pages = 0;
  INT64 ovf_upper = 0;

  if (parallel_num < 2 || !bt_load_parallel_enabled (main_load_args))
    {
      return BT_PX_NOT_ATTEMPTED;
    }

  /*
   * Key-partition every worker run into parallel_num shards.  Each shard worker k-way merges its slices
   * while putting, replacing the fan-in merge to a single temp run.  The put order is unchanged: the sort
   * comparator is a strict total order over (key, OID), so the per-shard merged stream is exactly the
   * subsequence a single merged run would have yielded for that key range, and no duplicate-key group is
   * split across shards (splitters are strictly increasing distinct keys chosen from global weighted
   * quantiles; records partition by key < / >= splitter, a property of the shared lower_bound cuts).
   * Splitter-selection errors fail the parallel build (BT_PX_ERROR) -- there is no serial fallback.
   */
  error = btsort_px_slice_runs_index_leaf (thread_p, px_sort_param, sort_param, parallel_num, shard_inputs,
					   &n_runs, &n_shards, &total_pages_64);
  if (error != NO_ERROR)
    {
      bt_load_set_px_outcome (main_load_args, BT_PX_ERROR);
      return BT_PX_ERROR;
    }
  if (n_shards < 2)
    {
      /*
       * no-redo builds are restricted to genuinely parallel construction.
       * btsort_px_slice_runs_index_leaf allocates shard_inputs only when it commits to n_shards >= 2, so
       * nothing needs freeing here.  Demote to a fully logged build now -- strictly before any file is
       * created -- so the legacy single-run path in btsort_merge_run_for_parallel_index_leaf_build() creates
       * the file itself, wrapped in the same sysop as the put: no window where a file is created and
       * attached to the outer transaction ahead of knowing whether the build that follows will actually
       * populate it.
       */
      bt_load_demote_to_logged (main_load_args);
      return BT_PX_NOT_ATTEMPTED;
    }

  /* Committed to the parallel shard path: create the main and overflow-key files, open the page provider
   * and allocate every shard's LOAD_ARGS inside one sysop that stays open until all of them exist (nested
   * span sysops attach to this parent), so a failure in between aborts it and destroys the files too. */
  log_sysop_start (thread_p);
  file_sysop_open = true;
  error = btree_create_file (thread_p, &sort_args->class_ids[0], sort_args->attr_ids[0], sort_args->btid->sys_btid);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      btsort_px_free_shard_inputs (shard_inputs, n_shards);
      bt_load_set_px_outcome (main_load_args, BT_PX_ERROR);
      return BT_PX_ERROR;
    }
  /* if loading is aborted or if transaction is aborted, vacuum must be notified before file is destoyed. */
  vacuum_log_add_dropped_file (thread_p, &sort_args->btid->sys_btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);
  error = btree_create_overflow_key_file (thread_p, sort_args->btid);
  if (error != NO_ERROR)
    {
      log_sysop_abort (thread_p);
      btsort_px_free_shard_inputs (shard_inputs, n_shards);
      bt_load_set_px_outcome (main_load_args, BT_PX_ERROR);
      return BT_PX_ERROR;
    }

  total_pages = (int) MIN ((INT64) INT_MAX, total_pages_64);
  est_main_pages = (int) MIN ((INT64) INT_MAX, (INT64) total_pages + MAX ((INT64) 64, (INT64) total_pages / 10));
  if (sort_args->sum_ovf_pages > 0)
    {
      ovf_upper = sort_args->sum_ovf_pages;
      INT64 cap = MAX ((INT64) DISK_SECTOR_NPAGES, ovf_upper / n_shards);
      est_ovf_pages = (int) MIN (ovf_upper, MIN (cap, (INT64) INT_MAX));
    }
  error = bt_load_provider_open (thread_p, &provider, sort_args->btid->sys_btid, n_shards, est_main_pages,
				 est_ovf_pages, true);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  for (int i = 0; i < n_shards; i++)
    {
      error = bt_load_alloc_shard_load_args (thread_p, main_load_args, provider, i, &shard_load_args[i]);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      px_sort_param[i].put_arg = shard_load_args[i];
      px_sort_param[i].px_merge_inputs = shard_inputs[i];
      px_sort_param[i].px_merge_n_inputs = n_runs;
      px_sort_param[i].px_status = BTSORT_PX_PROGRESS;
    }

  /* Every resource exists: transfer ownership to the outer transaction, right before the workers start. */
  log_sysop_attach_to_outer (thread_p);
  file_sysop_open = false;
  assert (log_get_system_op_level (thread_p) < 0);

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: px construct btid(%d, (%d, %d)), workers==shards=%d runs=%d",
		     sort_args->btid->sys_btid->root_pageid, sort_args->btid->sys_btid->vfid.volid,
		     sort_args->btid->sys_btid->vfid.fileid, n_shards, n_runs);
    }

  BTSORT_EXECUTE_PARALLEL (n_shards, px_sort_param, btsort_put_result_index_leaf);
  error = bt_load_provider_service_loop (thread_p, provider);
  assert (log_get_system_op_level (thread_p) < 0);
  BTSORT_WAIT_PARALLEL (n_shards, sort_param, px_sort_param);
  if (error == NO_ERROR)
    {
      error = bt_load_px_join_finalize (thread_p, main_load_args, shard_load_args, n_shards);
    }
  assert (log_get_system_op_level (thread_p) < 0);

cleanup:
  for (int i = 0; i < n_shards; i++)
    {
      bt_load_free_shard_load_args (thread_p, shard_load_args[i]);
      px_sort_param[i].px_merge_inputs = NULL;
      px_sort_param[i].px_merge_n_inputs = 0;
    }
  btsort_px_free_shard_inputs (shard_inputs, n_shards);
  bt_load_provider_close (provider);
  if (error != NO_ERROR)
    {
      if (file_sysop_open)
	{
	  /* Failed before ownership transferred: abort the sysop, destroying the files and every span
	   * already attached to it. */
	  log_sysop_abort (thread_p);
	}
      bt_load_set_px_outcome (main_load_args, BT_PX_ERROR);
      return BT_PX_ERROR;
    }
  bt_load_set_px_outcome (main_load_args, BT_PX_TREE_DONE);
  return BT_PX_TREE_DONE;
}

static int
btsort_merge_run_for_parallel_index_leaf_build (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param,
						BTSORT_PARAM * sort_param, int parallel_num)
{
  int error = NO_ERROR;
  int i;
  BTSORT_MERGE_QUEUE_CTX qctx;
  SORT_ARGS *sort_args_p;
  LOAD_ARGS *load_args_p;

  if (parallel_num > BTSORT_MAX_PARALLEL)
    {
      return ER_FAILED;
    }

  btsort_merge_queue_ctx_init (&qctx, sort_param, px_sort_param, parallel_num);
  btsort_merge_queue_enqueue_initial_runs (&qctx, px_sort_param, parallel_num);

  sort_args_p = (SORT_ARGS *) sort_param->get_arg;
  load_args_p = (LOAD_ARGS *) sort_param->put_arg;

  for (i = 0; i < parallel_num; i++)
    {
      SORT_ARGS *px_sort_args_p = (SORT_ARGS *) px_sort_param[i].get_arg;
      sort_args_p->n_oids += px_sort_args_p->n_oids;
      sort_args_p->n_nulls += px_sort_args_p->n_nulls;
      sort_args_p->n_ovf_keys += px_sort_args_p->n_ovf_keys;
      sort_args_p->sum_ovf_pages += px_sort_args_p->sum_ovf_pages;
    }

  /*
   * no-redo builds are restricted to genuinely parallel construction.  btsort_px_construct_index_leaf
   * owns both the shard-count decision and the main/overflow-key file creation: it creates and attaches both
   * files, in one sysop, only once it has committed to n_shards >= 2, and demotes load_args_p->no_redo to
   * false (with no file created yet) whenever it falls back to BT_PX_NOT_ATTEMPTED.  This removes the
   * previous eager file-creation window (main file created and attached to the outer transaction ahead of
   * knowing whether the shard build would actually engage): on the n_shards < 2 fallthrough, no file exists
   * yet when we reach the legacy path below, so there is nothing left orphaned if the eventual put fails --
   * the legacy path creates the file itself, wrapped in the very sysop the put's failure would abort.
   */
  {
    /*
     * Build the tree straight from the workers' sorted runs.  Each shard worker k-way merges its own key
     * range of every run while putting, so the fan-in merge tree -- whose tail is one thread rewriting the whole
     * data set once per level -- is skipped entirely.  BT_PX_NOT_ATTEMPTED falls through to the legacy path
     * below: merge everything into a single run and put it serially.
     */
    BT_LOAD_PX_OUTCOME outcome = btsort_px_construct_index_leaf (thread_p, px_sort_param, sort_param, parallel_num);
    if (outcome != BT_PX_NOT_ATTEMPTED)
      {
	/* the initial runs were consumed in place; nothing staged them into sort_param->temp, so retire them
	 * here (the BTSORT_PX_THREAD_IN_PARALLEL resource cleanup never retires temp files). */
	for (i = 0; i < parallel_num; i++)
	  {
	    int idx = px_sort_param[i].px_result_file_idx;
	    if (px_sort_param[i].file_contents[idx].num_pages[0] > 0 && px_sort_param[i].temp[idx].volid != NULL_VOLID)
	      {
		int retire_error = file_temp_retire (thread_p, &px_sort_param[i].temp[idx]);
		if (retire_error == NO_ERROR)
		  {
		    VFID_SET_NULL (&px_sort_param[i].temp[idx]);
		    file_find_nth_cursor_reset (&px_sort_param[i].temp_cursor[idx]);
		  }
		else
		  {
		    ASSERT_ERROR ();
		    error = retire_error;
		  }
	      }
	  }
	btsort_merge_queue_ctx_destroy (&qctx);
	return outcome == BT_PX_TREE_DONE ? error : ER_FAILED;
      }
  }

  /* legacy single-run path: merge all runs into one, then put it serially from the temp file. */
  if (qctx.queue_size >= 2)
    {
      error = btsort_merge_queue_run (thread_p, &qctx);
      if (error != NO_ERROR)
	{
	  btsort_merge_queue_ctx_destroy (&qctx);
	  return ER_FAILED;
	}
      btsort_merge_queue_stage_final_run (&qctx, &px_sort_param[0]);
    }
  else if (qctx.queue_size == 1)
    {
      btsort_merge_queue_stage_final_run (&qctx, &px_sort_param[0]);
    }

  {
    int result_file_idx = px_sort_param[0].px_result_file_idx;
    sort_param->px_result_file_idx = result_file_idx;
    sort_param->file_contents[result_file_idx].num_pages[0] =
      px_sort_param[0].file_contents[result_file_idx].num_pages[0];
    sort_param->temp[result_file_idx] = px_sort_param[0].temp[result_file_idx];
    file_find_nth_cursor_reset (&sort_param->temp_cursor[result_file_idx]);
  }

  /* no-redo builds are restricted to genuinely parallel construction, so by construction we never
   * reach here with load_args_p->no_redo still true -- btsort_px_construct_index_leaf above either committed to
   * the parallel shard path (returning before this point) or demoted load_args_p->no_redo to false before
   * returning BT_PX_NOT_ATTEMPTED. The file was therefore never created above; create it now and leave the
   * wrapping sysop open through the put below, exactly like the single-process (serial) fallback in
   * btree_sort() does -- xbtree_load_index attaches it to the outer transaction once the tree is fully
   * built. */
  assert (!bt_load_parallel_enabled (load_args_p));
  log_sysop_start (thread_p);
  if (btree_create_file
      (thread_p, &sort_args_p->class_ids[0], sort_args_p->attr_ids[0], sort_args_p->btid->sys_btid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      btsort_merge_queue_ctx_destroy (&qctx);
      return ER_FAILED;
    }

  /* if loading is aborted or if transaction is aborted, vacuum must be notified before file is destoyed. */
  vacuum_log_add_dropped_file (thread_p, &sort_args_p->btid->sys_btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  if (btsort_put_result_from_tmpfile (thread_p, sort_param, 0) != NO_ERROR)
    {
      /* A sysop is open here unconditionally (the create-file above), so the abort is unconditional. */
      log_sysop_abort (thread_p);
      error = ER_FAILED;
    }

  btsort_merge_queue_ctx_destroy (&qctx);
  return error;
}

/*
 * btsort_merge_nruns () - merge n run
 *   return: NO_ERROR
 *   px_sort_param(in):
 *   sort_param(in):
 *   parallel_num(in):
 */
static int
btsort_merge_nruns (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param)
{
  int error = NO_ERROR;
  int i = 0, idx = 0, file_pg_cnt_est;

  /* Create output temporary files make file and temporary volume page count estimates */
  file_pg_cnt_est = btsort_get_avg_numpages_of_nonempty_tmpfile (sort_param);
  file_pg_cnt_est = MAX (1, file_pg_cnt_est);

  for (i = sort_param->half_files; i < sort_param->tot_tempfiles; i++)
    {
      error =
	btsort_add_new_file (thread_p, &(sort_param->temp[i]), &sort_param->temp_cursor[i], file_pg_cnt_est, true,
			     sort_param->tde_encrypted);
      if (error != NO_ERROR)
	{
	  goto retire_all_on_error;
	}
    }

  /* Merge the parallel processed results. */
  error = btsort_exphase_merge (thread_p, sort_param);

  if (error != NO_ERROR)
    {
      goto retire_all_on_error;
    }

  /* save result run */
  sort_param->px_result_run->temp_file = sort_param->temp[sort_param->px_result_file_idx];
  sort_param->px_result_run->num_pages = sort_param->file_contents[sort_param->px_result_file_idx].num_pages[0];

  /* retire temp file */
  for (i = 0; i < sort_param->tot_tempfiles; i++)
    {
      if (sort_param->temp[i].volid != NULL_VOLID && i != sort_param->px_result_file_idx)
	{
	  (void) file_temp_retire (thread_p, &sort_param->temp[i]);
	  VFID_SET_NULL (&sort_param->temp[i]);
	  file_find_nth_cursor_reset (&sort_param->temp_cursor[i]);
	}
    }
  return NO_ERROR;

retire_all_on_error:
  /* On error: retire every allocated temp file — both the dequeued inputs
   * (temp[0..half_files-1], already removed from the queue and no longer
   * tracked there) and any output files allocated before the failure. The
   * caller (btsort_merge_nruns_queue_cb) leaves px_result_run at its
   * setup_ctx-initialized NULL_VOLID, so no separate result-file retire is
   * needed here. */
  for (i = 0; i < sort_param->tot_tempfiles; i++)
    {
      if (sort_param->temp[i].volid != NULL_VOLID)
	{
	  (void) file_temp_retire (thread_p, &sort_param->temp[i]);
	  VFID_SET_NULL (&sort_param->temp[i]);
	  file_find_nth_cursor_reset (&sort_param->temp_cursor[i]);
	}
    }
  return error;
}

/*
 * btsort_compute_parallel_degree () - Compute the parallel degree of the index build sort
 *   return: degree (< 2 means serial)
 *   no_logging_build(in): true for the no-logging index build (loaddb --no-logging-index)
 *   n_data_pages(in): data pages of the heap(s) to scan
 *   n_sects(in): data sectors of the heap(s) to scan; a worker needs one of its own to make a run
 *
 * Note: The no-logging build owns the table, so it takes every core, capped by PRM_MAX_PARALLELISM
 *       and n_sects. CREATE INDEX goes parallel from parallel_index_build_page_threshold pages on:
 *       the degree is floor (log2 (pages / threshold)) + 2, capped by index_build_parallelism and
 *       by the core count.
 */
static int
btsort_compute_parallel_degree (bool no_logging_build, int n_data_pages, int n_sects)
{
  const int start_degree = 2;
  int core_count = (int) cubthread::system_core_count ();
  int page_threshold, cap, degree;
  UINT64 x;

  if (no_logging_build)
    {
      degree = MIN (core_count, PRM_MAX_PARALLELISM);
      degree = MIN (degree, n_sects);
      return (degree < start_degree) ? 0 : degree;
    }

  if (core_count <= start_degree)
    {
      return 0;
    }

  page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_INDEX_BUILD_PAGE_THRESHOLD);
  page_threshold = MAX (page_threshold, start_degree);
  if (n_data_pages < page_threshold)
    {
      return 0;
    }

  cap = prm_get_integer_value (PRM_ID_INDEX_BUILD_PARALLELISM);
  if (cap < 0)
    {
      cap = prm_get_integer_value (PRM_ID_PARALLELISM);
    }
  cap = MIN (cap, core_count);

  /* floor (log2 (pages / threshold)) + start_degree */
  degree = start_degree;
  for (x = (UINT64) n_data_pages / (UINT64) page_threshold; x > 1; x >>= 1)
    {
      degree++;
    }

  degree = MIN (degree, cap);
  return (degree < start_degree) ? 0 : degree;
}

/*
 * btsort_check_parallelism () - Decide the parallel degree of the index build sort
 *   return: parallel_num (1 = single process)
 *   sort_param(in):
 *
 * Note: See btsort_compute_parallel_degree for the degree policy. The degree is then limited by
 *       the number of data sectors and by the parallel workers actually reserved.
 */
static int
btsort_check_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param)
{
  int parallel_num = 1;
  SORT_ARGS *sort_args_p = (SORT_ARGS *) sort_param->get_arg;
  int n_data_pages = 0, n_sects = 0, error_code = NO_ERROR;
  bool no_logging_build = bt_load_parallel_enabled ((const LOAD_ARGS *) sort_param->put_arg);

  if (sort_args_p->n_classes != 1)
    {
      /* not partition, partition has own indexes, this means like this :
       * create t1; create t2 under t1;  Nothing to look at when there is no class either. */
      return 1;
    }
  /* get number of pages to sort and number of data sectors to scan */
  error_code = heap_get_num_data_pages (thread_p, &sort_args_p->hfids[0], &n_data_pages);
  if (error_code != NO_ERROR)
    {
      return 1;
    }
  error_code = file_get_num_data_sectors (thread_p, &sort_args_p->hfids[0].vfid, &n_sects);
  if (error_code != NO_ERROR)
    {
      return 1;
    }

  parallel_num = btsort_compute_parallel_degree (no_logging_build, n_data_pages, n_sects);
  if (parallel_num < 2)
    {
      /* single process */
      return 1;
    }

  if (n_sects < parallel_num)
    {
      /* no sector in some threads */
      return 1;
    }

  /* check worker */
  sort_param->px_worker_manager = parallel_query::worker_manager::try_reserve_workers (parallel_num);
  if (sort_param->px_worker_manager == NULL)
    {
      return 1;
    }
  else
    {
      /* clamp to the number of workers actually reserved (partial reservation under contention). */
      return sort_param->px_worker_manager->get_reserved_workers ();
    }
}

/*
 * btsort_start_parallelism () - Prepare one BTSORT_PARAM per worker and split the heap data sectors among them
 *   return: NO_ERROR or error code
 *   px_sort_param(out): array of px_parallel_num worker parameters
 *   sort_param(in): main sort parameters
 */
static int
btsort_start_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param)
{
  int error = NO_ERROR;
  int parallel_num = sort_param->px_parallel_num;
  SORT_ARGS *sort_args_p = (SORT_ARGS *) sort_param->get_arg, *px_sort_args_p;
  std::vector < ftab_set > ftab_sets (parallel_num);
  ftab_set temp;
  FILE_FTAB_COLLECTOR collector;

  /* copy sort_param for parallel sort */
  error = btsort_copy_sort_param (thread_p, px_sort_param, sort_param, parallel_num);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  for (int i = 0; i < parallel_num; i++)
    {
      px_sort_param[i].get_arg = NULL;
    }

  for (int i = 0; i < parallel_num; i++)
    {
      px_sort_args_p = (SORT_ARGS *) malloc (sizeof (SORT_ARGS));
      if (px_sort_args_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SORT_ARGS));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memcpy (px_sort_args_p, sort_param->get_arg, sizeof (SORT_ARGS));
      px_sort_param[i].get_arg = px_sort_args_p;
      px_sort_param[i].get_fn = &btree_sort_get_next_parallel;
      px_sort_args_p->ftab_sets = NULL;
      px_sort_args_p->curr_sec = FILE_PARTIAL_SECTOR_INITIALIZER;
      px_sort_args_p->curr_pgoffset = 0;
      px_sort_args_p->n_ovf_keys = 0;
      px_sort_args_p->sum_ovf_pages = 0;
      px_sort_args_p->in_recdes =
      {
      0, 0, 0, NULL};
    }

  /* split ftab into each parallel sort param */
  for (int i = 0; i < sort_args_p->n_classes; i++)
    {
      error = file_get_all_data_sectors (thread_p, &sort_args_p->hfids[i].vfid, &collector);
      if (error != NO_ERROR)
	{
	  return error;
	}
      temp.convert (&collector);
      db_private_free_and_init (thread_p, collector.partsect_ftab);
      ftab_sets = temp.split (parallel_num);
      for (int j = 0; j < parallel_num; j++)
	{
	  px_sort_args_p = (SORT_ARGS *) px_sort_param[j].get_arg;
	  if (px_sort_args_p->ftab_sets == NULL)
	    {
	      px_sort_args_p->ftab_sets = (std::vector < ftab_set > *)malloc (sizeof (std::vector < ftab_set >));
	      if (px_sort_args_p->ftab_sets == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  sizeof (std::vector < ftab_set >));
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      placement_new (px_sort_args_p->ftab_sets);
	    }
	  /* Always push (even if empty) to keep ftab_sets index aligned with cur_class.
	   * An empty ftab_set causes get_next_vpid() to return S_END immediately. */
	  px_sort_args_p->ftab_sets->push_back (ftab_sets[j]);
	}
    }

  return error;
}

/*
 * btsort_end_parallelism () - Fan-in after all workers finished: merge the worker runs and build the leaf level
 *   return: NO_ERROR or error code
 *   px_sort_param(in): worker parameters
 *   sort_param(in): main sort parameters
 */
static int
btsort_end_parallelism (THREAD_ENTRY * thread_p, BTSORT_PARAM * px_sort_param, BTSORT_PARAM * sort_param)
{
  int error = NO_ERROR;
  int parallel_num = sort_param->px_parallel_num;

  error = btsort_merge_run_for_parallel_index_leaf_build (thread_p, px_sort_param, sort_param, parallel_num);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  return error;
}
#endif /* SERVER_MODE */

/*
 * btsort_write_area () - Write memory area to disk
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
btsort_write_area (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor, int first_page,
		   INT32 num_pages, char *area_start, bool tde_encrypted)
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
      ret = file_numerable_find_nth_cursor (thread_p, vfid, page_no++, true, NULL, NULL, cursor, &vpid);
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
 * btsort_read_area () - Read memory area from disk
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
btsort_read_area (THREAD_ENTRY * thread_p, VFID * vfid, FILE_FIND_NTH_CURSOR * cursor, int first_page,
		  INT32 num_pages, char *area_start)
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
      ret = file_numerable_find_nth_cursor (thread_p, vfid, page_no++, false, NULL, NULL, cursor, &vpid);
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
 * btsort_get_num_half_tmpfiles () - Determines the number of temporary files to be used
 *                        during the sorting process
 *   return: half of the number of temporary files (the number of input files of a merge pass)
 *   tot_buffers(in): total number of buffers in the buffer pool area
 *
 * Note: Half of the buffers are given to input files, within
 *       [BTSORT_MIN_HALF_FILES, BTSORT_MAX_HALF_FILES] (too few files cannot merge the runs, too
 *       many saturate the buffer).
 */
static int
btsort_get_num_half_tmpfiles (int tot_buffers)
{
  int half_files = tot_buffers / 2;

  if (half_files < BTSORT_MIN_HALF_FILES)
    {
      return BTSORT_MIN_HALF_FILES;
    }

  if (half_files < BTSORT_MAX_HALF_FILES)
    {
      return half_files;
    }

  return BTSORT_MAX_HALF_FILES;
}

/*
 * btsort_checkalloc_numpages_of_outfiles () - Check sizes of output files
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
btsort_checkalloc_numpages_of_outfiles (THREAD_ENTRY * thread_p, BTSORT_PARAM * sort_param)
{
  int out_file;
  int out_half;
  int needed_pages[2 * BTSORT_MAX_HALF_FILES];
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
	      error_code = file_temp_retire (thread_p, &sort_param->temp[i]);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	      VFID_SET_NULL (&sort_param->temp[i]);
	      file_find_nth_cursor_reset (&sort_param->temp_cursor[i]);
	    }
	}
    }
  return NO_ERROR;
}

/*
 * btsort_get_numpages_of_active_infiles () - Find number of active input files
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
btsort_get_numpages_of_active_infiles (const BTSORT_PARAM * sort_param)
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
 * btsort_find_inbuf_size () - Distribute buffers
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
btsort_find_inbuf_size (int tot_buffers, int in_sections)
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
 * btsort_run_add_new () - Adds a new node to the end of the given list
 *   return: NO_ERROR
 *   file_contents(in): which list to add
 *   num_pages(in): what value to put for the new run
 */
static int
btsort_run_add_new (BTSORT_FILE_CONTENTS * file_contents, int num_pages)
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
      new_total_elements = ((int) (((float) file_contents->num_slots * BTSORT_EXPAND_DYN_ARRAY_RATIO) + 0.5));
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
 * btsort_run_remove_first () - Removes the first run of the given file contents list
 *   return: void
 *   file_contents(in): which list to remove from
 */
static void
btsort_run_remove_first (BTSORT_FILE_CONTENTS * file_contents)
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
 * btsort_get_num_file_contents () - Returns the number of elements kept in the
 *                           given linked list
 *   return:
 *   file_contents(in): which list
 */
static int
btsort_get_num_file_contents (BTSORT_FILE_CONTENTS * file_contents)
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
