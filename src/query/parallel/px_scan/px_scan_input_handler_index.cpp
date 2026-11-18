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

/* px_scan_input_handler_index.cpp — facade: delegates to key_range_list / leaf_page_dispatcher / overflow_chain_pool. */

#include "px_scan_input_handler_index.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "heap_file.h"
#include "list_file.h"
#include "log_impl.h"
#include "mvcc.h"
#include "object_domain.h"
#include "qfile_tuple_layout.h"
#include "query_manager.h"
#include "object_representation_sr.h"
#include "system_parameter.h"

#include <algorithm>
#include <chrono>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd,
				     int parallelism)
  {
    int err = m_ranges.init_on_main (thread_p, indx_info, scan_id, vd);
    if (err != NO_ERROR)
      {
	return err;
      }
    m_leaf.init (&m_ranges);
    m_ovf.init (parallelism);
    m_parallelism = MAX (parallelism, 1);
    return bitmap_init_on_main (thread_p, scan_id, parallelism);
  }

  input_handler_index::~input_handler_index ()
  {
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

    for (int i = 0; i < m_bm_nworkers; i++)
      {
	bitmap_worker &w = m_bm_workers[i];

	if (w.rec.tfile != nullptr)
	  {
	    (void) qmgr_free_list_temp_file (thread_p, m_bm_query_id, w.rec.tfile);
	  }
	if (w.sub.tfile != nullptr)
	  {
	    (void) qmgr_free_list_temp_file (thread_p, m_bm_query_id, w.sub.tfile);
	  }
	if (w.over != nullptr)
	  {
	    qfile_destroy_list (thread_p, w.over);
	    qfile_free_list_id (w.over);
	  }
      }
    delete[] m_bm_workers;
  }

  bitmap_table::~bitmap_table ()
  {
    free (e);
    free (b);
  }

  int
  bitmap_table::init (int entries, int buckets)
  {
    e = (bitmap_entry *) malloc (sizeof (bitmap_entry) * entries);
    b = (std::uint16_t *) calloc (buckets, sizeof (std::uint16_t));
    if (e == nullptr || b == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bitmap_entry) * entries);
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    n = 0;
    cap = entries;
    nb = buckets;
    shift = 64 - __builtin_ctz ((unsigned int) buckets);
    return NO_ERROR;
  }

  void
  bitmap_table::clear ()
  {
    n = 0;
    memset (b, 0, nb * sizeof (std::uint16_t));
  }

  /* a temp page of records: the page header every temp page of a query has, then this header, then the records */
  struct bitmap_page_header
  {
    VPID next;
    std::int32_t n;
    std::int32_t unused;
  };
  static const int BITMAP_PAGE_HEADER = QFILE_PAGE_HEADER_SIZE;

  int
  input_handler_index::bitmap_write (THREAD_ENTRY *thread_p, bitmap_writer &wr)
  {
    const int per_page = (DB_PAGESIZE - BITMAP_PAGE_HEADER - (int) sizeof (bitmap_page_header))
			 / (int) sizeof (bitmap_record);
    const int n = (int) wr.batch.size ();

    if (n == 0)
      {
	return NO_ERROR;
      }
    if (wr.tfile == nullptr)
      {
	wr.tfile = qmgr_create_new_temp_file (thread_p, m_bm_query_id, TEMP_FILE_MEMBUF_NONE);
	if (wr.tfile == nullptr)
	  {
	    ASSERT_ERROR ();
	    return er_errid ();
	  }
      }

    /* group the batch by chain in one pass each way */
    wr.start.assign (wr.chain.size () + 1, 0);
    for (int i = 0; i < n; i++)
      {
	wr.start[wr.where[i] + 1]++;
      }
    for (size_t k = 0; k < wr.chain.size (); k++)
      {
	wr.start[k + 1] += wr.start[k];
      }
    wr.order.resize (n);
    for (int i = 0; i < n; i++)
      {
	wr.order[wr.start[wr.where[i]]++] = (std::uint16_t) i;
      }

    PAGE_PTR page = nullptr;
    std::uint32_t cur = 0;
    int error_code = NO_ERROR;

    for (int i = 0; i < n; i++)
      {
	const bitmap_record &r = wr.batch[wr.order[i]];
	const std::uint32_t where = wr.where[wr.order[i]];

	if (page != nullptr && where != cur)
	  {
	    qmgr_set_dirty_page (thread_p, page, FREE, NULL, wr.tfile);
	    page = nullptr;
	  }
	cur = where;

	bitmap_chain &c = wr.chain[where];

	if (page == nullptr && !VPID_ISNULL (&c.tail))
	  {
	    page = qmgr_get_old_page (thread_p, &c.tail, wr.tfile);
	    if (page == nullptr)
	      {
		ASSERT_ERROR_AND_SET (error_code);
		break;
	      }
	  }
	if (page == nullptr || c.tail_n >= per_page)
	  {
	    VPID vpid;
	    PAGE_PTR fresh = qmgr_get_new_page (thread_p, &vpid, wr.tfile);

	    if (fresh == nullptr)
	      {
		ASSERT_ERROR_AND_SET (error_code);
		break;
	      }
	    if (page != nullptr)
	      {
		((bitmap_page_header *) (page + BITMAP_PAGE_HEADER))->next = vpid;
		qmgr_set_dirty_page (thread_p, page, FREE, NULL, wr.tfile);
	      }
	    else
	      {
		c.first = vpid;
	      }
	    bitmap_page_header *h = (bitmap_page_header *) (fresh + BITMAP_PAGE_HEADER);

	    VPID_SET_NULL (&h->next);
	    h->n = 0;
	    c.tail = vpid;
	    c.tail_n = 0;
	    page = fresh;
	  }
	memcpy (page + BITMAP_PAGE_HEADER + sizeof (bitmap_page_header) + (size_t) c.tail_n * sizeof (bitmap_record), &r,
		sizeof (bitmap_record));
	c.tail_n++;
	((bitmap_page_header *) (page + BITMAP_PAGE_HEADER))->n = c.tail_n;
      }
    if (page != nullptr)
      {
	qmgr_set_dirty_page (thread_p, page, FREE, NULL, wr.tfile);
      }
    wr.batch.clear ();
    wr.where.clear ();
    return error_code;
  }

  /* nobody writes the chain any more: its pages are read without a lock of ours */
  template <typename F>
  int
  input_handler_index::bitmap_read_chain (THREAD_ENTRY *thread_p, const bitmap_writer &wr, std::uint32_t chain,
					  F &&each)
  {
    if (chain >= wr.chain.size ())
      {
	return NO_ERROR;
      }
    for (VPID vpid = wr.chain[chain].first; !VPID_ISNULL (&vpid);)
      {
	PAGE_PTR page = qmgr_get_old_page_read_only (thread_p, &vpid, wr.tfile);
	int error_code = NO_ERROR;

	if (page == nullptr)
	  {
	    ASSERT_ERROR_AND_SET (error_code);
	    return error_code;
	  }
	const bitmap_page_header *h = (const bitmap_page_header *) (page + BITMAP_PAGE_HEADER);
	const bitmap_record *recs = (const bitmap_record *) (page + BITMAP_PAGE_HEADER + sizeof (bitmap_page_header));

	for (int k = 0; k < h->n && error_code == NO_ERROR; k++)
	  {
	    error_code = each (recs[k].page, recs[k].word, recs[k].bits);
	  }
	vpid = h->next;
	qmgr_free_old_page (thread_p, page, wr.tfile);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }
    return NO_ERROR;
  }

  int
  input_handler_index::bitmap_mark (THREAD_ENTRY *thread_p, bitmap_worker *w, const OID *oid, int *sector_hint)
  {
    const int sectid = SECTOR_FROM_PAGEID (oid->pageid);
    int s = *sector_hint;

    if (s < 0 || m_bm_sectors[s].vsid.sectid != sectid || m_bm_sectors[s].vsid.volid != oid->volid)
      {
	int low = 0;
	int high = (int) m_bm_sectors.size () - 1;

	s = -1;
	while (low <= high)
	  {
	    const int mid = (low + high) / 2;
	    const VSID &vsid = m_bm_sectors[mid].vsid;

	    if (vsid.volid == oid->volid && vsid.sectid == sectid)
	      {
		s = mid;
		break;
	      }
	    if (vsid.volid < oid->volid || (vsid.volid == oid->volid && vsid.sectid < sectid))
	      {
		low = mid + 1;
	      }
	    else
	      {
		high = mid - 1;
	      }
	  }
	if (s < 0)
	  {
	    /* a sector the heap got after the list was taken */
	    return NO_ERROR;
	  }
	*sector_hint = s;
      }

    const int page_offset = oid->pageid - SECTOR_FIRST_PAGEID (sectid);

    if ((m_bm_sectors[s].page_bitmap & ((std::uint64_t) 1 << page_offset)) == 0)
      {
	/* a page the heap got after the list was taken */
	return NO_ERROR;
      }
    if (oid->slotid >= BITMAP_WORDS * 64)
      {
	DB_VALUE value;
	DB_VALUE *value_p = &value;

	if (w->over == nullptr)
	  {
	    TP_DOMAIN *domain = &tp_Oid_domain;
	    QFILE_TUPLE_VALUE_TYPE_LIST type_list;

	    type_list.type_cnt = 1;
	    type_list.domp = &domain;
	    w->over = qfile_open_list (thread_p, &type_list, NULL, m_bm_query_id, 0, NULL);
	    if (w->over == nullptr)
	      {
		int error_code = er_errid ();

		return error_code == NO_ERROR ? ER_FAILED : error_code;
	      }
	  }
	db_make_oid (&value, oid);
	return qfile_add_values_tuple_to_list (thread_p, w->over, &value_p, 1);
      }

    const std::uint32_t page = (std::uint32_t) s * DISK_SECTOR_NPAGES + page_offset;
    const int word = oid->slotid >> 6;
    const std::uint64_t bit = (std::uint64_t) 1 << (oid->slotid & 63);

    if (!w->switched)
      {
	bitmap_entry *p = w->tbl.get (page);

	if (p != nullptr)
	  {
	    p->w[word] |= bit;
	    return NO_ERROR;
	  }
	/* full for the first time: the entries outside the first partition's first part leave as records */
	std::vector<bitmap_entry> kept;

	for (int i = 0; i < w->tbl.n; i++)
	  {
	    const bitmap_entry &x = w->tbl.e[i];

	    if ((x.page >> 6) < m_bm_first_unit_sectors)
	      {
		kept.push_back (x);
		continue;
	      }
	    const std::uint32_t part = bitmap_unit_of (x.page) / m_bm_nsub;

	    for (int k = 0; k < BITMAP_WORDS; k++)
	      {
		if (x.w[k] != 0)
		  {
		    int error_code = bitmap_add (thread_p, w->rec, part, x.page, k, x.w[k]);

		    if (error_code != NO_ERROR)
		      {
			return error_code;
		      }
		  }
	      }
	  }
	w->tbl.clear ();
	for (const bitmap_entry &x : kept)
	  {
	    memcpy (w->tbl.get (x.page)->w, x.w, sizeof (x.w));
	  }
	w->switched = true;
      }

    if ((std::uint32_t) s < m_bm_first_unit_sectors)
      {
	bitmap_entry *p = w->tbl.get (page);

	if (p == nullptr)
	  {
	    /* a part's pages fit a table: bitmap_init_on_main */
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	    return ER_GENERIC_ERROR;
	  }
	p->w[word] |= bit;
	return NO_ERROR;
      }
    return bitmap_add (thread_p, w->rec, bitmap_unit_of (page) / m_bm_nsub, page, word, bit);
  }

  int
  input_handler_index::bitmap_end_marks (THREAD_ENTRY *thread_p, bitmap_worker *w)
  {
    if (w->over != nullptr)
      {
	/* the list's last page is fixed by this thread */
	qfile_close_list (thread_p, w->over);
      }
    /* the table's entries by partition, so that a heap worker reads those of its partition only */
    w->tbl_start.assign (m_bm_nparts + 1, 0);
    for (int i = 0; i < w->tbl.n; i++)
      {
	w->tbl_start[bitmap_unit_of (w->tbl.e[i].page) / m_bm_nsub + 1]++;
      }
    for (std::uint32_t k = 0; k < m_bm_nparts; k++)
      {
	w->tbl_start[k + 1] += w->tbl_start[k];
      }
    w->tbl_order.resize (w->tbl.n);
    for (int i = 0; i < w->tbl.n; i++)
      {
	w->tbl_order[w->tbl_start[bitmap_unit_of (w->tbl.e[i].page) / m_bm_nsub]++] = (std::uint16_t) i;
      }
    /* each start moved to its partition's end, the next one's start */
    for (std::uint32_t k = m_bm_nparts; k > 0; k--)
      {
	w->tbl_start[k] = w->tbl_start[k - 1];
      }
    w->tbl_start[0] = 0;
    return bitmap_write (thread_p, w->rec);
  }

  int
  input_handler_index::bitmap_load (THREAD_ENTRY *thread_p, bitmap_worker *w, std::uint32_t part, std::uint32_t sub)
  {
    bitmap_table &table = w->heap_tbl;
    int error_code = NO_ERROR;
    /* an entry of the part being read */
    auto keep = [&table] (std::uint32_t page, int word, std::uint64_t bits) -> int
    {
      bitmap_entry *p = table.get (page);

      if (p == nullptr)
	{
	  /* a part's pages fit a table: bitmap_init_on_main */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
      p->w[word] |= bits;
      return NO_ERROR;
    };

    if (table.e == nullptr)
      {
	error_code = table.init (m_bm_cap, m_bm_buckets);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }
    table.clear ();

    if (sub > 0)
      {
	error_code = bitmap_read_chain (thread_p, w->sub, sub, keep);
      }
    else
      {
	/* the partition's first part stays here; the other parts go to this worker's own temp file */
	auto put = [&] (std::uint32_t page, int word, std::uint64_t bits) -> int
	{
	  const std::uint32_t to = m_bm_nsub > 1 ? bitmap_unit_of (page) % m_bm_nsub : 0;

	  return to == 0 ? keep (page, word, bits) : bitmap_add (thread_p, w->sub, to, page, word, bits);
	};

	if (m_bm_nsub > 1)
	  {
	    /* shortcut: the pages the previous partition's parts took stay in the temp file until the scan ends,
	     * reuse them when heaps of this size become common */
	    w->sub.chain.assign (m_bm_nsub, bitmap_chain { VPID_INITIALIZER, VPID_INITIALIZER, 0 });
	  }
	for (int i = 0; i < m_bm_nworkers && error_code == NO_ERROR; i++)
	  {
	    const bitmap_worker &from = m_bm_workers[i];

	    /* a worker that left the index step by an error has no entries in order, and the scan has stopped */
	    for (std::uint32_t k = from.tbl_start.empty () ? 0 : from.tbl_start[part];
		 !from.tbl_start.empty () && k < from.tbl_start[part + 1] && error_code == NO_ERROR; k++)
	      {
		const bitmap_entry &x = from.tbl.e[from.tbl_order[k]];

		for (int j = 0; j < BITMAP_WORDS && error_code == NO_ERROR; j++)
		  {
		    if (x.w[j] != 0)
		      {
			error_code = put (x.page, j, x.w[j]);
		      }
		  }
	      }
	    if (error_code == NO_ERROR && from.rec.tfile != nullptr)
	      {
		error_code = bitmap_read_chain (thread_p, from.rec, part, put);
	      }
	  }
	if (error_code == NO_ERROR)
	  {
	    error_code = bitmap_write (thread_p, w->sub);
	  }
      }
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    w->order.resize (table.n);
    for (int i = 0; i < table.n; i++)
      {
	w->order[i] = i;
      }
    std::sort (w->order.begin (), w->order.end (), [&table] (std::uint32_t x, std::uint32_t y)
    {
      return table.e[x].page < table.e[y].page;
    });
    return NO_ERROR;
  }

  bool
  input_handler_index::bitmap_take_over (THREAD_ENTRY *thread_p, bitmap_worker *taker, int *error_code)
  {
    bool expected = false;
    bitmap_worker *base = nullptr;

    *error_code = NO_ERROR;
    if (!m_bm_over_taken.compare_exchange_strong (expected, true))
      {
	return false;
      }
    for (int i = 0; i < m_bm_nworkers; i++)
      {
	bitmap_worker &w = m_bm_workers[i];

	if (w.over == nullptr || w.over->tuple_cnt == 0)
	  {
	    continue;
	  }
	if (base == nullptr)
	  {
	    base = &w;
	    continue;
	  }
	*error_code = qfile_append_list (thread_p, base->over, w.over);
	if (*error_code != NO_ERROR)
	  {
	    return false;
	  }
      }
    if (base == nullptr)
      {
	return false;
      }
    /* two keys of one row, or two workers, record the same OID twice */
    if (qfile_sort_list (thread_p, base->over, NULL, Q_DISTINCT, true) == nullptr)
      {
	/* the sort destroyed the list's file; the list itself goes with its worker */
	ASSERT_ERROR_AND_SET (*error_code);
	return false;
      }
    if (qfile_open_list_scan (base->over, &m_bm_over_scan) != NO_ERROR)
      {
	ASSERT_ERROR_AND_SET (*error_code);
	return false;
      }
    m_bm_over_list = base->over;
    taker->scans_over = true;
    return true;
  }

  SCAN_CODE
  input_handler_index::bitmap_next_over (THREAD_ENTRY *thread_p, OID *oid)
  {
    DB_VALUE value;
    bool is_null;
    SCAN_CODE scan = qfile_scan_list_next (thread_p, &m_bm_over_scan, &m_bm_over_tuple, PEEK);

    if (scan != S_SUCCESS)
      {
	return scan;
      }
    if (qfile_slot_read_column_value (&m_bm_over_tuple, 0, m_bm_over_list->type_list.domp[0], &value, false, &is_null)
	!= NO_ERROR || is_null)
      {
	assert (false);
	return S_ERROR;
      }
    COPY_OID (oid, db_get_oid (&value));
    return S_SUCCESS;
  }

  void
  input_handler_index::bitmap_close_worker (THREAD_ENTRY *thread_p, bitmap_worker *w)
  {
    if (w->over != nullptr)
      {
	qfile_close_list (thread_p, w->over);
      }
    if (w->scans_over)
      {
	qfile_slot_clear (&m_bm_over_tuple);
	qfile_close_scan (thread_p, &m_bm_over_scan);
	w->scans_over = false;
      }
  }

  int
  input_handler_index::bitmap_init_on_main (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, int parallelism)
  {
    INDX_SCAN_ID *isidp = &scan_id->s.isid;
    FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
    int error_code;

    /* the plan decides; a scan that reads no heap row per OID, or that returns its rows in key order, has nothing
     * to read in file order */
    if (!m_bm_planned || isidp->indx_info == nullptr || isidp->indx_info->coverage != 0 || isidp->need_count_only
	|| isidp->gby_skip != nullptr || isidp->multi_range_opt.use || isidp->iss.use
	|| mvcc_is_mvcc_disabled_class (&isidp->cls_oid))
      {
	return NO_ERROR;
      }

    /* The sector list below misses every page allocated after it. With the statement's snapshot taken first, the
     * rows on those pages all belong to transactions the snapshot does not see, so a mark outside the list is
     * dropped. */
    if (logtb_get_mvcc_snapshot (thread_p) == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
	return ER_MVCC_CANT_GET_SNAPSHOT;
      }
    error_code = file_get_all_data_sectors (thread_p, &isidp->hfid.vfid, &collector);
    if (error_code == NO_ERROR && collector.nsects > 0)
      {
	const size_t nsects = (size_t) collector.nsects;
	/* a table and its buckets take the memory of an index scan's key buffer; an entry is found by a 16-bit place */
	const size_t memory = (size_t) MAX (prm_get_integer_value (PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES), 1) * DB_PAGESIZE;
	size_t buckets = 256;
	size_t units = 1;

	while (buckets < 2 * (memory / 96) && buckets < 65536)
	  {
	    buckets *= 2;
	  }
	m_bm_buckets = (int) buckets;
	m_bm_cap = (int) ((memory - MIN (memory, buckets * sizeof (std::uint16_t))) / sizeof (bitmap_entry));
	m_bm_cap = MAX (MIN (m_bm_cap, m_bm_buckets / 2), DISK_SECTOR_NPAGES);

	m_bm_sectors.assign (collector.partsect_ftab, collector.partsect_ftab + nsects);
	std::sort (m_bm_sectors.begin (), m_bm_sectors.end (),
		   [] (const FILE_PARTIAL_SECTOR &a, const FILE_PARTIAL_SECTOR &b)
	{
	  return a.vsid.volid != b.vsid.volid ? a.vsid.volid < b.vsid.volid : a.vsid.sectid < b.vsid.sectid;
	});

	/* a part is a run of whole sectors: the sectors of the largest one must fit a table. A small heap still gets
	 * a few partitions a worker, so that the heap step is shared as the index step is. */
	while ((nsects + units - 1) / units * DISK_SECTOR_NPAGES > (size_t) m_bm_cap
	       || (units < (size_t) MAX (parallelism, 1) * 4 && units < nsects))
	  {
	    units *= 2;
	  }
	m_bm_nparts = (std::uint32_t) MIN (units, (size_t) BITMAP_MAX_PARTITIONS);
	m_bm_nsub = (std::uint32_t) (units / m_bm_nparts);
	m_bm_first_unit_sectors = (std::uint32_t) ((nsects + units - 1) / units);

	m_bm_nworkers = MAX (parallelism, 1);
	/* the memory wrapper's operator new returns NULL when out of memory */
	m_bm_workers = new bitmap_worker[m_bm_nworkers];
	if (m_bm_workers == nullptr)
	  {
	    m_bm_nworkers = 0;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		    sizeof (bitmap_worker) * MAX (parallelism, 1));
	    error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  }
	for (int i = 0; i < m_bm_nworkers && error_code == NO_ERROR; i++)
	  {
	    error_code = m_bm_workers[i].tbl.init (m_bm_cap, m_bm_buckets);
	    m_bm_workers[i].rec.chain.assign (m_bm_nparts, bitmap_chain { VPID_INITIALIZER, VPID_INITIALIZER, 0 });
	    m_bm_workers[i].rec.batch.reserve (BITMAP_BATCH);
	    m_bm_workers[i].rec.where.reserve (BITMAP_BATCH);
	  }
	if (error_code == NO_ERROR)
	  {
	    m_bm_index_pending = parallelism;
	    m_bm_on = true;
	  }
      }
    if (collector.partsect_ftab != NULL)
      {
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }
    return error_code;
  }

  void
  input_handler_index::bitmap_leave_index_step ()
  {
    std::lock_guard<std::mutex> lock (m_bm_mutex);

    if (--m_bm_index_pending == 0)
      {
	m_bm_cv.notify_all ();
      }
  }

  bool
  input_handler_index::bitmap_wait_index_step ()
  {
    std::unique_lock<std::mutex> lock (m_bm_mutex);

    /* a worker that failed before its index step never leaves it: it raised the interrupt instead */
    while (m_bm_index_pending > 0 && m_interrupt_p->get_code () == interrupt::interrupt_code::NO_INTERRUPT)
      {
	m_bm_cv.wait_for (lock, std::chrono::milliseconds (10));
      }
    return m_interrupt_p->get_code () == interrupt::interrupt_code::NO_INTERRUPT;
  }

  /* No-op: slot_iterator_index drives the leaf-page cursor directly. */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    return NO_ERROR;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    /* no-op per worker: siblings still scan m_key_val_ranges; freed in cleanup_keys on main thread. */
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    m_ranges.cleanup_keys (thread_p);
    /* per-helper local_key owned by slot_iterator's m_slot_key; cleared at OVERFLOW_SHARED exit. */
  }
}
