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

#include <cassert>
#include "error_code.h"
#include "file_manager.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "porting_inline.hpp"
#include "scope_exit.hpp"
#include "slotted_page.h"
#include "storage_common.h"
#include "page_buffer_support.hpp"

#include "oos_file.hpp"
#include "oos_log.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// ****************************************************************************
// static functions
// ****************************************************************************

static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid);

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);

static int oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				   const OOS_RECORD_HEADER &header, OID &oid);
static int oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				    OID &oid);
static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
		      OOS_RECORD_HEADER &out_header);
static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
		       OOS_RECORD_HEADER &out_header);

STATIC_INLINE __attribute__ ((ALWAYS_INLINE)) int oos_get_max_chunk_size_within_page ();

static int get_recently_inserted_oos_vpid (const VFID &oos_vfid, VPID &vpid);

// ****************************************************************************
// memory map used to store recently inserted OOS VPID for each OOS VFID
//
// this is used in oos_find_best_page to quickly find the last inserted page
// ****************************************************************************

struct VFIDHash
{
  std::size_t operator() (const VFID &v) const noexcept
  {
    return std::hash<int32_t> {} (v.fileid) ^ (std::hash<short> {} (v.volid) << 1);
  }
};
struct VFIDEq
{
  bool operator() (const VFID &a, const VFID &b) const noexcept
  {
    return a.fileid == b.fileid && a.volid == b.volid;
  }
};
static std::unordered_map<VFID, VPID, VFIDHash, VFIDEq> oos_recently_inserted_oos_vpid_map;

// ****************************************************************************

using namespace oos_log;

// review point: should it be 8?
static constexpr int OOS_ALIGNMENT = 8;


int
oos_create (THREAD_ENTRY *thread_p, VFID &oos_vfid)
{
  FILE_DESCRIPTORS des; // unused
  int err = NO_ERROR;
  err = file_create_with_npages (thread_p, FILE_OOS, 1, &des, &oos_vfid);
  if (err != NO_ERROR)
    {
      oos_error ("file_create_with_npages failed");
      return err;
    }
  return NO_ERROR;
}

int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  // TODO: actually destroy the OOS file
  return 0;
}


