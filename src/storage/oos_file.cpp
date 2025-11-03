#include "oos_file.hpp"
#include "error_code.h"
#include "file_manager.h"

int
oos_create (THREAD_ENTRY *thread_p, const HFID &hfid, VFID &oos_vfid)
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
  return 1;
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

