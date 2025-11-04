#include "oos_file.hpp"
#include "error_code.h"
#include "file_manager.h"

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

[[nodiscard]]
int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const RECDES &recdes, OID &oid)
{
  return 1;
}

int
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  return 1;
}

int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  int err = 0;
  PAGE_TYPE page_type = PAGE_OOS;
  err = file_alloc (thread_p, &oos_vfid, file_init_page_type, &page_type, &vpid, nullptr);
  if (err)
    {
      return err;
    }

  return 0;
}