static int oos_prepend_record_header (RECDES &rec_in, const OOS_RECORD_HEADER &oos_header, RECDES &rec_out)
{
  // Review point:
  // This function just prepends the OOS header to the input record.
  //
  // While it doing simple job, it allocated new data area for output record.
  // Can this be avoided to improve performance?

  int err;
  err = recdes_allocate_data_area (&rec_out, rec_in.length + (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      return err;
    }

  // Review point: REC_HOME is required to avoid assertion failure in spage_insert.
  rec_out.type = REC_HOME;
  rec_out.length = rec_in.length + (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (rec_out.data, &oos_header, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (rec_out.data + (int)sizeof (OOS_RECORD_HEADER), rec_in.data, rec_in.length);
  return NO_ERROR;
}


static int oos_pop_record_header (RECDES &rec_in, OOS_RECORD_HEADER &header_out, RECDES &rec_out)
{
  assert (rec_in.length >= (int)sizeof (OOS_RECORD_HEADER));
  assert (&rec_in != &rec_out);

  int err;
  err = recdes_allocate_data_area (&rec_out, rec_in.length - (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      oos_error ("recdes_allocate_data_area failed");
      return err;
    }
  rec_out.type = REC_HOME;
  rec_out.length = rec_in.length - (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (&header_out, rec_in.data, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (rec_out.data, rec_in.data + (int)sizeof (OOS_RECORD_HEADER), rec_out.length);
  return NO_ERROR;
}


int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  oos_debug ("arguments: oos_vfid={fileid=%d, volid=%d}, recdes.length=%d",
	     oos_vfid.fileid, oos_vfid.volid, recdes.length);
  int err = NO_ERROR;

  // TODO: otherwise spage assert type <= REC_UNKNOWN fails
  assert (recdes.type == REC_HOME);
  assert (recdes.length > 0);

  if (recdes.length <= oos_get_max_chunk_size_within_page ())
    {
      const OOS_RECORD_HEADER header{recdes.length, 0, OID_INITIALIZER};
      return oos_insert_within_page (thread_p, oos_vfid, recdes, header, oid);
    }
  else
    {
      return oos_insert_across_pages (thread_p, oos_vfid, recdes, oid);
    }
}


static int oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;

  // split the recdes to multiple chunks and insert them one by one
  const int max_chunk_size = oos_get_max_chunk_size_within_page ();
  assert (recdes.length + (int)sizeof (OOS_RECORD_HEADER) > max_chunk_size);

  int required_page_nums = (recdes.length + max_chunk_size - 1) / max_chunk_size;
  assert (required_page_nums > 1);

  const int total_size = recdes.length;

  int total_inserted_size = 0;
  OID next_chunk_oid = OID_INITIALIZER; // the last chunk has null OID as next_chunk_oid
  // this loop inserts chunks in reverse order so that next_chunk_oid is always known
  for (int i = required_page_nums - 1; i >= 0; --i)
    {

      RECDES chunk_recdes{};
      chunk_recdes.type = REC_HOME;
      chunk_recdes.length = std::min (max_chunk_size, total_size - i * max_chunk_size);
      total_inserted_size += chunk_recdes.length;
      chunk_recdes.data = recdes.data + i * max_chunk_size;

      //  TODO: code review feedback
      //
      // - Distinguish between the record header and segment header for clarity.
      // - 2nd to nth chunks do not need total_size in their headers, only the 1st chunk needs it.
      // - If wanted for debug purposes, use NDEBUG
      //
      OOS_RECORD_HEADER header{total_size, i, next_chunk_oid};

      OID current_chunk_oid;
      err = oos_insert_within_page (thread_p, oos_vfid, chunk_recdes, header, current_chunk_oid);
      if (err != NO_ERROR)
	{
	  oos_error ("could not insert chunk index=%d of length %d.", i, chunk_recdes.length);
	  // TODO: free partially inserted chunks.
	  // Currently, already inserted chunks to slotted pages will remain as garbage.
	  return err;
	}

      next_chunk_oid = current_chunk_oid;
    }
  assert (total_inserted_size == recdes.length);

  // update the out parameter 'oid' to give access to the first slot
  oid = next_chunk_oid;
  return NO_ERROR;
}


static int oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				   const OOS_RECORD_HEADER &header,
				   OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;

  assert (recdes.length <= oos_get_max_chunk_size_within_page ());

  int required_length = recdes.length + (int)sizeof (OOS_RECORD_HEADER);

  assert (required_length <= DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT));

  auto auto_page_ptr = oos_find_best_page (thread_p, oos_vfid, required_length, vpid);

  RECDES oos_rec{};
  {
    err = oos_prepend_record_header (recdes, header, oos_rec);
    if (err != NO_ERROR)
      {
	oos_error ("oos_prepend_record_header failed");
	return err;
      }
    // oos_prepend_record_header allocates data area for oos_rec
    // therefore, we need to free it after use
    scope_exit oos_rec_freer ([&]()
    {
      recdes_free_data_area (&oos_rec);
    });

    PGSLOTID slotid = NULL_SLOTID;
    PAGE_PTR page_ptr = auto_page_ptr.get();
    int sp_status = spage_insert (thread_p, page_ptr, &oos_rec, &slotid);
    if (sp_status != SP_SUCCESS)
      {
	oos_error ("spage_insert failed with status %d", sp_status);
	return ER_FAILED;
      }
    assert (slotid != NULL_SLOTID);

    try
      {
	auto result = oos_recently_inserted_oos_vpid_map.insert_or_assign (oos_vfid, vpid);
      }
    catch (const std::bad_alloc &)
      {
	oos_error ("memory allocation failed while inserting into map");
	return ER_FAILED;
      }
    catch (const std::exception &e)
      {
	oos_error ("exception while inserting into map: %s", e.what());
	return ER_FAILED;
      }

    oid.pageid = vpid.pageid;
    oid.slotid = slotid;
    oid.volid = vpid.volid;
  }
  assert (oos_rec.data == nullptr); // should be freed by scope_exit

  return NO_ERROR;
}


static int oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &oid,
				  const OOS_RECORD_HEADER &first_chunk_header, RECDES &recdes)
{
  int err = NO_ERROR;
  const int total_size = first_chunk_header.total_size;
  assert (first_chunk_header.chunk_index == 0);

  assert (total_size > oos_get_max_chunk_size_within_page ());
  oos_trace ("total_size=%d", total_size);

  err = recdes_allocate_data_area (&recdes, total_size);
  if (err != NO_ERROR)
    {
      return err;
    }

  recdes.type = REC_HOME;
  recdes.length = total_size;

  int idx = 0;
  OID current_chunk_oid = oid;
  char *buf = recdes.data;
  int total_read_size = 0;
  while (current_chunk_oid.pageid != NULL_PAGEID)
    {
      OOS_RECORD_HEADER header;
      RECDES chunk_recdes;
      {
	int err = oos_read_within_page (thread_p, current_chunk_oid, chunk_recdes, header);
	if (err != NO_ERROR)
	  {
	    oos_error ("oos_read_within_page failed for chunk index=%d", idx);
	    return err;
	  }
	scope_exit defer_free_chunk_recdes ([&]()
	{
	  recdes_free_data_area (&chunk_recdes);
	});

	assert (idx == header.chunk_index);
	assert (total_size == header.total_size);

	total_read_size += chunk_recdes.length;
	std::memcpy (buf, chunk_recdes.data, chunk_recdes.length);
	buf += chunk_recdes.length;

	current_chunk_oid = header.next_chunk_oid;
      }
      assert (chunk_recdes.data == nullptr); // should be freed by scope_exit

      idx++;
    }

  assert (total_read_size == total_size);
  assert (buf == total_size + recdes.data);

  return NO_ERROR;
}


static int oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
				 OOS_RECORD_HEADER &header_out)
{
  int err = NO_ERROR;
  const auto [pageid, slotid, volid] = oid;
  auto vpid = VPID{pageid, volid};

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      oos_error ("oos_read_within_page: pgbuf_fix failed for volid=%d, pageid=%d", volid, pageid);
      return ER_FAILED;
    }
  scope_exit page_unfixer ([&]()
  {
    pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  });

  RECDES recdes_with_oos_header;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, slotid, &recdes_with_oos_header, PEEK);
  if (code != S_SUCCESS)
    {
      oos_error ("oos_read_within_page: spage_get_record failed for volid=%d, pageid=%d, slotid=%d",
		 volid, pageid, slotid);
      return ER_FAILED;
    }

  err = oos_pop_record_header (recdes_with_oos_header, header_out, recdes);
  if (err != NO_ERROR)
    {
      oos_error ("oos_pop_record_header failed for volid=%d, pageid=%d, slotid=%d",
		 volid, pageid, slotid);
      return err;
    }
  return NO_ERROR;
}


