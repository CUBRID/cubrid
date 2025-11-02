#include "oos_file.hpp"
#include <optional>

[[nodiscard]] OosInitResult
oos_init (const THREAD_ENTRY &thread_entry, const HFID &hfid)
{
  printf ("oos_init called\n");
  fflush (stdout);
  OosInitResult result{};
  result.error_code = 0;
  return result;
}

[[nodiscard]] OosInsertResult
oos_insert (const THREAD_ENTRY &thread_entry, const std::vector<DB_VALUE *> &db_values)
{
  return OosInsertResult{0, {}};
}

[[nodiscard]] int
oos_get (const OID &oid, const RECDES &recdes)
{
  return 0;
}


