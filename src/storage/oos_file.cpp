#include "oos_file.hpp"

[[nodiscard]] OosCreateResult
oos_create (const THREAD_ENTRY *thread_entry, const HFID &hfid)
{
  printf ("oos_create called\n");
  fflush (stdout);
  OosCreateResult result{};
  result.error_code = 0;
  return result;
}

int oos_destroy (const THREAD_ENTRY *thread_entry, const HFID &hfid)
{
  return 0;
}

[[nodiscard]] OosInsertResult
oos_insert (const THREAD_ENTRY *thread_entry, const VFID &oos_vfid, const std::vector<RECDES> &db_values)
{
  return OosInsertResult{0, {}};
}

[[nodiscard]] int
oos_get (const THREAD_ENTRY *threnad_entry, const VFID &oos_vfid, const OID &oid, RECDES &recdes)
{
  return 0;
}

