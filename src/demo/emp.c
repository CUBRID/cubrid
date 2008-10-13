/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * emp.c
 */

/******************      SYSTEM INCLUDE FILES      ***************************/
#include <string.h>

/******************     PRIVATE INCLUDE FILES      ***************************/

/******************     IMPORTED DECLARATIONS      ***************************/
#include <dbi.h>

/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/

/******************        PRIVATE DEFINES         ***************************/

/******************        PRIVATE TYPEDEFS        ***************************/

/******************      PRIVATE DECLARATIONS      ***************************/

/******************           FUNCTIONS            ***************************/
void
employee_rating (DB_OBJECT * employee, DB_VALUE * return_value)
{
  static char buf[512];
  DB_VALUE salary;
  int error;
  double pay;

  if (error = db_get (employee, "salary", &salary))
    {
      sprintf (buf, "Can not compute employee rating: "
	       "db_get return code=%d, %s", error, db_error_string (3));
      DB_MAKE_STRING (return_value, buf);
      return;
    }
  pay = DB_GET_MONETARY (&salary)->amount;

  if (pay > 123456.7)
    strcpy (buf, "excellent");
  else if (pay > 76543.2)
    strcpy (buf, "good");
  else if (pay > 34567.8)
    strcpy (buf, "fair");
  else
    strcpy (buf, "poor");

  DB_MAKE_STRING (return_value, buf);
}
