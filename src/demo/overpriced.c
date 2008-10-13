/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * overpriced.c
 */

/******************      SYSTEM INCLUDE FILES      ***************************/


/******************     PRIVATE INCLUDE FILES      ***************************/

#include "dbi.h"

/******************     IMPORTED DECLARATIONS      ***************************/


/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/


/******************        PRIVATE DEFINES         ***************************/

#define DECENT_PRICE 125.00
#define COST_ATTRIBUTE "cost"

/******************        PRIVATE TYPEDEFS        ***************************/


/******************      PRIVATE DECLARATIONS      ***************************/


/******************           FUNCTIONS            ***************************/

void
accommodations_overpriced (DB_OBJECT * self, DB_VALUE * return_value)
{
  DB_VALUE price;
  int error;

  DB_MAKE_INT (return_value,
	       (db_get (self, COST_ATTRIBUTE, &price) == NO_ERROR)
	       ? (DB_GET_MONETARY (&price)->amount > DECENT_PRICE) : -1);
}
