#include <cassert>
#include "error_code.h"
#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"

struct oos_record_header
{
  int total_size;
  int chunk_index;
  OID next_chunk_oid;
};
using OOS_RECORD_HEADER = struct oos_record_header;

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);
static int oos_insert_small (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			     const OOS_RECORD_HEADER &header, OID &oid);
static int oos_insert_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			     const OOS_RECORD_HEADER &header, OID &oid);
static int
oos_read_internal (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
		   OOS_RECORD_HEADER &out_header);

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
  std::memcpy (&header_out, rec_in.data, (int)sizeof (OOS_RECORD_HEADER));

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

  if (err != NO_ERROR)
    {
      return err;
    }

  const OOS_RECORD_HEADER header{recdes.length, 0, OID_INITIALIZER};

  if (recdes.length > spage_max_record_size () - (int)sizeof (SPAGE_SLOT))
    {
      return oos_insert_large (thread_p, oos_vfid, recdes, header, oid);
    }
  else
    {
      return oos_insert_small (thread_p, oos_vfid, recdes, header, oid);
    }
}


static int oos_insert_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			     const OOS_RECORD_HEADER &header, OID &oid)
{
  int err = NO_ERROR;

  // split the recdes to multiple chunks and insert them one by one
  int chunk_size = spage_max_record_size () - (int)sizeof (SPAGE_SLOT);
  assert (recdes.length > chunk_size);

  int payload_size = chunk_size - (int)sizeof (OOS_RECORD_HEADER);

  int required_page_nums = (recdes.length + payload_size - 1) / payload_size;
  assert (required_page_nums > 1);

  printf ("oos_insert_large: required_page_nums=%d\n", required_page_nums);

  RECDES chunk_recdes{};
  chunk_recdes.type = REC_HOME;

  OID prev_chunk_oid = OID_INITIALIZER;
  for (int i = required_page_nums - 1; i >= 0; --i)
    {
      int current_chunk_size = std::min (payload_size, recdes.length - i * payload_size);

      OOS_RECORD_HEADER header{recdes.length, i, prev_chunk_oid};
      recdes_set_data_area (&chunk_recdes, recdes.data + i * payload_size, current_chunk_size);
      chunk_recdes.length = current_chunk_size;

      // prev_chunk_oid is updated to point at the newly inserted chunk, so that we can copy it to header inside this loop.
      err = oos_insert_small (thread_p, oos_vfid, chunk_recdes, header, prev_chunk_oid);
      if (err != NO_ERROR)
	{
	  break;
	}
    }

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
      printf ("oos_insert_small: spage_insert failed with status %d\n", sp_status);
      err = ER_FAILED;
      goto cleanup;
    }
  assert (slotid != NULL_SLOTID);

  oid.pageid = vpid.pageid;
  oid.slotid = slotid;
  oid.volid = vpid.volid;

cleanup:
  recdes_free_data_area (&oos_rec);
  pgbuf_unfix_and_init (thread_p, page_ptr);
  return err;
}

static int oos_read_large (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
			   const OOS_RECORD_HEADER &header)
{
  int err = NO_ERROR;
  int total_size = header.total_size;
  assert (header.chunk_index == 0);
  assert (total_size > spage_max_record_size () - (int)sizeof (SPAGE_SLOT));

  RECDES full_recdes{};
  err = recdes_allocate_data_area (&full_recdes, total_size);
  if (err != NO_ERROR)
    {
      return err;
    }

  int idx = 0;
  OID current_chunk_oid = oid;
  char *buf = recdes.data;
  while (current_chunk_oid.pageid != NULL_PAGEID)
    {
      RECDES chunk_recdes{};
      OOS_RECORD_HEADER header;
      int err = oos_read_internal (thread_p, oos_vfid, current_chunk_oid, chunk_recdes, header);
      if (err != NO_ERROR)
	{
	  recdes_free_data_area (&full_recdes);
	  return err;
	}

      std::memcpy (buf, chunk_recdes.data, chunk_recdes.length);
      buf += chunk_recdes.length;
      current_chunk_oid = header.next_chunk_oid;
    }
  assert (buf == header.total_size + recdes.data);

  recdes.length = total_size;
  recdes.type = REC_HOME;
  recdes.data = full_recdes.data;

  return err;
}

int oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  OOS_RECORD_HEADER header;
  return oos_read_internal (thread_p, oos_vfid, oid, recdes, header);
}

static int
oos_read_internal (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
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

  oos_pop_record_header (recdes_with_oos_header, header_out, recdes);

  if (header_out.total_size > recdes.length)
    {
      err = oos_read_large (thread_p, oos_vfid, oid, recdes, header_out);
    }

  pgbuf_unfix_and_init (thread_p, page_ptr);
  return err;
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

