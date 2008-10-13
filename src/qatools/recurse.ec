/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * recurse.ec
 */


/******************      SYSTEM INCLUDE FILES      ***************************/


/******************     PRIVATE INCLUDE FILES      ***************************/

#include <dbi.h>
#include <string.h>
#include <stdlib.h>

/******************     IMPORTED DECLARATIONS      ***************************/


/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/


/******************        PRIVATE DEFINES         ***************************/

/******************        PRIVATE TYPEDEFS        ***************************/


/******************      PRIVATE DECLARATIONS      ***************************/


/******************           FUNCTIONS            ***************************/

void
x_recurse (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * arg0)
{
  exec sqlx begin declare section;
  int n;
  exec sqlx end declare section;

  n = DB_GET_INT (arg0);
  if (n > 1)
    {
      exec sqlx select sum (x_recurse (x2,:n - 1)) into:n from x2;
    }

  DB_MAKE_INT (result, n);
}



void
esqlx_add_int (DB_OBJECT * invoked_on, DB_VALUE * return_value, DB_VALUE * p1)
{
  exec sqlx begin declare section;
  int xint;
  DB_OBJECT *obj = invoked_on;
  exec sqlx end declare section;

exec sqlx select xint into:xint from x2 where x2 =:obj;

  DB_MAKE_INTEGER (return_value, DB_GET_INTEGER (p1) + xint);
}


void
esqlx_bad_method_on_x (DB_OBJECT * invoked_on, DB_VALUE * return_value)
{
  exec sqlx begin declare section;
  DB_OBJECT *obj = invoked_on;
  exec sqlx end declare section;

  exec sqlx update object:obj set xint = xint + 1;

  exec sqlx commit work;

  DB_MAKE_INTEGER (return_value, 1);
}
