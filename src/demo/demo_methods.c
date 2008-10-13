/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * demo_methods.c
 */

/******************      SYSTEM INCLUDE FILES      ***************************/


/******************     PRIVATE INCLUDE FILES      ***************************/

#include <dbi.h>

/******************     IMPORTED DECLARATIONS      ***************************/


/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/


/******************        PRIVATE DEFINES         ***************************/

#define LAT_DIST 70.00
#define LAT_ATT "latitude"

/******************        PRIVATE TYPEDEFS        ***************************/


/******************      PRIVATE DECLARATIONS      ***************************/


/******************           FUNCTIONS            ***************************/

void
location_find_lodging_named (DB_OBJECT * invoked_from,
			     DB_VALUE * return_value, DB_VALUE * parm)
{
  char buf[128];
  DB_VALUE name;
  int error;
  DB_OBJLIST *optr;
  DB_OBJECT *class;

  DB_MAKE_NULL (return_value);
  class = (db_find_class ("location"));
  optr = db_get_all_objects (class);

  while (optr)
    {
      db_get (optr->op, "lodging", &name);
      if (!strcmp (DB_GET_STRING (&name), DB_GET_STRING (parm)))
	{
	  DB_MAKE_OBJECT (return_value, optr->op);
	  return;
	}
      optr = optr->next;
    }
  DB_MAKE_NULL (return_value);
  return;
}

void
location_distance_from_equator (DB_OBJECT * invoked_from,
				DB_VALUE * return_value)
{

  static char buf[128];
  DB_VALUE ourlat;
  int error;

  if (error = db_get (invoked_from, LAT_ATT, &ourlat))
    {
      sprintf (buf, "Can't get the latitude of this object");
      DB_MAKE_STRING (return_value, buf);
      return;
    }

  sprintf (buf, "Approximately %f miles from the equator.",
	   DB_GET_FLOAT (&ourlat) * LAT_DIST);
  DB_MAKE_STRING (return_value, buf);
  return;
}
