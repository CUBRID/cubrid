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
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

VPID recently_inserted_oos_vpid = VPID_INITIALIZER;

int
oos_create (THREAD_ENTRY *thread_p, VFID &oos_vfid)
{
  // TODO: check if it is already created
  // with something like hfid or vfid

  FILE_DESCRIPTORS des;

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
  int err;
  err = recdes_allocate_data_area (&rec_out, rec_in.length + (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      return err;
    }

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
  assert (rec_out.area_size >= rec_in.length - (int)sizeof (OOS_RECORD_HEADER));

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


  const int max_possible_chunk_size = spage_max_record_size () - (int)sizeof (SPAGE_SLOT) - (int)sizeof (
      OOS_RECORD_HEADER);
  if (recdes.length > max_possible_chunk_size)
    {
      return oos_insert_large (thread_p, oos_vfid, recdes, oid);
    }
  else
    {
      const OOS_RECORD_HEADER header{recdes.length, 0, OID_INITIALIZER};
      return oos_insert_small (thread_p, oos_vfid, recdes, header, oid);
    }
}


static int oos_insert_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;

  // split the recdes to multiple chunks and insert them one by one
  int chunk_size = spage_max_record_size () - (int)sizeof (SPAGE_SLOT) - (int)sizeof (OOS_RECORD_HEADER);
  assert (recdes.length > chunk_size);

  int required_page_nums = (recdes.length + chunk_size - 1) / chunk_size;
  assert (required_page_nums > 1);

  oos_log ("oos_insert_large: required_page_nums=%d\n", required_page_nums);

  const int total_size = recdes.length;

  OID prev_chunk_oid = OID_INITIALIZER;
  for (int i = required_page_nums - 1; i >= 0; --i)
    {

      RECDES chunk_recdes{};
      chunk_recdes.type = REC_HOME;
      chunk_recdes.length = std::min (chunk_size, total_size - i * chunk_size);
      chunk_recdes.data = recdes.data + i * chunk_size;

      // prev_chunk_oid is updated to point at the newly inserted chunk, so that we can copy it to header inside this loop.
      OOS_RECORD_HEADER header{total_size, i, prev_chunk_oid};
      oos_log ("insert_large: header = {total_size=%d, chunk_index=%d, next_chunk_oid={pageid=%d, slotid=%d}}\n",
	       header.total_size, header.chunk_index,
	       header.next_chunk_oid.pageid, header.next_chunk_oid.slotid);
      err = oos_insert_small (thread_p, oos_vfid, chunk_recdes, header, prev_chunk_oid);
      if (err != NO_ERROR)
	{
	  break;
	}
    }

  // update the out parameter 'oid' to give access to the first slot
  oid = prev_chunk_oid;
  return err;
}

static int oos_insert_small (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			     const OOS_RECORD_HEADER &header,
			     OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;

  int required_length = recdes.length + (int)sizeof (OOS_RECORD_HEADER);

  // data payload + oos header must be smaller than or equal to max record size - slot size
  assert (required_length <= spage_max_record_size() - (int)sizeof (SPAGE_SLOT));

  err = oos_find_best_page (thread_p, oos_vfid, required_length, vpid);
  if (err != NO_ERROR)
    {
      return err;
    }

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      assert (false);
      return ER_FAILED;
    }

  RECDES oos_rec{};
  err = oos_prepend_record_header (recdes, header, oos_rec);
  if (err != NO_ERROR)
    {
      err = ER_FAILED;
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return err;
    }

  PGSLOTID slotid = NULL_SLOTID;
  int sp_status = spage_insert (thread_p, page_ptr, &oos_rec, &slotid);
  if (sp_status != SP_SUCCESS)
    {
      oos_log ("oos_insert_small: spage_insert failed with status %d\n", sp_status);
      err = ER_FAILED;
      goto cleanup;
    }
  assert (slotid != NULL_SLOTID);

  recently_inserted_oos_vpid = vpid;

  oid.pageid = vpid.pageid;
  oid.slotid = slotid;
  oid.volid = vpid.volid;

cleanup:
  recdes_free_data_area (&oos_rec);
  pgbuf_unfix_and_init (thread_p, page_ptr);
  return err;
}

