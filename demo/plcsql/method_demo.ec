#include "dbi.h"
#include "dbtype_def.h"

void exec_int (DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *val)
{
  int res = -1;

  if (val != NULL) {
        res = db_get_int (val);
  }

end:
  db_make_int (rtn, res);
}
