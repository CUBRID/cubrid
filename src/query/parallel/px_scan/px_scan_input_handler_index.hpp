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

/*
 * px_scan_input_handler_index.hpp
 */

#ifndef _PX_SCAN_INPUT_HANDLER_INDEX_HPP_
#define _PX_SCAN_INPUT_HANDLER_INDEX_HPP_

#include "access_spec.hpp"
#include "btree.h"
#include "dbtype.h"
#include "px_interrupt.hpp"
#include "px_scan_index_key_range_list.hpp"
#include "px_scan_index_leaf_page_dispatcher.hpp"
#include "px_scan_index_overflow_chain_pool.hpp"
#include "file_manager.h"
#include "list_file.h"
#include "query_list.h"
#include "query_manager.h"
#include "scan_manager.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace parallel_scan
{
  /* bitmap heap scan: see bitmap_marks */
  constexpr int BITMAP_WORDS = 10;       /* slot bits of a page entry: 640 */
  constexpr int BITMAP_BATCH = 4096;     /* records gathered before they are written */
  constexpr std::uint32_t BITMAP_MAX_PARTITIONS = 256;

  struct bitmap_record
  {
    std::uint32_t page;     /* place of the sector in the sector list * 64 + page of the sector */
    std::uint8_t word;      /* which of the entry's words */
    std::uint8_t unused[3];
    std::uint64_t bits;
  };
  struct bitmap_entry
  {
    std::uint32_t page;
    std::uint32_t unused;
    std::uint64_t w[BITMAP_WORDS];
  };
  struct bitmap_table
  {
    bitmap_entry *e;
    std::uint16_t *b;       /* 0: empty, else the entry's place + 1 */
    int n;
    int cap;
    int nb;                 /* buckets, a power of two */
    int shift;
    bitmap_table () : e (nullptr), b (nullptr), n (0), cap (0), nb (0), shift (0) {}
    ~bitmap_table ();
    int init (int entries, int buckets);
    void clear ();
    /* the page's entry, created when absent; nullptr when the table is full */
    inline bitmap_entry *get (std::uint32_t page)
    {
      std::uint32_t i = (std::uint32_t) ((page * 0x9E3779B97F4A7C15ull) >> shift);

      for (;; i = (i + 1) & (nb - 1))
	{
	  const std::uint16_t x = b[i];

	  if (x == 0)
	    {
	      if (n >= cap)
		{
		  return nullptr;
		}
	      bitmap_entry *p = &e[n];

	      p->page = page;
	      for (int k = 0; k < BITMAP_WORDS; k++)
		{
		  p->w[k] = 0;
		}
	      b[i] = (std::uint16_t) (++n);
	      return p;
	    }
	  if (e[x - 1].page == page)
	    {
	      return &e[x - 1];
	    }
	}
    }
  };
  /* records of one chain in a temp file: pages linked from first to tail */
  struct bitmap_chain
  {
    VPID first;
    VPID tail;
    int tail_n;             /* records on the tail page */
  };
  /* a temp file of record chains and the batch not yet written to it */
  struct bitmap_writer
  {
    std::vector<bitmap_chain> chain;
    std::vector<bitmap_record> batch;
    std::vector<std::uint32_t> where;	/* the chain of each record of the batch */
    std::vector<std::uint32_t> start;       /* by chain: where its records of the batch go */
    std::vector<std::uint16_t> order;       /* the batch grouped by chain */
    QMGR_TEMP_FILE *tfile;
    bitmap_writer () : tfile (nullptr) {}
  };
  struct bitmap_worker
  {
    bitmap_table tbl;       /* index step */
    bool switched;          /* tbl was full once: it holds the first partition's first part only */
    bitmap_writer rec;      /* index step: a chain a partition */
    std::vector<std::uint32_t> tbl_start;	/* by partition: where its entries of tbl begin in tbl_order */
    std::vector<std::uint16_t> tbl_order;	/* tbl\'s entries grouped by partition, once the index step is over */
    QFILE_LIST_ID *over;    /* OIDs whose slot is beyond the width */
    bool scans_over;		/* heap step: this worker reads the merged list */
    bitmap_table heap_tbl;  /* heap step: the partition, or the part of it, being read */
    bitmap_writer sub;      /* heap step: the parts of a partition wider than a table */
    std::vector<std::uint32_t> order;       /* heap step: heap_tbl's entries by page */
    bitmap_worker () : switched (false), over (nullptr), scans_over (false) {}
  };


  /* The marks of a bitmap heap scan: the index step records the rows it finds by heap page, the heap step reads the
   * recorded pages in file order, each once. One object a scan; a parallel index scan's workers share it, a serial
   * index scan is its one worker.
   *
   * A worker of the index step keeps a hash table of page entries of its own: an entry holds the slot bits of one
   * page. The heap is divided into partitions, runs of whole sectors small enough for one table. Until its table is
   * full a worker keeps every page in it; from then on the table holds the first partition's first part only and
   * every other mark leaves as a 16-byte record, written batch by batch to that partition's chain of pages in the
   * worker's temp file. A slot beyond an entry's width goes to the worker's list file instead.
   *
   * A worker of the heap step takes a partition, ORs every worker's entries and records of it into its own table and
   * reads the pages in order. A heap too large for 256 partitions gets partitions wider than a table: the worker
   * divides such a partition once more into its own temp file.
   *
   * Written by the thread that calls init () before the workers start, except a worker's own bitmap_worker (read by
   * every worker once all index steps are over), the counters, and the merged list (the worker that took it). */
  class bitmap_marks
  {
    public:
      ~bitmap_marks ();
      /* a scan that reads no heap row per OID, or that returns its rows in key order, has nothing to read in file
       * order, whatever the plan chose */
      static bool scan_qualifies (const INDX_SCAN_ID *isidp);
      /* the heap's sectors as they are now, the partitions, a table a worker. The statement's snapshot must be taken:
       * a page the heap gets afterwards holds no row the snapshot sees. */
      int init (THREAD_ENTRY *thread_p, const VFID *heap_vfid, int nworkers, QUERY_ID query_id);
      bool on () const
      {
	return m_bm_on;
      }
      /* a worker's own tables, temp files and list for both steps */
      bitmap_worker *claim_worker ()
      {
	return &m_bm_workers[m_bm_claim.fetch_add (1, std::memory_order_relaxed) % m_bm_nworkers];
      }
      /* index step: record one OID. sector_hint: the caller's last sector, kept by the caller. */
      int mark (THREAD_ENTRY *thread_p, bitmap_worker *w, const OID *oid, int *sector_hint);
      /* ... the worker's index step is over: its last batch is written */
      int end_marks (THREAD_ENTRY *thread_p, bitmap_worker *w);
      /* heap step: the next partition nobody took; false: none left */
      bool take_partition (std::uint32_t *part)
      {
	*part = m_bm_next_part.fetch_add (1, std::memory_order_relaxed);
	return *part < m_bm_nparts;
      }
      std::uint32_t parts_of_partition () const
      {
	return m_bm_nsub;
      }
      /* ... part `sub` of the partition into w->heap_tbl, its entries by page in w->order. Part 0 comes first and
       * reads every worker's entries and records of the partition; the other parts go to w->sub on the way and are
       * read from there. */
      int load (THREAD_ENTRY *thread_p, bitmap_worker *w, std::uint32_t part, std::uint32_t sub);
      void page_vpid (std::uint32_t page, VPID *vpid) const
      {
	const FILE_PARTIAL_SECTOR &sector = m_bm_sectors[page >> 6];

	vpid->volid = sector.vsid.volid;
	vpid->pageid = SECTOR_FIRST_PAGEID (sector.vsid.sectid) + (page & 63);
      }
      /* heap step: the workers' lists as one, sorted and without duplicates, for the one worker that gets true */
      bool take_over (THREAD_ENTRY *thread_p, bitmap_worker *taker, int *error_code);
      /* ... its next OID: S_SUCCESS, S_END or S_ERROR */
      SCAN_CODE next_over (THREAD_ENTRY *thread_p, OID *oid);
      /* a worker ends, on any path: the pages its list or its scan of the merged list still holds are let go */
      void close_worker (THREAD_ENTRY *thread_p, bitmap_worker *w);

    private:
      /* the part (of all partitions' parts, counted through) a page belongs to */
      inline std::uint32_t unit_of (std::uint32_t page) const
      {
	return (std::uint32_t) (((std::uint64_t) (page >> 6) * m_bm_nparts * m_bm_nsub) / m_bm_sectors.size ());
      }
      inline int add (THREAD_ENTRY *thread_p, bitmap_writer &wr, std::uint32_t chain, std::uint32_t page, int word,
		      std::uint64_t bits)
      {
	if (!wr.batch.empty () && wr.batch.back ().page == page && wr.batch.back ().word == word)
	  {
	    wr.batch.back ().bits |= bits;
	    return NO_ERROR;
	  }
	wr.batch.push_back (bitmap_record { page, (std::uint8_t) word, { 0, 0, 0 }, bits });
	wr.where.push_back (chain);
	return (int) wr.batch.size () >= BITMAP_BATCH ? write (thread_p, wr) : NO_ERROR;
      }
      /* the batch goes to the temp pages of its chains */
      int write (THREAD_ENTRY *thread_p, bitmap_writer &wr);
      template <typename F>
      int read_chain (THREAD_ENTRY *thread_p, const bitmap_writer &wr, std::uint32_t chain, F &&each);

      bool m_bm_on = false;
      QUERY_ID m_bm_query_id = NULL_QUERY_ID;
      std::vector<FILE_PARTIAL_SECTOR> m_bm_sectors;	/* the heap's sectors when the scan started, by volume and sector */
      std::uint32_t m_bm_nparts = 1;	/* partitions */
      std::uint32_t m_bm_nsub = 1;	/* parts of a partition; 1: a partition fits a table */
      std::uint32_t m_bm_first_unit_sectors = 0;	/* sectors of the first partition's first part */
      bitmap_worker *m_bm_workers = nullptr;
      int m_bm_nworkers = 0;
      std::atomic<int> m_bm_claim { 0 };
      std::atomic<std::uint32_t> m_bm_next_part { 0 };
      std::atomic<bool> m_bm_over_taken { false };
      QFILE_LIST_ID *m_bm_over_list = nullptr;	/* one of the workers' lists, holding them all */
      QFILE_LIST_SCAN_ID m_bm_over_scan;
      QFILE_TUPLE_RECORD m_bm_over_tuple = QFILE_TUPLE_RECORD_INITIALIZER;
      int m_bm_cap = 0;		/* entries of a table */
      int m_bm_buckets = 0;
  };


  class input_handler_index
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_index (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p),
	  m_ranges (),
	  m_leaf (),
	  m_ovf (),
	  m_parallelism (1)
      {
      }
      ~input_handler_index ();
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* get_next_page_with_fix: worker_scan_id MUST be per-task; held_leaf = caller's current leaf (see leaf_page_dispatcher);
       * out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &held_leaf,
					PAGE_PTR &out_page, INT16 *out_slot_hint = nullptr, int *out_range_idx = nullptr)
      {
	return m_leaf.get_next_page_with_fix (thread_p, worker_scan_id, held_leaf, out_page, out_slot_hint, out_range_idx);
      }

      void release_leaf (THREAD_ENTRY *thread_p, PAGE_PTR &held_leaf)
      {
	m_leaf.release_leaf (thread_p, held_leaf);
      }

      void signal_chain_ended (THREAD_ENTRY *thread_p, int last_local_idx, PAGE_PTR &held_leaf)
      {
	m_leaf.signal_chain_ended (thread_p, last_local_idx, held_leaf);
      }

      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      /* getter delegations to m_ranges */
      BTID_INT *get_btid_int ()
      {
	return m_ranges.get_btid_int ();
      }
      int get_parallelism () const
      {
	return m_parallelism;
      }

      INDX_INFO *get_indx_info ()
      {
	return m_ranges.get_indx_info ();
      }
      bool is_desc_index () const
      {
	return m_ranges.is_desc_index ();
      }
      key_val_range *get_key_val_ranges ()
      {
	return m_ranges.get_key_val_ranges ();
      }
      int get_num_key_ranges () const
      {
	return m_ranges.get_num_key_ranges ();
      }

      /* --- Shared overflow API (v3 / key-publish): delegate to m_ovf. --- */
      int try_publish_overflow (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
				const DB_VALUE *key, int range_idx)
      {
	return m_ovf.try_publish (thread_p, first_ovf_vpid, key, range_idx);
      }
      SCAN_CODE claim_next_overflow_page (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page,
					  int &out_range_idx)
      {
	return m_ovf.claim_next (thread_p, slot_idx, out_page, out_range_idx);
      }
      void release_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
      {
	m_ovf.release_page (thread_p, page);
      }
      void exit_overflow_help (THREAD_ENTRY *thread_p, int slot_idx)
      {
	m_ovf.exit_help (thread_p, slot_idx);
      }
      SCAN_CODE wait_or_help_overflow (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
				       DB_VALUE *out_local_key, bool *out_local_clear_key,
				       int &out_range_idx, int &out_slot_idx)
      {
	return m_ovf.wait_or_help (thread_p, out_page, out_local_key, out_local_clear_key, out_range_idx, out_slot_idx);
      }
      void enter_worker ()
      {
	m_ovf.enter_worker ();
      }
      void leave_worker ()
      {
	m_ovf.leave_worker ();
      }
      void signal_no_more_leaves ()
      {
	m_ovf.signal_no_more_leaves ();
      }

      /* --- bitmap heap scan (bitmap_marks): the workers record what the index finds, wait for each other, then read
       * the heap. --- */
      /* before init_on_main: the query the temp files belong to */
      void set_bitmap_query_id (QUERY_ID query_id)
      {
	m_bm_query_id = query_id;
      }
      bool bitmap_on () const
      {
	return m_bm.on ();
      }
      bitmap_marks &bitmap ()
      {
	return m_bm;
      }
      /* a worker's index step is over (its last leaf and its last overflow page are processed) */
      void bitmap_leave_index_step ();
      /* wait until every worker's index step is over; false: the query was interrupted meanwhile */
      bool bitmap_wait_index_step ();

    private:
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;

      /* (C) range/keylist sub-component: owns BTID_INT / key_val_range vector lifecycle. */
      parallel_index_scan::key_range_list m_ranges;
      /* (B) leaf cursor sub-component: own mutex + range cursor + first-descent latch. */
      parallel_index_scan::leaf_page_dispatcher m_leaf;
      /* (A) shared overflow pool: per-slot active-chains vector + helpers/wait. */
      parallel_index_scan::overflow_chain_pool m_ovf;
      int m_parallelism;                              /* workers of the scan. */

      int bitmap_init_on_main (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, int parallelism);

      bitmap_marks m_bm;
      QUERY_ID m_bm_query_id = NULL_QUERY_ID;
      std::mutex m_bm_mutex;
      std::condition_variable m_bm_cv;
      int m_bm_index_pending = 0;	/* workers whose index step is not over; under m_bm_mutex */
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