/* oos_read -
 *
 * return:
 *
 *   oos_vfid(in):
 *   oid(in):
 *   recdes(out):
 *
 *   important notes: recdes.data is allocated inside this function.
 *   It sholud be freed by the caller using recdes_free_data_area().
 */
int
oos_read (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes)
{
  oos_debug ("arguments: oos_vfid={fileid=%d, volid=%d}, oid={pageid=%d, slotid=%d, volid=%d}",
	     oid.pageid, oid.slotid, oid.volid);

  int err = NO_ERROR;

  // try reading just one slot
  OOS_RECORD_HEADER first_chunk_header;
  RECDES first_chunk_recdes;
  err = oos_read_within_page (thread_p, oid, first_chunk_recdes, first_chunk_header);
  if (err != NO_ERROR)
    {
      oos_error ("oos_read_within_page failed");
      return err;
    }

  // check if we need to read all the chunks
  if (first_chunk_header.next_chunk_oid.slotid != NULL_SLOTID)
    {
      err = oos_read_across_pages (thread_p, oid, first_chunk_header, recdes);
      recdes_free_data_area (&first_chunk_recdes);
      if (err != NO_ERROR)
	{
	  oos_error ("oos_read_across_pages failed");
	  return err;
	}
    }
  else
    {
      recdes = first_chunk_recdes;
    }
  oos_trace ("read completed, total_size=%d", first_chunk_header.total_size);

  return NO_ERROR;
}


