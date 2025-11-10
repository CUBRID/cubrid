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
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"
#include "oos_util.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static std::unordered_map<const VFID *, VPID> oos_recently_inserted_oos_vpid_map;

int oos_get_max_chunk_size_within_page ()
{
  // one additional slot and OOS header are needed within page
  return spage_max_record_size () - (int)sizeof (SPAGE_SLOT) - (int)sizeof (OOS_RECORD_HEADER);
}

int
oos_create (THREAD_ENTRY *thread_p, VFID &oos_vfid)
{

  FILE_DESCRIPTORS des; // unused
  int err = file_create_with_npages (thread_p, FILE_OOS, 1, &des, &oos_vfid);
  if (err != NO_ERROR)
    {
      return err;
    }

  return 0;
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
  return err;
}

static void oos_pop_record_header (RECDES &rec_in, OOS_RECORD_HEADER &header_out, RECDES &rec_out)
{
  assert (rec_in.length >= (int)sizeof (OOS_RECORD_HEADER));
  assert (&rec_in != &rec_out);

  recdes_allocate_data_area (&rec_out, rec_in.length - (int)sizeof (OOS_RECORD_HEADER));
  rec_out.type = REC_HOME;
  rec_out.length = rec_in.length - (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (&header_out, rec_in.data, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (rec_out.data, rec_in.data + (int)sizeof (OOS_RECORD_HEADER), rec_out.length);
}

int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;

  // TODO: otherwise spage assert type <= REC_UNKNOWN fails
  assert (recdes.type == REC_HOME);

  if (err != NO_ERROR)
    {
      return err;
    }

  if (recdes.length > oos_get_max_chunk_size_within_page ())
    {
      return oos_insert_across_pages (thread_p, oos_vfid, recdes, oid);
    }
  else
    {
      const OOS_RECORD_HEADER header{recdes.length, 0, OID_INITIALIZER};
      return oos_insert_within_page (thread_p, oos_vfid, recdes, header, oid);
    }
}


static int oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;

  // split the recdes to multiple chunks and insert them one by one
  const int max_chunk_size = oos_get_max_chunk_size_within_page ();
  assert (recdes.length > max_chunk_size);

  int required_page_nums = (recdes.length + max_chunk_size - 1) / max_chunk_size;
  assert (required_page_nums > 1);

  oos_log ("oos_insert_across_pages: required_page_nums=%d\n", required_page_nums);

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

      OOS_RECORD_HEADER header{total_size, i, next_chunk_oid};
      oos_log ("oos_insert_large: header = {total_size=%d, chunk_index=%d, next_chunk_oid={pageid=%d, slotid=%d}}\n",
	       header.total_size, header.chunk_index,
	       header.next_chunk_oid.pageid, header.next_chunk_oid.slotid);

      OID current_chunk_oid;
      err = oos_insert_within_page (thread_p, oos_vfid, chunk_recdes, header, current_chunk_oid);
      if (err != NO_ERROR)
	{
	  // TODO: cleanup previously inserted chunks
	  break;
	}

      next_chunk_oid = current_chunk_oid;
    }

  assert (total_inserted_size == recdes.length);

  // update the out parameter 'oid' to give access to the first slot
  oid = next_chunk_oid;
  return err;
}

static int oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				   const OOS_RECORD_HEADER &header,
				   OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;

  assert (recdes.length <= oos_get_max_chunk_size_within_page ());

  int required_length = recdes.length + (int)sizeof (OOS_RECORD_HEADER);
  err = oos_find_best_page (thread_p, oos_vfid, required_length, vpid);
  if (err != NO_ERROR)
    {
      return err;
    }

  PAGE_PTR raw_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (raw_ptr == nullptr)
    {
      return ER_FAILED;
    }
  auto_unfixed_page_ptr page_ptr {raw_ptr, page_auto_unfix {thread_p} };


  RECDES oos_rec{};
  {
    err = oos_prepend_record_header (recdes, header, oos_rec);
    // oos_prepend_record_header allocates data area for oos_rec
    // therefore, we need to free it after use
    auto_freed_recdes_ptr oos_rec_will_be_freed (&oos_rec, recdes_free_data_area);

    if (err != NO_ERROR)
      {
	return err;
      }

    PGSLOTID slotid = NULL_SLOTID;
    int sp_status = spage_insert (thread_p, page_ptr.get(), &oos_rec, &slotid);
    if (sp_status != SP_SUCCESS)
      {
	oos_log ("oos_insert_within_page: spage_insert failed with status %d\n", sp_status);
	return ER_FAILED;
      }
    assert (slotid != NULL_SLOTID);

    oos_recently_inserted_oos_vpid_map.emplace (&oos_vfid, vpid);
    printf("recently inserted oos vpid map size: %zu\n", oos_recently_inserted_oos_vpid_map.size());

    oid.pageid = vpid.pageid;
    oid.slotid = slotid;
    oid.volid = vpid.volid;
  }
  assert (oos_rec.data == nullptr); // should be freed by auto_freed_recdes_ptr

  return NO_ERROR;
}

