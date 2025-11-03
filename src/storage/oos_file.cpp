#include "oos_file.hpp"
#include "file_manager.h"

[[nodiscard]] OosCreateResult
oos_create (const THREAD_ENTRY *thread_p, const HFID &hfid)
{
  printf ("oos_create called\n");
  fflush (stdout);

  // TODO: check if it is already created


  FILE_DESCRIPTORS des;
  VFID oos_vfid;
  if (file_create_with_npages (thread_p, FILE_OOS, 1, &des, &oos_vfid) != NO_ERROR)
    {
      return OosCreateResult{1, {}};
    }


  // TODO: default fail
  OosCreateResult result{};

  result.error_code = 1;
  return result;

}

int oos_destroy (const THREAD_ENTRY *thread_p, const HFID &hfid)
{
  return 1;
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

