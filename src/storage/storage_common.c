/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * storage_common.c - Definitions and data types of disk related stuffs
 *                    such as pages, file structures, and so on.
 */

#ident "$Id$"

#include <stdlib.h>
#include <assert.h>

#include "config.h"

#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "file_io.h"
#include "tz_support.h"
#include "db_date.h"
#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* RESERVED_SIZE_IN_PAGE should be aligned */
#define RESERVED_SIZE_IN_PAGE   (sizeof (FILEIO_PAGE_RESERVED) + sizeof (FILEIO_PAGE_WATERMARK))

PGLENGTH db_Io_page_size = IO_DEFAULT_PAGE_SIZE;
PGLENGTH db_Log_page_size = IO_DEFAULT_PAGE_SIZE;
PGLENGTH db_User_page_size = IO_DEFAULT_PAGE_SIZE - RESERVED_SIZE_IN_PAGE;

static PGLENGTH find_valid_page_size (PGLENGTH page_size);

/*
 * db_set_page_size(): set the page size of system.
 *
 *   returns: NO_ERROR if page size is set by given size, otherwise ER_FAILED
 *   io_page_size(IN): the IO page size
 *   log_page_size(IN): the LOG page size
 *
 * Note: Set the database page size to the given size. The given size
 *       must be power of 2, greater than or equal to 1K, and smaller
 *       than or equal to 16K.
 */
int
db_set_page_size (PGLENGTH io_page_size, PGLENGTH log_page_size)
{
  assert (io_page_size >= IO_MIN_PAGE_SIZE && log_page_size >= IO_MIN_PAGE_SIZE);

  if (io_page_size < IO_MIN_PAGE_SIZE || log_page_size < IO_MIN_PAGE_SIZE)
    {
      return ER_FAILED;
    }

  db_Io_page_size = find_valid_page_size (io_page_size);
  db_User_page_size = db_Io_page_size - RESERVED_SIZE_IN_PAGE;
  db_Log_page_size = find_valid_page_size (log_page_size);

  if (db_Io_page_size != io_page_size || db_Log_page_size != log_page_size)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * db_network_page_size(): find the network pagesize
 *
 *   returns: network pagesize
 *
 * Note: Find the best network pagesize for C/S communications for
 *       given transaction/client.
 */
PGLENGTH
db_network_page_size (void)
{
  return db_Io_page_size;
}

/*
 * find_valid_page_size(): find the valid page size of system
 *
 *   returns: page_size
 *   page_size(IN): the page size
 *
 * Note: Find the database pagesize with the given size, where the given size
 *       must be power of 2, greater than or equal to 1K, and smaller than or
 *       equal to 16K.
 */
static PGLENGTH
find_valid_page_size (PGLENGTH page_size)
{
  PGLENGTH power2_page_size = page_size;

  if (power2_page_size < IO_MIN_PAGE_SIZE)
    {
      power2_page_size = IO_MIN_PAGE_SIZE;
    }
  else if (power2_page_size > IO_MAX_PAGE_SIZE)
    {
      power2_page_size = IO_MAX_PAGE_SIZE;
    }
  else
    {
      if (!IS_POWER_OF_2 (power2_page_size))
	{
	  /*
	   * Not a power of 2 or page size is too small
	   *
	   * Round the number to a power of two. Find smaller number that it is
	   * a power of two, and then shift to get larger number.
	   */
	  while (!IS_POWER_OF_2 (power2_page_size))
	    {
	      if (power2_page_size < IO_MIN_PAGE_SIZE)
		{
		  power2_page_size = IO_MIN_PAGE_SIZE;
		  break;
		}
	      else
		{
		  /* Turn off some bits but the left most one */
		  power2_page_size = power2_page_size & (power2_page_size - 1);
		}
	    }

	  power2_page_size <<= 1;

	  if (power2_page_size < IO_MIN_PAGE_SIZE)
	    {
	      power2_page_size = IO_MIN_PAGE_SIZE;
	    }
	  else if (power2_page_size > IO_MAX_PAGE_SIZE)
	    {
	      power2_page_size = IO_MAX_PAGE_SIZE;
	    }

	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DTSR_BAD_PAGESIZE, 2, page_size, power2_page_size);
	}
    }

  return power2_page_size;
}

