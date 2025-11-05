#include <cassert>
#include "error_code.h"
#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);
static int oos_insert_small (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid);
static int oos_insert_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid);

struct oos_record_header
{
  int total_size;
  int chunk_count;
};
using OOS_RECORD_HEADER = struct oos_record_header;

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

static int oos_prepend_record_header (RECDES &rec_in, RECDES &rec_out)
{
  int err;
  OOS_RECORD_HEADER oos_header;
  err = recdes_allocate_data_area (&rec_out, rec_in.length + (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      return err;
    }

  oos_header.total_size = rec_in.length;
  oos_header.chunk_count = 1;
  rec_out.type = REC_HOME;
  rec_out.length = rec_in.length + (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (rec_out.data, &oos_header, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (rec_out.data + (int)sizeof (OOS_RECORD_HEADER), rec_in.data, rec_in.length);
  return err;
}

static void oos_remove_record_header (RECDES &rec_in, RECDES &rec_out)
{
  assert (rec_in.length >= (int)sizeof (OOS_RECORD_HEADER));
  rec_out.length = rec_in.length - (int)sizeof (OOS_RECORD_HEADER);
  rec_out.type = REC_HOME;
  rec_out.data = rec_in.data + (int)sizeof (OOS_RECORD_HEADER);
}

int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;

  // TODO: otherwise spage assert type <= REC_UNKNOWN fails
  assert (recdes.type == REC_HOME);

  // ugly workaronund for large records
  RECDES oos_rec;
  err = oos_prepend_record_header (recdes, oos_rec);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (oos_rec.length > spage_max_record_size () - (int)sizeof (SPAGE_SLOT))
    {
      return oos_insert_large (thread_p, oos_vfid, oos_rec, oid);
    }
  else
    {
      return oos_insert_small (thread_p, oos_vfid, oos_rec, oid);
    }
}


static int oos_insert_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  // split the recdes to multiple chunks and insert them one by one
  int chunk_size = spage_max_record_size () - (int)sizeof (SPAGE_SLOT);
  assert (recdes.length > chunk_size);

  int required_page_nums = (recdes.length + chunk_size - 1) / chunk_size;
  assert (required_page_nums > 1);

  // create a vector of RECDES that each contains chunk_size data
  std::vector<RECDES> recdes_chunks{required_page_nums};

  int bytes_remaining = recdes.length;
  int offset = 0;

  while (bytes_remaining > 0)
    {
      int current_chunk_size = std::min (chunk_size, bytes_remaining);
      RECDES chunk_recdes;
      chunk_recdes.type = REC_HOME;
      chunk_recdes.length = current_chunk_size;
      int err = recdes_allocate_data_area (&chunk_recdes, current_chunk_size);
      if (err != NO_ERROR)
	{
	  goto error;
	}

      std::memcpy (chunk_recdes.data, recdes.data + offset, current_chunk_size);
      recdes_chunks.at (recdes_chunks.size() - 1) = chunk_recdes;

      bytes_remaining -= current_chunk_size;
      offset += current_chunk_size;
    }

  printf ("oos_insert_large: split record into %zu chunks\n", recdes_chunks.size());


  for (auto &&rec: recdes_chunks)
    {
      OID chunk_oid;
      // rec
      printf ("oos_insert_large: inserting chunk of size %d\n", rec.length);
      int err = oos_insert_small (thread_p, oos_vfid, rec, chunk_oid);
      if (err)
	{
	  // TODO: free previously allocated chunks
	  return err;
	}
    }

  return NO_ERROR;

error:
  for (auto &&rec: recdes_chunks)
    {
      recdes_free_data_area (&rec);
    }

  return ER_FAILED;
}

static int oos_insert_small (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;
  err = oos_find_best_page (thread_p, oos_vfid, recdes.length, vpid);
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

  PGSLOTID slotid = -1;
  int sp_status = spage_insert (thread_p, page_ptr, &recdes, &slotid);
  if (sp_status != SP_SUCCESS)
    {
      printf ("oos_insert_small: spage_insert failed with status %d\n", sp_status);
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return ER_FAILED;
    }

  oid.pageid = vpid.pageid;
  oid.slotid = slotid;
  oid.volid = vpid.volid;

  pgbuf_unfix_and_init (thread_p, page_ptr);

  return NO_ERROR;

}

int
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
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

  oos_remove_record_header (recdes_with_oos_header, recdes);

  pgbuf_unfix_and_init (thread_p, page_ptr);
  return 0;
}

int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  int err = 0;
  PAGE_TYPE page_type = PAGE_OOS;
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid, nullptr);
  if (err)
    {
      return err;
    }

  return 0;
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

