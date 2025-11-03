#include "oos_file.hpp"
#include "error_code.h"
#include "file_manager.h"
#include "heap_file.h"

[[nodiscard]] OosCreateResult
oos_create (THREAD_ENTRY *thread_p, const HFID &hfid)
{
  // TODO: check if it is already created
  // with something like hfid or vfid

  FILE_DESCRIPTORS des;
  VFID oos_vfid;

  int err = file_create_with_npages (thread_p, FILE_OOS, 1, &des, &oos_vfid);
  if (err != NO_ERROR)
    {
      return OosCreateResult{err, {}};
    }

  return OosCreateResult{0, oos_vfid};
}

int oos_destroy (THREAD_ENTRY *thread_p, const HFID &hfid)
{
  return 1;
}

int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  int ret = file_destroy (thread_p, &oos_vfid, false);
  return ret;
}

[[nodiscard]] OosCreateResult oos_create (THREAD_ENTRY *thread_p)
{
  FILE_DESCRIPTORS des;
  VFID oos_vfid;
  if (file_create_with_npages (thread_p, FILE_OOS, 1, &des, &oos_vfid) != NO_ERROR)
    {
      return OosCreateResult{1, {}};
    }
  return OosCreateResult{0, oos_vfid};
}

[[nodiscard]] OosInsertResult
oos_insert (const THREAD_ENTRY *thread_p, const VFID &oos_vfid, const std::vector<RECDES> &db_values)
{



  return OosInsertResult{1, {}};
}

[[nodiscard]] int
oos_get (const THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  return 1;
}