static int get_recently_inserted_oos_vpid (const VFID &oos_vfid, VPID &vpid)
{
  auto it = oos_recently_inserted_oos_vpid_map.find (oos_vfid);
  if (it != oos_recently_inserted_oos_vpid_map.end ())
    {
      vpid = it->second;
      return NO_ERROR;
    }
  else
    {
      vpid = VPID_INITIALIZER;
      return ER_FAILED;
    }
  return NO_ERROR;
}


static auto_unfix_page_ptr oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
    VPID &vpid_out)
{
  int err = NO_ERROR;
  PAGE_TYPE page_type = PAGE_OOS;
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid_out, nullptr);
  if (err)
    {
      oos_error ("file_alloc failed");
      return nullptr;
    }
  oos_trace ("allocated new page {volid=%d, pageid=%d}",
	     vpid_out.volid, vpid_out.pageid);

  return pgbuf_fix_auto_unfix (thread_p, &vpid_out, OLD_PAGE,
			       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
}


static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  int err = 0;
  PAGE_TYPE page_type = PAGE_OOS;

  VPID recently_inserted_oos_vpid = VPID_INITIALIZER;
  auto it = oos_recently_inserted_oos_vpid_map.find (oos_vfid);
  if (it == oos_recently_inserted_oos_vpid_map.end ())
    {
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  recently_inserted_oos_vpid = it->second;
  if (recently_inserted_oos_vpid.pageid == NULL_PAGEID)
    {
      assert (false); // should not happen
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  auto auto_page_ptr = pgbuf_fix_auto_unfix (thread_p, &recently_inserted_oos_vpid, OLD_PAGE,
		       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (auto_page_ptr == nullptr)
    {
      assert (false); // should not happen
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  PAGE_PTR page_ptr = auto_page_ptr.get();

  auto [pageid, volid] = recently_inserted_oos_vpid;
  int freespace = spage_get_free_space (thread_p, page_ptr);
  oos_trace ("recently inserted page {volid=%d, pageid=%d} has freespace=%d",
	     volid, pageid, freespace);

  if (freespace >= rec_length + (int)sizeof (SPAGE_SLOT))
    {
      oos_trace ("reusing recently inserted page {volid=%d, pageid=%d} with freespace=%d",
		 recently_inserted_oos_vpid.volid, recently_inserted_oos_vpid.pageid, freespace);
      vpid = recently_inserted_oos_vpid;
      return auto_page_ptr;
    }

  oos_trace ("recently inserted page {volid=%d, pageid=%d} does not have enough space",
	     recently_inserted_oos_vpid.volid, recently_inserted_oos_vpid.pageid);

  return oos_file_alloc_new (thread_p, oos_vfid, vpid);
}


static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
{
  int err = NO_ERROR;

  err = file_init_page_type (thread_p, page, args);
  if (err != NO_ERROR)
    {
      return err;
    }
  spage_initialize (thread_p, page, ANCHORED, OOS_ALIGNMENT, false);
  return err;
}


// TODO: since this value never changes, we can make it a constant or static variable,
// and make it initialized only once in something like oos_boot().
STATIC_INLINE __attribute__ ((ALWAYS_INLINE)) int oos_get_max_chunk_size_within_page ()
{
  // TODO: fix bug for spage_max_record_size returning incorrect size, which is out of scope for OOS project.
  const int actual_upper_limit = DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT);

  return actual_upper_limit - (int)sizeof (OOS_RECORD_HEADER);
}


#if defined(CUBRID_UNIT_TEST_ENABLED)
int bridge_oos_get_max_chunk_size_within_page ()
{
  return oos_get_max_chunk_size_within_page ();
}

int bridge_oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
{
  return oos_vpid_init_new (thread_p, page, args);
}

int bridge_oos_get_recently_inserted_oos_vpid (const VFID &oos_vfid, VPID &vpid)
{
  return get_recently_inserted_oos_vpid (oos_vfid, vpid);
}

const auto_unfix_page_ptr bridge_oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length,
    VPID &vpid)
{
  return oos_find_best_page (thread_p, oos_vfid, rec_length, vpid);
}
#endif

