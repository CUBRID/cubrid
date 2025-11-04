#include "error_code.h"
#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);

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

int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{

  // TODO: otherwise spage assert type <= REC_UNKNOWN fails
  assert (recdes.type == REC_HOME);

  int err = NO_ERROR;
  VPID vpid;
  err = oos_find_best_page (thread_p, oos_vfid, recdes.length, vpid);
  if (err != NO_ERROR)
    {
      return err;
    }

  // TODO: why OLD_PAGE_IF_IN_BUFFER?
  // TODO: why PGBUF_LATCH_WRITE?
  // TODO: why PGBUF_UNCONDITIONAL_LATCH?
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return ER_FAILED;
    }

  PGSLOTID slotid = -1;
  int sp_status = spage_insert (thread_p, page_ptr, &recdes, &slotid);
  if (sp_status != SP_SUCCESS)
    {
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

  SCAN_CODE code = spage_get_record (thread_p, page_ptr, slotid, &recdes, PEEK);
  if (code != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return ER_FAILED;
    }

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