static int oos_read_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
			   const OOS_RECORD_HEADER &first_chunk_header, RECDES &recdes)
{
  int err = NO_ERROR;
  auto header = first_chunk_header;
  int total_size = first_chunk_header.total_size;
  assert (first_chunk_header.chunk_index == 0);
  assert (total_size > spage_max_record_size () - (int)sizeof (SPAGE_SLOT) - (int)sizeof (OOS_RECORD_HEADER));

  oos_log ("oos_read_large: total_size=%d\n", total_size);

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
  while (current_chunk_oid.pageid != NULL_PAGEID)
    {
      OOS_RECORD_HEADER header;
      RECDES chunk_recdes;
      int err = oos_read_small (thread_p, oos_vfid, current_chunk_oid, chunk_recdes, header);
      if (err != NO_ERROR)
	{
	  return err;
	}

      oos_log ("read_large: header = {total_size=%d, chunk_index=%d, next_chunk_oid={pageid=%d, slotid=%d}}\n",
	       header.total_size,
	       header.chunk_index, header.next_chunk_oid.pageid, header.next_chunk_oid.slotid);

      std::memcpy (buf, chunk_recdes.data, chunk_recdes.length);
      buf += chunk_recdes.length;
      oos_log ("oos_read_large: read chunk index=%d, chunk_size=%d\n", idx, chunk_recdes.length);
      current_chunk_oid = header.next_chunk_oid;
      recdes_free_data_area (&chunk_recdes);
    }
  assert (buf == header.total_size + recdes.data);

  return err;
}

static int oos_read_small (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
			   OOS_RECORD_HEADER &header_out)
{
  int err = NO_ERROR;
  const auto [pageid, slotid, volid] = oid;
  auto vpid = VPID{pageid, volid};

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return ER_FAILED;
    }

  RECDES recdes_with_oos_header;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, slotid, &recdes_with_oos_header, PEEK);
  if (code != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return ER_FAILED;
    }

  recdes_allocate_data_area (&recdes, recdes_with_oos_header.length - sizeof (OOS_RECORD_HEADER));
  recdes.length = recdes_with_oos_header.length - sizeof (OOS_RECORD_HEADER);
  oos_pop_record_header (recdes_with_oos_header, header_out, recdes);
  pgbuf_unfix_and_init (thread_p, page_ptr);
  return err;
}

int
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  int err = NO_ERROR;

  // try reading just one slot
  OOS_RECORD_HEADER first_chunk_header;
  RECDES first_chunk_recdes;
  err = oos_read_small (thread_p, oos_vfid, oid, first_chunk_recdes, first_chunk_header);
  if (err != NO_ERROR)
    {
      return err;
    }

  // check if we need to read all the chunks
  if (first_chunk_header.next_chunk_oid.slotid != NULL_SLOTID)
    {
      err = oos_read_large (thread_p, oos_vfid, oid, first_chunk_header, recdes);
      recdes_free_data_area (&first_chunk_recdes);
    }
  else
    {
      recdes = first_chunk_recdes;
    }
  oos_log ("oos_read: read completed, total_size=%d\n", first_chunk_header.total_size);

  return err;
}

int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  int err = 0;
  PAGE_TYPE page_type = PAGE_OOS;

  if (recently_inserted_oos_vpid.pageid != NULL_PAGEID)
    {
      // try the recently inserted page first
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &recently_inserted_oos_vpid, OLD_PAGE_IF_IN_BUFFER,
				     PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == nullptr)
	{
	  goto newalloc;
	}

      int freespace = spage_get_free_space (thread_p, page_ptr);

      pgbuf_unfix_and_init (thread_p, page_ptr);
      if (freespace >= rec_length + (int)sizeof (SPAGE_SLOT))
	{
	  oos_log ("oos_find_best_page: reusing recently inserted page {volid=%d, pageid=%d} with freespace=%d\n",
		   recently_inserted_oos_vpid.volid, recently_inserted_oos_vpid.pageid, freespace);
	  vpid = recently_inserted_oos_vpid;
	  return NO_ERROR;
	}
    }

newalloc:
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid, nullptr);
  if (err)
    {
      return err;
    }

  return NO_ERROR;
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