static int oos_read_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
				  const OOS_RECORD_HEADER &first_chunk_header, RECDES &recdes)
{
  int err = NO_ERROR;
  const auto header = first_chunk_header;
  const int total_size = first_chunk_header.total_size;
  assert (first_chunk_header.chunk_index == 0);
  assert (total_size > oos_get_max_chunk_size_within_page ());

  oos_log ("oos_read_across_pages: total_size=%d\n", total_size);

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
	int err = oos_read_within_page (thread_p, oos_vfid, current_chunk_oid, chunk_recdes, header);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
	auto_freed_recdes_ptr chunk_recdes_will_be_freed (&chunk_recdes, recdes_free_data_area);

	oos_log ("read_large: header = {total_size=%d, chunk_index=%d, next_chunk_oid={pageid=%d, slotid=%d}}\n",
		 header.total_size,
		 header.chunk_index, header.next_chunk_oid.pageid, header.next_chunk_oid.slotid);

	total_read_size += chunk_recdes.length;
	std::memcpy (buf, chunk_recdes.data, chunk_recdes.length);
	buf += chunk_recdes.length;
	oos_log ("oos_read_across_pages: read chunk index=%d, chunk_size=%d\n", idx, chunk_recdes.length);
	current_chunk_oid = header.next_chunk_oid;
      }
      assert (chunk_recdes.data == nullptr); // should be freed by auto_freed_recdes_ptr
    }

  assert (total_read_size == total_size);
  assert (buf == total_size + recdes.data);

  return NO_ERROR;
}

static int oos_read_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
				 OOS_RECORD_HEADER &header_out)
{
  const auto [pageid, slotid, volid] = oid;
  auto vpid = VPID{pageid, volid};

  PAGE_PTR raw_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (raw_ptr == nullptr)
    {
      return ER_FAILED;
    }
  auto_unfixed_page_ptr page_ptr { raw_ptr, page_auto_unfix {thread_p} };

  RECDES recdes_with_oos_header;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr.get(), slotid, &recdes_with_oos_header, PEEK);
  if (code != S_SUCCESS)
    {
      return ER_FAILED;
    }

  oos_pop_record_header (recdes_with_oos_header, header_out, recdes);
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
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  int err = NO_ERROR;

  // try reading just one slot
  OOS_RECORD_HEADER first_chunk_header;
  RECDES first_chunk_recdes;
  err = oos_read_within_page (thread_p, oos_vfid, oid, first_chunk_recdes, first_chunk_header);
  if (err != NO_ERROR)
    {
      return err;
    }

  // check if we need to read all the chunks
  if (first_chunk_header.next_chunk_oid.slotid != NULL_SLOTID)
    {
      err = oos_read_across_pages (thread_p, oos_vfid, oid, first_chunk_header, recdes);
      recdes_free_data_area (&first_chunk_recdes);
    }
  else
    {
      recdes = first_chunk_recdes;
    }
  oos_log ("oos_read: read completed, total_size=%d\n", first_chunk_header.total_size);

  return NO_ERROR;
}

int get_recently_inserted_oos_vpid (const VFID &oos_vfid, VPID &vpid)
{
  auto it = oos_recently_inserted_oos_vpid_map.find (&oos_vfid);
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

static int oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
			       VPID &vpid_out)
{
  int err;
  PAGE_TYPE page_type = PAGE_OOS;
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid_out, nullptr);
  if (err)
    {
      return err;
    }
  printf ("oos_file_alloc_new: allocated new page {volid=%d, pageid=%d}\n",
	  vpid_out.volid, vpid_out.pageid);

  return NO_ERROR;
}


int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  printf ("oos_find_best_page: looking for page for record length=%d\n", rec_length);
  int err = 0;
  PAGE_TYPE page_type = PAGE_OOS;

  VPID recently_inserted_oos_vpid = VPID_INITIALIZER;
  auto it = oos_recently_inserted_oos_vpid_map.find (&oos_vfid);
  if (it == oos_recently_inserted_oos_vpid_map.end ())
    {
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  recently_inserted_oos_vpid = it->second;
  if (recently_inserted_oos_vpid.pageid == NULL_PAGEID)
    {
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  // try the recently inserted page first
  PAGE_PTR raw_ptr = pgbuf_fix (thread_p, &recently_inserted_oos_vpid, OLD_PAGE_IF_IN_BUFFER,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (raw_ptr == nullptr)
    {
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }
  auto_unfixed_page_ptr page_ptr { raw_ptr, page_auto_unfix {thread_p} };

  auto [pageid, volid] = recently_inserted_oos_vpid;
  int freespace = spage_get_free_space (thread_p, page_ptr.get());
  printf ("oos_find_best_page: recently inserted page {volid=%d, pageid=%d} has freespace=%d\n",
	  volid, pageid, freespace);

  if (freespace >= rec_length + (int)sizeof (SPAGE_SLOT))
    {
      printf ("oos_find_best_page: reusing recently inserted page {volid=%d, pageid=%d} with freespace=%d\n",
	      recently_inserted_oos_vpid.volid, recently_inserted_oos_vpid.pageid, freespace);
      vpid = recently_inserted_oos_vpid;
      return NO_ERROR;
    }
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
  spage_initialize (thread_p, page, ANCHORED_DONT_REUSE_SLOTS, MAX_ALIGNMENT, false);
  return err;
}