void
db_print_data (DB_TYPE type, DB_DATA * data, FILE * fd)
{
  int hour, minute, second, millisecond, month, day, year;

  switch (type)
    {
    case DB_TYPE_SHORT:
      fprintf (fd, "%d", data->sh);
      break;

    case DB_TYPE_INTEGER:
      fprintf (fd, "%d", data->i);
      break;

    case DB_TYPE_BIGINT:
      fprintf (fd, "%lld", (long long) data->bigint);
      break;

    case DB_TYPE_FLOAT:
      fprintf (fd, "%f", data->f);
      break;

    case DB_TYPE_DOUBLE:
      fprintf (fd, "%f", data->d);
      break;

    case DB_TYPE_DATE:
      db_date_decode (&data->date, &month, &day, &year);
      fprintf (fd, "%d / %d / %d", month, day, year);
      break;

    case DB_TYPE_TIME:
      db_time_decode (&data->time, &hour, &minute, &second);
      fprintf (fd, "%d:%d:%d", hour, minute, second);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      fprintf (fd, "%d", data->utime);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      fprintf (fd, "%d Z:%X", data->timestamptz.timestamp, data->timestamptz.tz_id);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      db_datetime_decode (&data->datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
      fprintf (fd, "%d/%d/%d %d:%d:%d.%d", month, day, year, hour, minute, second, millisecond);
      break;

    case DB_TYPE_DATETIMETZ:
      db_datetime_decode (&(data->datetimetz.datetime), &month, &day, &year, &hour, &minute, &second, &millisecond);
      fprintf (fd, "%d/%d/%d %d:%d:%d.%d Z:%X", month, day, year, hour, minute, second, millisecond,
	       data->datetimetz.tz_id);
      break;

    case DB_TYPE_MONETARY:
      fprintf (fd, "%f", data->money.amount);
      switch (data->money.type)
	{
	case DB_CURRENCY_DOLLAR:
	  fprintf (fd, " dollars");
	  break;
	case DB_CURRENCY_YEN:
	  fprintf (fd, " yens");
	  break;
	case DB_CURRENCY_WON:
	  fprintf (fd, " wons");
	  break;
	case DB_CURRENCY_TL:
	  fprintf (fd, " turkish lira");
	  break;
	case DB_CURRENCY_BRITISH_POUND:
	  fprintf (fd, " pounds");
	  break;
	case DB_CURRENCY_CAMBODIAN_RIEL:
	  fprintf (fd, " riels");
	  break;
	case DB_CURRENCY_CHINESE_RENMINBI:
	  fprintf (fd, " renminbi");
	  break;
	case DB_CURRENCY_INDIAN_RUPEE:
	  fprintf (fd, " rupees");
	  break;
	case DB_CURRENCY_RUSSIAN_RUBLE:
	  fprintf (fd, " rubles");
	  break;
	case DB_CURRENCY_AUSTRALIAN_DOLLAR:
	  fprintf (fd, " Australian dollars");
	  break;
	case DB_CURRENCY_CANADIAN_DOLLAR:
	  fprintf (fd, " Canadian dollars");
	  break;
	case DB_CURRENCY_BRASILIAN_REAL:
	  fprintf (fd, " reals");
	  break;
	case DB_CURRENCY_ROMANIAN_LEU:
	  fprintf (fd, " lei");
	  break;
	case DB_CURRENCY_EURO:
	  fprintf (fd, " euros");
	  break;
	case DB_CURRENCY_SWISS_FRANC:
	  fprintf (fd, " Swiss francs");
	  break;
	case DB_CURRENCY_DANISH_KRONE:
	  fprintf (fd, " Danish crowns");
	  break;
	case DB_CURRENCY_NORWEGIAN_KRONE:
	  fprintf (fd, " Norwegian crowns");
	  break;
	case DB_CURRENCY_BULGARIAN_LEV:
	  fprintf (fd, " levs");
	  break;
	case DB_CURRENCY_VIETNAMESE_DONG:
	  fprintf (fd, " Vietnamese dongs");
	  break;
	case DB_CURRENCY_CZECH_KORUNA:
	  fprintf (fd, " Czech crowns");
	  break;
	case DB_CURRENCY_POLISH_ZLOTY:
	  fprintf (fd, " zloty");
	  break;
	case DB_CURRENCY_SWEDISH_KRONA:
	  fprintf (fd, " Swedish crowns");
	  break;
	case DB_CURRENCY_CROATIAN_KUNA:
	  fprintf (fd, " kunas");
	  break;
	case DB_CURRENCY_SERBIAN_DINAR:
	  fprintf (fd, " dinars");
	  break;
	default:
	  break;
	}
      break;

    default:
      fprintf (fd, "Undefined");
      break;
    }
}

int
recdes_allocate_data_area (RECDES * rec, int size)
{
  char *data;

  data = (char *) db_private_alloc (NULL, size);
  if (data == NULL)
    {
      return ER_FAILED;
    }

  rec->data = data;
  rec->area_size = size;

  return NO_ERROR;
}

void
recdes_free_data_area (RECDES * rec)
{
  db_private_free_and_init (NULL, rec->data);
}

void
recdes_set_data_area (RECDES * rec, char *data, int size)
{
  rec->data = data;
  rec->area_size = size;
}

char *
oid_to_string (char *buf, int buf_size, OID * oid)
{
  snprintf (buf, buf_size, "(%d|%d|%d)", oid->volid, oid->pageid, oid->slotid);
  buf[buf_size - 1] = 0;
  return buf;
}

char *
vpid_to_string (char *buf, int buf_size, VPID * vpid)
{
  snprintf (buf, buf_size, "(%d|%d)", vpid->volid, vpid->pageid);
  buf[buf_size - 1] = 0;
  return buf;
}

char *
vfid_to_string (char *buf, int buf_size, VFID * vfid)
{
  snprintf (buf, buf_size, "(%d|%d)", vfid->volid, vfid->fileid);
  buf[buf_size - 1] = 0;
  return buf;
}

char *
hfid_to_string (char *buf, int buf_size, HFID * hfid)
{
  snprintf (buf, buf_size, "(%d|%d|%d)", hfid->vfid.volid, hfid->vfid.fileid, hfid->hpgid);
  buf[buf_size - 1] = 0;
  return buf;
}

char *
btid_to_string (char *buf, int buf_size, BTID * btid)
{
  snprintf (buf, buf_size, "(%d|%d|%d)", btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);
  buf[buf_size - 1] = 0;
  return buf;
}
