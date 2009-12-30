/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * glo_class.c - Glo method source file
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "util_func.h"
#include "db.h"
#include "work_space.h"
#include "glo_class.h"
#include "elo_holder.h"
#include "elo_recovery.h"
#include "elo_class.h"
#include "object_accessor.h"
#include "schema_manager.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "locator_cl.h"
#include "authenticate.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#if defined(WINDOWS)
#include "porting.h"		/* use the IO compatibility package */
#endif

#define UNIT_SIZE_DEFAULT 8	/* default 8 bits per unit or 1 byte         */
#define POSITION_DEFAULT 0	/* default current position is 0             */
#define HEADER_SIZE_DEFAULT 0	/* default is no header information          */
#define SEARCH_BUFFER_SIZE 4096	/* size of buffer used for search            */
#define BASE_BYTE 8		/* byte size in bits                         */
#define PAD_SIZE 128		/* size of "zeros" array for blank padding   */
#define PROP_GLO_POSITION 100	/* property list id for the position att     */

static int mm_Error = 0;

static int esm_pad_overflow (DB_OBJECT * dest_obj, const int fd,
			     const int bytes_written, int in_unit_size,
			     int out_unit_size);
static void esm_Glo_migrate (DB_OBJECT * source, DB_OBJECT * dest_obj,
			     DB_VALUE * return_argument_p);
static int esm_compare_glo_pathname (DB_OBJECT * holder_p, DB_ELO * glo_p);
static INT64 update_position (DB_OBJECT * glo_object_p, INT64 position);
static int destroy_glo_and_holder_and_name (DB_OBJECT * esm_glo_object_p,
					    DB_OBJECT * holder_p);
static int get_write_lock (DB_OBJECT * glo_instance_p);
static INT64 get_position (DB_OBJECT * glo_object_p);
static void esm_search (DB_OBJECT * esm_glo_object_p,
			DB_VALUE * return_argument_p,
			DB_VALUE * search_for_object_p, int search_type,
			int search_length);

/*
 * esm_set_error() - set error
 *      return: none
 *  error(in) : error code
 */

void
esm_set_error (const int error)
{
  mm_Error = error;
}

/*
 * compare_glo_pathname() -
 *      return: true/false
 *  holder_p(in) : the holder object
 *  glo_p(in) :
 */

static int
esm_compare_glo_pathname (DB_OBJECT * holder_p, DB_ELO * glo_p)
{
  const char *pathname_from_holder;
  const char *pathname_from_glo;
  DB_OBJECT *name_object_p;
  DB_VALUE value;

  if (esm_shadow_file_exists (holder_p))
    {
      return (true);
    }
  pathname_from_glo = elo_get_pathname (glo_p);
  if (pathname_from_glo == NULL)
    {
      return (true);
    }
  if (db_get (holder_p, GLO_HOLDER_NAME_PTR, &value) == 0)
    {
      name_object_p = DB_GET_OBJECT (&value);
      if (db_get (name_object_p, GLO_NAME_PATHNAME, &value) != 0)
	{
	  return false;
	}
      pathname_from_holder = (char *) DB_GET_STRING (&value);

      if (pathname_from_holder == NULL)
	{
	  return false;
	}

      if (strcmp (pathname_from_holder, pathname_from_glo) != 0)
	{
	  elo_set_pathname (glo_p, pathname_from_holder);
	}
      db_value_clear (&value);
      return (true);
    }
  return (false);
}

/*
 * esm_get_glo_from_holder_for_read() -
 *      return: Pointer to the elo structure
 *  glo_instance_p(in) : the glo object
 */

DB_ELO *
esm_get_glo_from_holder_for_read (DB_OBJECT * glo_instance_p)
{
  int rc, save;
  DB_VALUE value;
  DB_OBJECT *glo_holder_p;
  DB_ELO *glo_p = NULL;

  AU_DISABLE (save);
  rc = db_get (glo_instance_p, GLO_CLASS_HOLDER_NAME, &value);
  if (rc != 0)
    {
      goto end;
    }
  glo_holder_p = DB_GET_OBJECT (&value);

  if (glo_holder_p == NULL)
    {
      goto end;
    }

  rc = db_get (glo_holder_p, GLO_HOLDER_GLO_NAME, &value);
  if (rc != 0)
    {
      goto end;
    }

  glo_p = DB_GET_ELO (&value);

  if (glo_p == NULL)
    {
      goto end;
    }

  if (!esm_compare_glo_pathname (glo_holder_p, glo_p))
    {
      glo_p = NULL;
    }


end:
  AU_ENABLE (save);
  return (glo_p);
}

/*
 * esm_get_glo_from_holder_for_write() -
 *      return: Pointer to the elo structure
 *  glo_instance_p(in) : the glo object
 *
 */

DB_ELO *
esm_get_glo_from_holder_for_write (DB_OBJECT * glo_instance_p)
{

  DB_VALUE value;
  DB_OBJECT *glo_holder_p;
  int rc, save;
  char *shadow_file_pathname;
  DB_ELO *glo_p = NULL;

  AU_DISABLE (save);

  rc = db_get (glo_instance_p, GLO_CLASS_HOLDER_NAME, &value);

  if (rc != 0)
    {
      goto end;
    }

  glo_holder_p = DB_GET_OBJECT (&value);

  if (glo_holder_p == NULL)
    {
      goto end;
    }

  rc = db_get (glo_holder_p, GLO_HOLDER_GLO_NAME, &value);

  if (rc != 0)
    {
      goto end;
    }

  glo_p = DB_GET_ELO (&value);

  if (glo_p == NULL)
    {
      goto end;
    }

  if (elo_get_pathname (glo_p) == NULL)
    {
      goto end;
    }

  if (!esm_shadow_file_exists (glo_holder_p))
    {
      shadow_file_pathname = esm_make_shadow_file (glo_holder_p);
    }

end:
  AU_ENABLE (save);
  return (glo_p);
}

/* Set the non-persistent position value for a GLO. */

/*
 * update_position() -
 *      return:
 *  glo_object_p(in) :
 *  position(in) :
 */

static INT64
update_position (DB_OBJECT * glo_object_p, INT64 position)
{
  /* we must reject negative positions here, or
   * they will wreak havoc on code downstream.
   */
  if (glo_object_p && position >= 0)
    {
      if (ws_put_prop (glo_object_p, PROP_GLO_POSITION, position) < 0)
	{
	  return -1;
	}
      else
	{
	  return position;
	}
    }
  else
    {
      return -1;
    }
}

/*
 * destroy_glo_and_holder_and_name() -
 *      return:
 *  esm_glo_object_p(in) :
 *  holder_p(in) :
 *
 */

static int
destroy_glo_and_holder_and_name (DB_OBJECT * esm_glo_object_p,
				 DB_OBJECT * holder_p)
{
  DB_VALUE value;
  int save;
  int error = NO_ERROR;
  DB_ELO *glo_p;

  /* initialize to null */
  glo_p = NULL;

  AU_DISABLE (save);

  error = db_get (holder_p, GLO_HOLDER_GLO_NAME, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  glo_p = DB_GET_ELO (&value);

  if (glo_p == NULL)
    {
      goto end;
    }

  if (update_position (esm_glo_object_p, 0) < 0)
    {
      goto end;
    }

  if (elo_get_pathname (glo_p) != NULL)
    {
      error = esm_make_dropped_shadow_file (holder_p);
    }
  else
    {
      if (glo_p->type == ELO_LO)
	{
	  if (update_position (esm_glo_object_p, 0) < 0)
	    {
	      AU_ENABLE (save);
	      return er_errid ();
	    }
	  elo_destroy (glo_p, esm_glo_object_p);
	  db_drop (holder_p);
	}
    }

end:
  AU_ENABLE (save);
  return (error);
}

/*
 * get_write_lock() -
 *      return: return error code
 *  glo_instance_p(in) : the glo instance
 */

static int
get_write_lock (DB_OBJECT * glo_instance_p)
{
  DB_VALUE value;
  DB_OBJECT *glo_holder_p;
  int save, error;

  AU_DISABLE (save);
  error = db_get (glo_instance_p, GLO_CLASS_HOLDER_NAME, &value);
  if (error != NO_ERROR)
    {
      goto end;

    }

  glo_holder_p = DB_GET_OBJECT (&value);

  if (glo_holder_p != NULL)
    {
      /* This method now returns an error if unable to acquire the locks */
      error = db_send (glo_holder_p, GLO_HOLDER_LOCK_METHOD, &value);
    }

end:
  AU_ENABLE (save);
  return (error);
}

/* Return the non-persistant position value for a GLO.*/

/*
 * get_position() -
 *      return: none
 *  glo_object_p(in) : the glo object
 */

static INT64
get_position (DB_OBJECT * glo_object_p)
{
  INT64 position = 0;

  if (glo_object_p)
    {
      ws_get_prop (glo_object_p, PROP_GLO_POSITION, &position);
    }

  return (position);
}

/*
 * esm_Glo_read() - reads n number of units into a buffer from the glo
 *      return: none
 *  esm_glo_object_p(out) : glo object
 *  return_argument_p(out) : returns the number of bytes read
 *  units_p(in/out) : number of units to read
 *  data_buffer_p(in/out) : destination of read
 *
 */

void
esm_Glo_read (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p,
	      const DB_VALUE * units_p, const DB_VALUE * data_buffer_p)
{
  int return_value, unit_size;
  INT64 position, offset;
  int no_of_units;
  int length;

  DB_ELO *glo_p;
  char *buffer;
  DB_VALUE value;

  db_make_int (return_argument_p, -1);	/* setup error return value */

  if ((units_p == NULL) || (DB_VALUE_TYPE (units_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  if ((data_buffer_p == NULL) || !IS_STRING (data_buffer_p))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  glo_p = esm_get_glo_from_holder_for_read (esm_glo_object_p);

  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  position = get_position (esm_glo_object_p);
  no_of_units = DB_GET_INTEGER (units_p);
  if (no_of_units <= 0)
    {
      db_make_int (return_argument_p, 0);
      return;
    }

  buffer = (char *) DB_GET_STRING (data_buffer_p);

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INTEGER (&value);

  length = (int) (((INT64) no_of_units * unit_size) / (INT64) BASE_BYTE);
  offset = ((INT64) unit_size * position) / (INT64) BASE_BYTE;

  return_value = elo_read_from (glo_p, offset, length, buffer,
				esm_glo_object_p);

  if (return_value >= 0)
    {
      if (return_value == length)
	{
	  return_value = no_of_units;
	}
      else
	{
	  return_value = (return_value * BASE_BYTE) / unit_size;
	}
    }

  /* guard against negative glo position */
  if (return_value >= 0
      && update_position (esm_glo_object_p, return_value + offset) >= 0)
    {
      db_make_int (return_argument_p, return_value);
    }
  else
    {
      esm_set_error (ERROR_DURING_READ);
    }
}

/*
 * esm_Glo_print_read() - print a portion of the data
 *      return: none
 *  esm_glo_object_p(in) :  glo object
 *  return_argument_p(out) : return length that was read
 *  argument_length(in) : length to be read
 *
 * Note: This method is like read_data, but it prints the data.
 *
 */

void
esm_Glo_print_read (DB_OBJECT * esm_glo_object_p,
		    DB_VALUE * return_argument_p, DB_VALUE * argument_length)
{
  int unprintable = 0;
  int result = -1;
  int length, i, error;
  char *buffer;
  DB_VALUE db_buffer;

  if ((argument_length == NULL)
      || (DB_VALUE_TYPE (argument_length) != DB_TYPE_INTEGER))
    {
      db_make_int (return_argument_p, -1);
      return;
    }

  length = DB_GET_INTEGER (argument_length);
  if (length <= 0)
    {
      db_make_int (return_argument_p, -1);
      return;
    }

  buffer = (char *) malloc (length);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      length);
      return;
    }

  db_make_varchar (&db_buffer, DB_MAX_VARCHAR_PRECISION, buffer, length);
  error = db_send (esm_glo_object_p, "read_data", return_argument_p,
		   argument_length, &db_buffer);
  if (error == 0)
    {
      fprintf (stdout, "\n***START PRINT\n");
      result = DB_GET_INTEGER (return_argument_p);
      for (i = 0; i < result; i++)
	{
	  if (isprint (buffer[i]))
	    {
	      (void) fputc (buffer[i], stdout);
	    }
	  else
	    {
	      unprintable++;
	      (void) fputc (' ', stdout);
	    }
	}
      if (unprintable > 0)
	{
	  fprintf (stdout,
		   "\n There were %d unprintable characters.", unprintable);
	  fprintf (stdout, " They were printed with spaces\n");
	}
      fprintf (stdout, "\n***END PRINT\n");
    }
  else
    {
      fprintf (stdout, "Error %s\n", db_error_string (3));
    }
  free_and_init (buffer);

}

/*
 * esm_Glo_write() - over-write n units from buffer into the glo
 *      return: none
 *  esm_glo_object_p(in) : glo object
 *  return_argument_p(out) : return the number of units written
 *  units(in) : the number of units to write
 *  data_buffer(in) : the data to write
 */

void
esm_Glo_write (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p,
	       DB_VALUE * unit_p, DB_VALUE * data_buffer_p)
{
  int return_value, unit_size;
  INT64 position, offset;
  int no_of_units;
  int length;

  DB_ELO *glo_p;
  char *buffer;
  DB_VALUE value;

  db_make_int (return_argument_p, -1);	/* error return value */

  if ((unit_p == NULL) || (DB_VALUE_TYPE (unit_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  if ((data_buffer_p == NULL) || !IS_STRING (data_buffer_p))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  if (get_write_lock (esm_glo_object_p) != NO_ERROR)
    {
      esm_set_error (COULD_NOT_ACQUIRE_WRITE_LOCK);
      return;
    }

  glo_p = esm_get_glo_from_holder_for_write (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  position = get_position (esm_glo_object_p);
  no_of_units = DB_GET_INTEGER (unit_p);
  if (no_of_units <= 0)
    {
      db_make_int (return_argument_p, 0);
      return;
    }

  buffer = (char *) DB_GET_STRING (data_buffer_p);

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INTEGER (&value);

  length = (int) (((INT64) no_of_units * unit_size) / (INT64) BASE_BYTE);
  offset = ((INT64) unit_size * position) / (INT64) BASE_BYTE;

  return_value = elo_write_to (glo_p, offset, length, buffer,
			       esm_glo_object_p);

  if (return_value >= 0)
    {
      if (return_value == length)
	{
	  return_value = no_of_units;
	}
      else
	{
	  return_value = return_value * unit_size;
	}
    }

  /* guard against negative glo position */
  if (return_value >= 0
      && update_position (esm_glo_object_p, return_value + offset) >= 0)
    {
      db_make_int (return_argument_p, return_value);
    }
  else
    {
      esm_set_error (ERROR_DURING_WRITE);
    }
}

/*
 * esm_Glo_insert() - insert n units from buffer into the glo
 *      return: none
 *  esm_glo_object_p(in) : glo object
 *  return_argument_p(out) : return value of this method
 *  unit_p(in) : units of data to insert
 *  data_buffer_p(in) : data to insert into glo
 */

void
esm_Glo_insert (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p,
		DB_VALUE * unit_p, DB_VALUE * data_buffer_p)
{
  int return_value, unit_size;
  int no_of_units;
  INT64 position, offset;
  int length;
  DB_ELO *glo_p;
  char *buffer;
  DB_VALUE value;

  db_make_int (return_argument_p, -1);	/* error return value */

  if ((unit_p == NULL) || (DB_VALUE_TYPE (unit_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  if ((data_buffer_p == NULL) || !IS_STRING (data_buffer_p))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  if (get_write_lock (esm_glo_object_p) != NO_ERROR)
    {
      esm_set_error (COULD_NOT_ACQUIRE_WRITE_LOCK);
      return;
    }

  glo_p = esm_get_glo_from_holder_for_write (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  position = get_position (esm_glo_object_p);
  no_of_units = DB_GET_INTEGER (unit_p);
  if (no_of_units <= 0)
    {
      db_make_int (return_argument_p, 0);
      return;
    }
  buffer = (char *) DB_GET_STRING (data_buffer_p);

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INTEGER (&value);

  length = (int) (((INT64) no_of_units * unit_size) / (INT64) BASE_BYTE);
  offset = ((INT64) unit_size * position) / (INT64) BASE_BYTE;

  return_value = elo_insert_into (glo_p, offset, length, buffer,
				  esm_glo_object_p);

  if (return_value >= 0)
    {
      if (return_value == length)
	{
	  return_value = no_of_units;
	}
      else
	{
	  return_value = (return_value * unit_size) / BASE_BYTE;
	}
    }

  /* guard against negative glo position */
  if (return_value >= 0
      && update_position (esm_glo_object_p, return_value + offset) >= 0)
    {
      db_make_int (return_argument_p, return_value);
    }
  else
    {
      esm_set_error (ERROR_DURING_INSERT);
    }
}

/*
 * esm_Glo_delete() - deletes n units from the object
 *      return: none
 *  esm_glo_object_p(in) : glo object
 *  return_argument_p(out) : returns the number of bytes deleted
 *  unit_p(in) : units to delete
 *
 */

void
esm_Glo_delete (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p,
		DB_VALUE * unit_p)
{
  int return_value, unit_size, no_of_units;
  INT64 position, offset;
  int length;
  DB_ELO *glo_p;
  DB_VALUE value;

  db_make_int (return_argument_p, -1);	/* error return value */

  if (get_write_lock (esm_glo_object_p) != NO_ERROR)
    {
      esm_set_error (COULD_NOT_ACQUIRE_WRITE_LOCK);
      return;
    }

  if ((unit_p == NULL) || (DB_VALUE_TYPE (unit_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }
  glo_p = esm_get_glo_from_holder_for_write (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  position = get_position (esm_glo_object_p);
  no_of_units = DB_GET_INTEGER (unit_p);

  if (no_of_units <= 0)
    {
      db_send (esm_glo_object_p, GLO_METHOD_SIZE, return_argument_p);
      return;
    }

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INTEGER (&value);
  length = (int) (((INT64) no_of_units * unit_size) / (INT64) BASE_BYTE);
  offset = (INT64) (unit_size * position) / (INT64) BASE_BYTE;
  return_value = (int) elo_delete_from (glo_p, offset, length,
					esm_glo_object_p);
  if (return_value < 0)
    {
      esm_set_error (ERROR_DURING_DELETE);
    }
  else
    {
      db_make_int (return_argument_p, return_value);
    }
}

/*
 * esm_Glo_seek() - seek to unit location n
 *      return: none
 *  esm_glo_object_p(in) : glo object
 *  return_argument_p(out) : return current position
 *  loc(in) : seek location
 *
 */

void
esm_Glo_seek (DB_OBJECT * esm_glo_object_p,
	      DB_VALUE * return_argument_p, DB_VALUE * location_p)
{
  int return_value;
  int unit_size;
  DB_VALUE value;
  DB_ELO *glo_p;

  db_make_int (return_argument_p, -1);	/* error return value */
  if ((location_p == NULL) || (DB_VALUE_TYPE (location_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INT (&value);
  glo_p = esm_get_glo_from_holder_for_read (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  return_value = DB_GET_INT (location_p);
  return_value =
    (int) (((INT64) unit_size * return_value) / (INT64) BASE_BYTE);

  /* guard against negative glo position */
  if (return_value >= 0
      && update_position (esm_glo_object_p, return_value) >= 0)
    {
      db_make_int (return_argument_p, return_value);
      return;
    }
  else
    {
      esm_set_error (ERROR_DURING_SEEK);
    }
}

/*
 * esm_Glo_truncate() - truncates the glo at the current location
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return the number of bytes truncated
 *
 */

void
esm_Glo_truncate (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  int unit_size;
  INT64 position, offset;
  int return_value;
  DB_ELO *glo_p;

  if (get_write_lock (esm_glo_object_p) != NO_ERROR)
    {
      esm_set_error (COULD_NOT_ACQUIRE_WRITE_LOCK);
      return;
    }

  glo_p = esm_get_glo_from_holder_for_write (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      return;
    }

  position = get_position (esm_glo_object_p);

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, return_argument_p);
  unit_size = DB_GET_INTEGER (return_argument_p);
  offset = (INT64) unit_size *position / (INT64) BASE_BYTE;
  return_value = (int) elo_truncate (glo_p, offset, esm_glo_object_p);
  if (return_value >= 0)
    {
      db_make_int (return_argument_p, return_value);
    }
  else
    {
      esm_set_error (ERROR_DURING_TRUNCATION);
    }
}

/*
 * esm_Glo_append() - appends the data in buffer to the end of the glo
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return the amount of data written
 *  unit_p(in) : units to append
 *  data_buffer_p(in) : the data to append
 */

void
esm_Glo_append (DB_OBJECT * esm_glo_object_p,
		DB_VALUE * return_argument_p, DB_VALUE * unit_p,
		DB_VALUE * data_buffer_p)
{
  int return_value, offset, no_of_units;
  int unit_size, length;
  char *buffer;
  DB_ELO *glo_p;
  DB_VALUE value;

  db_make_int (return_argument_p, -1);	/* error return value */
  if ((unit_p == NULL) || (DB_VALUE_TYPE (unit_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  if ((data_buffer_p == NULL) || !IS_STRING (data_buffer_p))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  if (get_write_lock (esm_glo_object_p) != NO_ERROR)
    {
      db_make_int (return_argument_p, -1);
      return;
    }
  glo_p = esm_get_glo_from_holder_for_write (esm_glo_object_p);
  if (glo_p == NULL)
    {
      db_make_int (return_argument_p, -1);
      return;
    }

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value);
  unit_size = DB_GET_INTEGER (&value);
  no_of_units = DB_GET_INTEGER (unit_p);
  if (no_of_units <= 0)
    {
      db_make_int (return_argument_p, 0);
      return;
    }

  buffer = (char *) DB_GET_STRING (data_buffer_p);

  length = (int) (((INT64) no_of_units * unit_size) / (INT64) BASE_BYTE);
  return_value = elo_append_to (glo_p, length, buffer, esm_glo_object_p);
  offset = elo_get_size (glo_p, esm_glo_object_p);

  /* guard against negative glo position */
  if (return_value >= 0
      && update_position (esm_glo_object_p,
			  (offset * unit_size) / (INT64) BASE_BYTE) >= 0)
    {
      db_make_int (return_argument_p, return_value);
    }
  else
    {
      esm_set_error (ERROR_DURING_APPEND);
    }
}

/*
 * esm_glo_pathname() - get the pathname of the glo object
 *      return: none
 *  esm_glo_object_p(int) : the glo object
 *  return_argument_p(out) : returns the pathname of the glo object
 *
 */

void
esm_Glo_pathname (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  DB_OBJECT *holder_p, *name_object_p;
  DB_VALUE value;
  int rc, save;

  if (return_argument_p)
    {
      db_make_null (return_argument_p);
      AU_DISABLE (save);

      rc = db_get (esm_glo_object_p, GLO_CLASS_HOLDER_NAME, &value);
      if (rc == NO_ERROR)
	{
	  holder_p = DB_GET_OBJECT (&value);
	  rc = db_get (holder_p, GLO_HOLDER_NAME_PTR, &value);
	  if (rc == NO_ERROR)
	    {
	      name_object_p = DB_GET_OBJECT (&value);
	      if (name_object_p != NULL)
		{
		  db_get (name_object_p, GLO_NAME_PATHNAME,
			  return_argument_p);
		}
	    }
	}

      AU_ENABLE (save);
    }
}

/*
 * esm_Glo_full_pathname() - get real full pathname of the glo object
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : returns real full pathname of the glo object
 *
 */

void
esm_Glo_full_pathname (DB_OBJECT * esm_glo_object_p,
		       DB_VALUE * return_argument_p)
{
  DB_OBJECT *holder_p, *name_object_p;
  DB_VALUE value;
  int rc, save;
  char *pathname, expanded_path[PATH_MAX], *real_path;

  if (return_argument_p == NULL)
    {
      return;
    }

  db_make_null (return_argument_p);
  AU_DISABLE (save);

  rc = db_get (esm_glo_object_p, GLO_CLASS_HOLDER_NAME, &value);
  if (rc != NO_ERROR)
    {
      return;
    }

  holder_p = DB_GET_OBJECT (&value);
  rc = db_get (holder_p, GLO_HOLDER_NAME_PTR, &value);
  if (rc == NO_ERROR)
    {
      name_object_p = DB_GET_OBJECT (&value);
      if (name_object_p != NULL)
	{
	  db_get (name_object_p, GLO_NAME_PATHNAME, return_argument_p);
	  pathname = (char *) DB_GET_STRING (return_argument_p);
	  if (pathname != NULL)
	    {
	      esm_expand_pathname (pathname, expanded_path, PATH_MAX);

	      real_path = (char *) db_private_alloc (NULL, PATH_MAX);
	      if (real_path == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, PATH_MAX);
		  return;
		}

	      realpath (expanded_path, real_path);
	      db_make_string (return_argument_p, real_path);
	      return_argument_p->need_clear = true;
	    }
	}
    }
  AU_ENABLE (save);
  return;
}

/*
 * esm_Glo_init() - Initializes the glo object's attributes to their default
 *                  values.
 *      return: none
 *  esm_glo_object_p(int) : the glo object
 *  return_argument_p(out) : return value of this method
 *
 */

void
esm_Glo_init (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  DB_ELO *glo_p;

  db_make_int (return_argument_p, UNIT_SIZE_DEFAULT);
  db_put_internal (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME,
		   return_argument_p);

  db_make_int (return_argument_p, HEADER_SIZE_DEFAULT);
  db_put_internal (esm_glo_object_p, GLO_CLASS_HEADER_SIZE_NAME,
		   return_argument_p);

  glo_p = esm_get_glo_from_holder_for_read (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      db_make_int (return_argument_p, -1);
      return;
    }

  update_position (esm_glo_object_p, 0);
}

/*
 * esm_Glo_size() - the current size of the glo object
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : returns the current size of the glo object
 *
 */

void
esm_Glo_size (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  INT64 total_size;
  int unit_size;
  DB_ELO *glo_p;

  glo_p = esm_get_glo_from_holder_for_read (esm_glo_object_p);
  if (glo_p == NULL)
    {
      esm_set_error (UNABLE_TO_FIND_GLO_STRUCTURE);
      db_make_int (return_argument_p, -1);
      return;
    }

  db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, return_argument_p);
  unit_size = DB_GET_INTEGER (return_argument_p);
  total_size =
    (elo_get_size (glo_p, esm_glo_object_p) * (INT64) BASE_BYTE) / unit_size;

  db_make_int (return_argument_p, (int) total_size);
}

/*
 * esm_Glo_compress() - compresses an LO into a (possibly) more efficient storage
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *
 */

void
esm_Glo_compress (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  DB_ELO *glo_p;
  int rc = 0;

  glo_p = esm_get_glo_from_holder_for_read (esm_glo_object_p);
  if (glo_p != NULL)
    {
      rc = elo_compress (glo_p);
    }

  if (return_argument_p)
    {
      db_make_int (return_argument_p, rc);
    }
}

/*
 * esm_Glo_create() - creates a new instance of an glo
 *      return: none
 *  esm_glo_class(in) : the glo class object
 *  return_argument_p(out) : return the new instance
 *  path_name_p(in) : the path name of external file
 *
 * Note : creates a new instance of an glo based on the argument passed
 *        if argument is null, large object is created, if argument is
 *        is not null, fbo is created.  Returns the new instance.
 *
 */

void
esm_Glo_create (DB_OBJECT * esm_glo_class, DB_VALUE * return_argument_p,
		DB_VALUE * path_name_p)
{
  DB_OBJECT *new_esm_glo_object_p, *glo_holder_class_p;
  DB_VALUE value;
  int save, error;
  const char *hint_classnames[3];
  int hint_subclasses[3];
  LOCK hint_locks[3];
  LC_FIND_CLASSNAME find;

  /* Return an error to method invocation unless we succeed */
  db_make_error (return_argument_p, -1);

  /*  We first have to fetch the class itself with some kind of intention
   *  write lock since we are going to modify it.  In fact, to avoid
   *  deadlocks with deleters, we really need an SIX lock. If we do not
   *  sm_class_name will otherwise fetch it with IS, and then we have
   *  to upgrade it anyway.
   */
  if (locator_fetch_object (esm_glo_class, DB_FETCH_QUERY_WRITE) == NULL)
    {
      /* Unable to get the necessary lock */
      db_make_error (return_argument_p, er_errid ());
      return;
    }

  /* Be sure we lock the user's class, in case they are subclassing glo */
  hint_classnames[0] = sm_class_name (esm_glo_class);
  hint_classnames[1] = GLO_HOLDER_CLASS_NAME;
  hint_classnames[2] = GLO_NAME_CLASS_NAME;
  memset (hint_subclasses, 0, 3 * sizeof (int));

  hint_locks[0] =
    locator_fetch_mode_to_lock (DB_FETCH_CLREAD_INSTWRITE, LC_CLASS);
  hint_locks[1] =
    locator_fetch_mode_to_lock (DB_FETCH_CLREAD_INSTWRITE, LC_CLASS);
  hint_locks[2] = locator_fetch_mode_to_lock (DB_FETCH_QUERY_WRITE, LC_CLASS);

  if (path_name_p != NULL && DB_VALUE_TYPE (path_name_p) != DB_TYPE_NULL)
    {
      if ((path_name_p == NULL) || !IS_STRING (path_name_p))
	{
	  esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
	  return;
	}

      find = locator_lockhint_classes (3, hint_classnames, hint_locks,
				       hint_subclasses, 1);
    }
  else
    {
      find = locator_lockhint_classes (2, hint_classnames, hint_locks,
				       hint_subclasses, 1);
    }

  glo_holder_class_p = db_find_class (GLO_HOLDER_CLASS_NAME);
  if (find == LC_CLASSNAME_EXIST && glo_holder_class_p != NULL)
    {
      AU_DISABLE (save);

      error =
	db_send (glo_holder_class_p, GLO_HOLDER_CREATE_METHOD, &value,
		 path_name_p);
      if (error != NO_ERROR)
	{
	  AU_ENABLE (save);
	  goto end;
	}

      if (!DB_GET_OBJECT (&value))
	{
	  AU_ENABLE (save);
	  goto end;
	}

      new_esm_glo_object_p = db_create_internal (esm_glo_class);
      if (!new_esm_glo_object_p)
	{
	  AU_ENABLE (save);
	  goto end;
	}
      error = db_put_internal (new_esm_glo_object_p,
			       GLO_CLASS_HOLDER_NAME, &value);
      if (error == 0)
	{
	  error = db_send (new_esm_glo_object_p, GLO_METHOD_INITIALIZE,
			   return_argument_p);
	  if (error == 0)
	    {
	      db_make_object (return_argument_p, new_esm_glo_object_p);
	    }
	}
      AU_ENABLE (save);
    }

end:
  if (DB_VALUE_TYPE (return_argument_p) != DB_TYPE_OBJECT)
    {
      db_make_error (return_argument_p, er_errid ());
    }
}

/*
 * esm_Glo_destroy() - deletes the glo data
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *
 */

void
esm_Glo_destroy (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  DB_OBJECT *holder_p;
  DB_VALUE value;
  int rc;

  /* Return an error to method invocation unless we succeed */
  db_make_error (return_argument_p, -1);

  /* Grab right locks on all classes and compositions to avoid deadlock. */
  if (db_fetch_composition (esm_glo_object_p, DB_FETCH_WRITE, 3, true)
      != NO_ERROR)
    {
      goto error;
    }

  /* Get the glo_holder object */
  rc = db_get (esm_glo_object_p, GLO_CLASS_HOLDER_NAME, &value);
  if (rc != NO_ERROR)
    {
      goto error;
    }

  /*
   * Note: if the holder is null, then this glo was corrupted in
   * some way.  If we return an error here, then the user will never
   * be able (through normal means) to delete their glo.  Since they are
   * trying to delete it anyway, just let it slide if the holder is null.
   */
  db_make_int (return_argument_p, 0);
  holder_p = DB_GET_OBJECT (&value);
  if (holder_p == NULL)
    {
      return;
    }

  /* destroy the glo data, name and holder objects if necessary */
  rc = destroy_glo_and_holder_and_name (esm_glo_object_p, holder_p);
  if (rc != NO_ERROR)
    {
      /* implies the glo is corrupted somehow */
      goto error;
    }

  /*
   * Deleted ok, with no errors.
   */
  return;

  /*
   * An error occurred.
   */
error:
  db_make_error (return_argument_p, er_errid ());
}

/*
 * esm_pad_overflow() - computes the additional amount to write to
 *                      the destination Glo to pad out the size for
 *                      copy_from/copy_to overflow
 *      return:
 *  dest_obj(in) :
 *  fd(in) :
 *  bytes_written(int) :
 *  in_unit_size (in)
 *  out_unit_size(in)
 */

static int
esm_pad_overflow (DB_OBJECT * dest_obj, const int fd, const int bytes_written,
		  int in_unit_size, int out_unit_size)
{
  int bytes_to_pad, write_size;
  char overflow[PAD_SIZE] = { '\0' };
  int rc = 0;
  DB_VALUE value1, value2, value3;

  if (in_unit_size <= 0)
    {
      in_unit_size = UNIT_SIZE_DEFAULT;
    }
  if (out_unit_size <= 0)
    {
      out_unit_size = UNIT_SIZE_DEFAULT;
    }
  if (in_unit_size < out_unit_size)
    {
      rc = bytes_to_pad = (bytes_written % (out_unit_size / in_unit_size));
      if (rc > 0)
	{
	  rc = bytes_to_pad = (out_unit_size / in_unit_size) - rc;
	  do
	    {
	      if (bytes_to_pad < PAD_SIZE)
		{
		  write_size = bytes_to_pad;
		}
	      else
		{
		  write_size = PAD_SIZE;
		}

	      db_make_int (&value1, write_size);
	      db_make_varchar (&value2, DB_MAX_VARCHAR_PRECISION, overflow,
			       write_size);
	      if (dest_obj)
		{
		  if ((db_send (dest_obj, GLO_METHOD_APPEND, &value3,
				&value1, &value2) != 0)
		      || (DB_GET_INTEGER (&value3) != write_size))
		    {
		      rc = -1;
		      break;
		    }
		}
	      else if (write (fd, overflow, write_size) != write_size)
		{
		  rc = -1;
		  break;
		}
	    }
	  while ((bytes_to_pad -= write_size) > 0);
	}
    }
  return (rc);
}

/*
 * esm_Glo_migrate() - Copies the data from the source object
 *                     to the destination object
 *      return: none
 *  source(in) : the source object
 *  dest_obj(in) : the destination object
 *  return_argument_p(out) : return value (size of data transferred)
 */

static void
esm_Glo_migrate (DB_OBJECT * source, DB_OBJECT * dest_obj,
		 DB_VALUE * return_argument_p)
{
  DB_VALUE value1, value2, value3, value4;
  int length, pads_written, total_length = 0;
  int old_byte_size_in = -1, old_byte_size_out = -1;
  char *buffer;
  int network_pagesize;

  db_make_int (&value1, 0);
  if (db_send (dest_obj, GLO_METHOD_SEEK, &value3, &value1) == NO_ERROR)
    {
      if (db_send (source, GLO_METHOD_SEEK, &value3, &value1) == NO_ERROR)
	{
	  if (db_send (dest_obj, GLO_METHOD_TRUNCATE, &value3) == NO_ERROR)
	    {
	      if (db_get (dest_obj, GLO_CLASS_UNIT_SIZE_NAME, &value1) !=
		  NO_ERROR)
		{
		  esm_set_error (ERROR_DURING_MIGRATE);
		  return;
		}
	      old_byte_size_out = DB_GET_INTEGER (&value1);
	      if (old_byte_size_out == UNIT_SIZE_DEFAULT)
		{
		  old_byte_size_out = -1;
		}
	      else
		{
		  db_make_int (&value1, UNIT_SIZE_DEFAULT);
		  db_put_internal (dest_obj, GLO_CLASS_UNIT_SIZE_NAME,
				   &value1);
		}
	      if (db_get (source, GLO_CLASS_UNIT_SIZE_NAME, &value1) != 0)
		{
		  esm_set_error (ERROR_DURING_MIGRATE);
		  return;
		}
	      old_byte_size_in = DB_GET_INTEGER (&value1);
	      if (old_byte_size_in == UNIT_SIZE_DEFAULT)
		{
		  old_byte_size_in = -1;
		}
	      else
		{
		  db_make_int (&value1, UNIT_SIZE_DEFAULT);
		  db_put_internal (source, GLO_CLASS_UNIT_SIZE_NAME, &value1);
		}
	      /*
	       * Start the migration process with the best network page size
	       */
	      network_pagesize = db_network_page_size ();
	      buffer = (char *) malloc (network_pagesize);
	      if (buffer == NULL)
		{
		  esm_set_error (ERROR_DURING_MIGRATE);
		  return;
		}
	      db_make_int (&value2, network_pagesize);
	      db_make_varchar (&value1, DB_MAX_VARCHAR_PRECISION,
			       buffer, network_pagesize);
	      while (true)
		{
		  if (db_send
		      (source, GLO_METHOD_READ, &value3, &value2,
		       &value1) != 0)
		    {
		      break;
		    }
		  if (DB_VALUE_TYPE (&value3) != DB_TYPE_INTEGER)
		    {
		      break;
		    }
		  length = DB_GET_INTEGER (&value3);
		  total_length += length;
		  if (length < 0)
		    {
		      break;
		    }
		  if (db_send (dest_obj, GLO_METHOD_WRITE, &value4, &value3,
			       &value1) != 0)
		    {
		      break;
		    }
		  if (length < network_pagesize)
		    {
		      pads_written = esm_pad_overflow (dest_obj, 0,
						       total_length,
						       old_byte_size_in,
						       old_byte_size_out);
		      if (pads_written < 0)
			{
			  total_length = -1;
			}
		      else
			{
			  total_length += pads_written;
			}

		      db_make_int (return_argument_p, total_length);

		      if (old_byte_size_in > 0)
			{
			  db_make_int (&value1, old_byte_size_in);
			  db_put_internal (source, GLO_CLASS_UNIT_SIZE_NAME,
					   &value1);
			}
		      if (old_byte_size_out > 0)
			{
			  db_make_int (&value1, old_byte_size_out);
			  db_put_internal (dest_obj, GLO_CLASS_UNIT_SIZE_NAME,
					   &value1);
			}

		      free_and_init (buffer);
		      return;
		    }
		}
	      if (old_byte_size_in > 0)
		{
		  db_make_int (&value1, old_byte_size_in);
		  db_put_internal (source, GLO_CLASS_UNIT_SIZE_NAME, &value1);
		}
	      if (old_byte_size_out > 0)
		{
		  db_make_int (&value1, old_byte_size_out);
		  db_put_internal (dest_obj, GLO_CLASS_UNIT_SIZE_NAME,
				   &value1);
		}
	      free_and_init (buffer);
	    }
	}
    }

  esm_set_error (ERROR_DURING_MIGRATE);
  db_make_int (return_argument_p, -1);
}

/*
 * esm_Glo_copy_to() - copies the data in this object to the destination
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  destination(in) : Either a string or another Glo
 * Note:
 *   copies the data in this object to the destination. The destination may
 *   be another GLO (FBO or LO) or a string (used as a pathname)
 *
 */

void
esm_Glo_copy_to (DB_OBJECT * esm_glo_object_p,
		 DB_VALUE * return_argument_p, DB_VALUE * destination_p)
{
  DB_VALUE value1, value2, value3;
  char *pathname;
  int rc, fd, pads_written;
  int old_byte_size = -1, length = 0, total_length = 0;
  DB_OBJECT *dest_object_p = NULL;
  char realpath[PATH_MAX];
  char *buffer;
  int network_pagesize;

  db_make_int (return_argument_p, -1);	/* error return value */

  if (DB_IS_NULL (destination_p)
      || (!(IS_STRING (destination_p)
	    || (DB_VALUE_TYPE (destination_p) == DB_TYPE_OBJECT))))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  if (IS_STRING (destination_p))
    {
      pathname = (char *) DB_PULL_STRING (destination_p);
      if (envvar_expand (pathname, realpath, PATH_MAX) != NO_ERROR)
	{
	  strcpy (realpath, pathname);
	}

      fd = open (realpath, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
      if (fd > 0)
	{
	  ftruncate (fd, (int) 0);
	  db_make_int (&value2, 0);
	  db_send (esm_glo_object_p, GLO_METHOD_SEEK, &value1, &value2);
	  if (db_get (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME, &value1) !=
	      0)
	    {
	      esm_set_error (COPY_TO_ERROR);
	      return;
	    }
	  old_byte_size = DB_GET_INTEGER (&value1);
	  if (old_byte_size == UNIT_SIZE_DEFAULT)
	    {
	      old_byte_size = -1;
	    }
	  else
	    {
	      db_make_int (&value1, UNIT_SIZE_DEFAULT);
	      db_put_internal (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME,
			       &value1);
	    }

	  /*
	   * Start the copy process with the best network page size
	   */

	  network_pagesize = db_network_page_size ();
	  buffer = (char *) malloc (network_pagesize);
	  if (buffer == NULL)
	    {
	      esm_set_error (COPY_TO_ERROR);
	      return;
	    }

	  db_make_int (&value2, network_pagesize);
	  db_make_varchar (&value3, DB_MAX_VARCHAR_PRECISION,
			   buffer, network_pagesize);
	  while (db_send (esm_glo_object_p, GLO_METHOD_READ, &value1, &value2,
			  &value3) == 0)
	    {
	      length = DB_GET_INTEGER (&value1);
	      if (DB_VALUE_TYPE (&value1) == DB_TYPE_INTEGER && length > 0)
		{
		  total_length += length;
		  rc = write (fd, buffer, length);
		  if (rc != length)
		    {
		      /* did we run out of file space? */
		      close (fd);
		      unlink (realpath);
		      esm_set_error (COPY_TO_ERROR);
		      if (old_byte_size > 0)
			{
			  db_make_int (&value1, old_byte_size);
			  db_put_internal (esm_glo_object_p,
					   GLO_CLASS_UNIT_SIZE_NAME, &value1);
			}
		      free_and_init (buffer);
		      return;	/* This is an error return - out of disk space */
		    }
		}
	      else
		{
		  pads_written = esm_pad_overflow (NULL, fd, total_length,
						   old_byte_size, 8);
		  if (pads_written < 0)
		    {
		      esm_set_error (COPY_TO_ERROR);
		      total_length = -1;
		    }
		  else
		    {
		      total_length += pads_written;
		    }

		  close (fd);
		  if (length != 0)
		    {
		      unlink (realpath);
		      esm_set_error (COPY_TO_ERROR);
		    }
		  else
		    {
		      db_make_int (return_argument_p, total_length);
		    }

		  if (old_byte_size > 0)
		    {
		      db_make_int (&value1, old_byte_size);
		      db_put_internal (esm_glo_object_p,
				       GLO_CLASS_UNIT_SIZE_NAME, &value1);
		    }
		  free_and_init (buffer);
		  return;	/* this is the normal return */
		}
	    }

	  free_and_init (buffer);
	  close (fd);
	  if (old_byte_size > 0)
	    {
	      db_make_int (&value1, old_byte_size);
	      db_put_internal (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME,
			       &value1);
	    }
	}
      else
	{
	  esm_set_error (COPY_TO_ERROR);
	}
      return;			/* this is an error return, unable to open file */
    }
  else if (DB_VALUE_TYPE (destination_p) == DB_TYPE_OBJECT)
    {
      dest_object_p = DB_PULL_OBJECT (destination_p);
      esm_Glo_migrate (esm_glo_object_p, dest_object_p, return_argument_p);
    }
}

/*
 * esm_Glo_copy_from() - copies the data into this object from the source
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  source_p(in) : Either a string or another Glo
 *
 * Note:
 *   copies the data from the source into this object. The source may be
 *   another GLO (FBO or LO) or a string (used as a pathname)
 *
 */

void
esm_Glo_copy_from (DB_OBJECT * esm_glo_object_p,
		   DB_VALUE * return_argument_p, DB_VALUE * source_p)
{
  DB_VALUE value1, value2, value3;
  char *pathname;
  int fd, length, pads_written;
  int old_byte_size = -1, total_length = 0, rc, write_length;
  DB_OBJECT *source_object_p;
  char realpath[PATH_MAX];
  char *buffer;
  int network_pagesize;

  db_make_int (return_argument_p, -1);	/* error return value */

  if (DB_IS_NULL (source_p)
      || (!(IS_STRING (source_p)
	    || (DB_VALUE_TYPE (source_p) == DB_TYPE_OBJECT))))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  if (IS_STRING (source_p))
    {
      pathname = (char *) DB_PULL_STRING (source_p);
      if (envvar_expand (pathname, realpath, PATH_MAX) != NO_ERROR)
	{
	  strcpy (realpath, pathname);
	}

      fd = open (realpath, O_RDONLY, 0);
      if (fd > 0)
	{
	  db_make_int (&value2, 0);
	  if (db_send (esm_glo_object_p, GLO_METHOD_SEEK, &value1, &value2) ==
	      0)
	    {
	      if (db_send (esm_glo_object_p, GLO_METHOD_TRUNCATE, &value1) ==
		  0)
		{
		  if (db_get
		      (esm_glo_object_p, GLO_CLASS_UNIT_SIZE_NAME,
		       &value1) != 0)
		    {
		      esm_set_error (COPY_FROM_ERROR);
		      return;	/* error return, could not get unit size */
		    }
		  old_byte_size = DB_GET_INTEGER (&value1);
		  if (old_byte_size == UNIT_SIZE_DEFAULT)
		    {
		      old_byte_size = -1;
		    }
		  else
		    {
		      db_make_int (&value1, UNIT_SIZE_DEFAULT);
		      db_put_internal (esm_glo_object_p,
				       GLO_CLASS_UNIT_SIZE_NAME, &value1);
		    }


		  /*
		   * Start the copy process with the best network page size
		   */

		  network_pagesize = db_network_page_size ();
		  buffer = (char *) malloc (network_pagesize);
		  if (buffer == NULL)
		    {
		      esm_set_error (COPY_FROM_ERROR);
		      return;
		    }

		  db_make_varchar (&value2, DB_MAX_VARCHAR_PRECISION,
				   buffer, network_pagesize);
		  while ((length = read (fd, buffer, network_pagesize)) > 0)
		    {
		      total_length += length;
		      db_make_int (&value1, length);
		      rc = db_send (esm_glo_object_p, GLO_METHOD_WRITE,
				    &value3, &value1, &value2);
		      write_length = DB_GET_INTEGER (&value3);
		      if ((rc != 0) || (write_length != length))
			{
			  close (fd);
			  esm_set_error (COPY_FROM_ERROR);
			  if (old_byte_size > 0)
			    {
			      db_make_int (&value1, old_byte_size);
			      db_put_internal (esm_glo_object_p,
					       GLO_CLASS_UNIT_SIZE_NAME,
					       &value1);
			    }
			  free_and_init (buffer);
			  return;	/* This is an error return, error on write */
			}
		    }


		  free_and_init (buffer);
		  pads_written = esm_pad_overflow (esm_glo_object_p,
						   0, total_length,
						   old_byte_size, 8);
		  if (pads_written < 0)
		    {
		      esm_set_error (COPY_TO_ERROR);
		      total_length = -1;
		    }
		  else
		    {
		      total_length += pads_written;
		    }

		  close (fd);
		  db_make_int (return_argument_p, total_length);
		  if (old_byte_size > 0)
		    {
		      db_make_int (&value1, old_byte_size);
		      db_put_internal (esm_glo_object_p,
				       GLO_CLASS_UNIT_SIZE_NAME, &value1);
		    }
		  return;	/* Successful return */
		}
	    }
	  esm_set_error (COPY_FROM_ERROR);
	  return;		/* Error return, the truncate failed. */
	}
      else
	{
	  esm_set_error (COPY_FROM_ERROR);
	}
    }
  else if (DB_VALUE_TYPE (source_p) == DB_TYPE_OBJECT)
    {
      source_object_p = DB_PULL_OBJECT (source_p);
      esm_Glo_migrate (source_object_p, esm_glo_object_p, return_argument_p);
    }
}

/*
 * esm_Glo_position() - the current read/write pointer position of the glo object
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *
 */

void
esm_Glo_position (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  INT64 position;

  position = get_position (esm_glo_object_p);
  db_make_int (return_argument_p, (int) position);
}

/*
 * esm_search() - searches the glo for a text string
 *      return : none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  search_for(in) : return search string
 *  search_type (in) : search type (text or binary)
 *  search_length(in) :
 *
 * Note : searches the glo for a text string. If the string is found,
 *        the offset into the GLO where the string is located will be
 *        the return argument. If the text string was not found, the
 *        return value will be < 0.
 *
 */

static void
esm_search (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p,
	    DB_VALUE * search_for_object_p, int search_type,
	    int search_length)
{
#if 1				/* not implemented yet */
  db_make_int (return_argument_p, -1);	/* error return value */
#else
  int current_position, buffer_start;
  int read_length;
  char *input_string;
  DB_VALUE return_value, value1, value2;
  char buffer[SEARCH_BUFFER_SIZE + 1];
  REG_SEARCH_DESCRIPTOR *search_data_struct_p;

  db_make_int (return_argument_p, -1);	/* error return value */
  if ((search_for_object_p == NULL) || !IS_STRING (search_for_object_p))
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  input_string = (char *) DB_GET_STRING (search_for_object_p);
  if (input_string == NULL)
    {
      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
      return;
    }

  /* Allocate and initialize the search state block */
  search_data_struct_p = (REG_SEARCH_DESCRIPTOR *)
    malloc (sizeof (REG_SEARCH_DESCRIPTOR));
  if (search_data_struct_p == NULL)
    {
      esm_set_error (COULD_NOT_ALLOCATE_SEARCH_BUFFERS);
      return;
    }

  /*
   * assume no escape char for multimedia methods, need to have a way to
   * pass this in ?
   */

  search_data_struct_p->like_escape = NULL;

  search_data_struct_p->reg_exp_text = (REGTEXT *) input_string;
  if (search_type == REG_BIN)
    {
      search_data_struct_p->reg_exp_length = search_length;
    }
  else
    {
      search_data_struct_p->reg_exp_length = strlen (input_string);
    }
  search_data_struct_p->regx_max =
    (search_data_struct_p->reg_exp_length * 3) + 10;
  search_data_struct_p->regx =
    (REG_STRUCT *) malloc (search_data_struct_p->regx_max *
			   sizeof (REG_STRUCT));
  if (search_data_struct_p->regx == NULL)
    {
      esm_set_error (COULD_NOT_ALLOCATE_SEARCH_BUFFERS);
      return;
    }
  if ((search_data_struct_p->work_buff = (REGTEXT *)
       malloc (SEARCH_BUFFER_SIZE * sizeof (REGTEXT))) == NULL)
    {
      esm_set_error (COULD_NOT_ALLOCATE_SEARCH_BUFFERS);
      return;
    }
  search_data_struct_p->work_buff_sz = SEARCH_BUFFER_SIZE;

  /* compile the regular expression */
  search_data_struct_p->the_text = (REGTEXT *) buffer;
  search_data_struct_p->search_lang = search_type;
  search_data_struct_p->search_result = 0;
  search_data_struct_p->search_cmd = REG_COMPILE_CMD;
  if ((reg_search (search_data_struct_p) == 0)
      || (search_data_struct_p->search_result != 0))
    {
      free_and_init (search_data_struct_p->regx);
      free_and_init (search_data_struct_p->work_buff);
      free_and_init (search_data_struct_p);
      esm_set_error (COULD_NOT_COMPILE_REGULAR_EXPRESSION);
      return;
    }
  /* initialize the working buffer */
  search_data_struct_p->search_cmd = REG_RESET_CMD;
  if ((reg_search (search_data_struct_p) == 0)
      || (search_data_struct_p->search_result != 0))
    {
      free_and_init (search_data_struct_p->regx);
      free_and_init (search_data_struct_p->work_buff);
      free_and_init (search_data_struct_p);
      esm_set_error (COULD_NOT_RESET_WORKING_BUFFER);
      return;
    }

  /* find (and cache) the current position of the GLO data */
  if (db_send (esm_glo_object_p, GLO_METHOD_POSITION, &return_value) != 0)
    {
      free_and_init (search_data_struct_p->regx);
      free_and_init (search_data_struct_p->work_buff);
      free_and_init (search_data_struct_p);
      esm_set_error (SEARCH_ERROR_ON_POSITION_CACHE);
      return;
    }
  current_position = DB_GET_INTEGER (&return_value);

  /* start read/search loop */
  db_make_int (&value1, SEARCH_BUFFER_SIZE);
  db_make_varchar (&value2, DB_MAX_VARCHAR_PRECISION, buffer,
		   SEARCH_BUFFER_SIZE);
  search_data_struct_p->search_cmd = REG_MATCH_CMD;
  search_data_struct_p->text_offset = 0;
  buffer_start = 0;
  do
    {
      if (db_send
	  (esm_glo_object_p, GLO_METHOD_READ, &return_value, &value1,
	   &value2) != 0)
	{
	  free_and_init (search_data_struct_p->regx);
	  free_and_init (search_data_struct_p->work_buff);
	  free_and_init (search_data_struct_p);
	  esm_set_error (SEARCH_ERROR_ON_DATA_READ);
	  return;
	}
      read_length = DB_GET_INTEGER (&return_value);
      if (read_length >= 0)
	{
	  if (read_length == 0)
	    {
	      search_data_struct_p->text_length = 0;
	    }
	  else
	    {
	      search_data_struct_p->text_length = read_length;
	    }
	  search_data_struct_p->search_result = 0;
	  if (reg_search (search_data_struct_p) == 0)
	    {
	      free_and_init (search_data_struct_p->regx);
	      free_and_init (search_data_struct_p->work_buff);
	      free_and_init (search_data_struct_p);
	      esm_set_error (SEARCH_ERROR_DURING_LOOKUP);
	      return;
	    }
	  else
	    {
	      if (search_data_struct_p->search_result == REG_MATCH)
		{
		  db_make_int (return_argument_p,
			       current_position + buffer_start +
			       search_data_struct_p->match_offset);
		  break;
		}
	    }
	}
      buffer_start += read_length;
    }
  while (read_length > 0);

  if (DB_GET_INTEGER (return_argument_p) >= 0)
    {
      db_make_int (&value1, current_position + buffer_start +
		   search_data_struct_p->match_offset +
		   search_data_struct_p->match_length);
    }
  else
    {
      db_make_int (&value1, current_position);
    }

  if (db_send (esm_glo_object_p, GLO_METHOD_SEEK, &return_value, &value1)
      != 0)
    {
      esm_set_error (SEARCH_ERROR_REPOSITIONING_POINTER);
      db_make_int (return_argument_p, -1);
    }

  free_and_init (search_data_struct_p->regx);
  free_and_init (search_data_struct_p->work_buff);
  free_and_init (search_data_struct_p);
#endif
}

/*
 * esm_Glo_like_search() - searches the glo for a text string
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  search_for_object_p(in) : search string
 *
 * Note : searches the glo for a text string. If the string is found,
 *        the offset into the GLO where the string is located will be
 *        the return argument. If the text string was not found, the
 *        return value will be < 0.
 */

void
esm_Glo_like_search (DB_OBJECT * esm_glo_object_p,
		     DB_VALUE * return_argument_p,
		     DB_VALUE * search_for_object_p)
{
  esm_search (esm_glo_object_p, return_argument_p, search_for_object_p,
	      2 /*REG_LIKE */ , 0);
}

/*
 * esm_Glo_reg_search() - searches the glo for a text string in the GREP fashion
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  search_for_object_p(in) : search string
 *
 * Note : If the string is found, the offset into the GLO where the
 *        string is located will be the return argument. If the
 *        text string was not found, the return value will be < 0.
 *
 */

void
esm_Glo_reg_search (DB_OBJECT * esm_glo_object_p,
		    DB_VALUE * return_argument_p,
		    DB_VALUE * search_for_object_p)
{
  esm_search (esm_glo_object_p, return_argument_p, search_for_object_p,
	      0 /*REG_GREP */ , 0);
}

/*
 * esm_Glo_binary_search() - searches the glo for a binary value
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  search_for_object_p(in) : search string
 *  search_length(in) :
 *
 * Note : If the binary value is found, the offset into the GLO where
 *        the binary value is located will be the return argument.
 *        If the binary value was not found, the return value will be < 0.
 *
 */

void
esm_Glo_binary_search (DB_OBJECT * esm_glo_object_p,
		       DB_VALUE * return_argument_p,
		       DB_VALUE * search_for_object_p,
		       DB_VALUE * search_length_p)
{
  int search_length;

  db_make_int (return_argument_p, -1);	/* error return value */

  if ((search_length_p == NULL)
      || (DB_VALUE_TYPE (search_length_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  search_length = DB_GET_INTEGER (search_length_p);

  esm_search (esm_glo_object_p, return_argument_p, search_for_object_p,
	      4 /*REG_BIN */ , search_length);
}

/*
 * esm_Glo_set_error() - set multimedia error code
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *  error_value_p(in) : the multimedia error code
 *
 */

void
esm_Glo_set_error (DB_OBJECT * esm_glo_object_p,
		   DB_VALUE * return_argument_p, DB_VALUE * error_value_p)
{
  db_make_int (return_argument_p, -1);	/* error return value */

  if ((error_value_p == NULL)
      || (DB_VALUE_TYPE (error_value_p) != DB_TYPE_INTEGER))
    {
      esm_set_error (INVALID_INTEGER_INPUT_ARGUMENT);
      return;
    }

  mm_Error = DB_GET_INTEGER (error_value_p);
  db_make_int (return_argument_p, mm_Error);
}

/*
 * esm_Glo_get_error() - return the last value set in the multimedia error code
 *      return: none
 *  esm_glo_object_p(in) : the glo object
 *  return_argument_p(out) : return value of this method
 *
 */

void
esm_Glo_get_error (DB_OBJECT * esm_glo_object_p, DB_VALUE * return_argument_p)
{
  db_make_int (return_argument_p, mm_Error);
}

/*
 * esm_Glo_create_lo() - creates a new instance of a glo (LO)
 *      return: none
 *  esm_glo_class(in) : the glo class object
 *  return_argument_p(out) : return the new instance
 *
 */

void
esm_Glo_create_lo (DB_OBJECT * esm_glo_class_p, DB_VALUE * return_argument_p)
{
  DB_VALUE value;

  db_make_null (&value);
  esm_Glo_create (esm_glo_class_p, return_argument_p, &value);
}

/*
 * esm_Glo_import_lo() - creates a new instance of a glo (LO), loads the LO from
 *                       the pathname argument
 *      return: none
 *  esm_glo_class_p(in) : the glo class object
 *  return_argument_p(out) : returns the new LO
 *  path_name_p(in) : the path name of external file
 *
 */

void
esm_Glo_import_lo (DB_OBJECT * esm_glo_class_p,
		   DB_VALUE * return_argument_p, DB_VALUE * path_name_p)
{
  DB_VALUE value;
  DB_OBJECT *lo_instance_p;

  db_make_null (&value);
  esm_Glo_create (esm_glo_class_p, return_argument_p, &value);
  lo_instance_p = DB_GET_OBJECT (return_argument_p);
  if (lo_instance_p != NULL)
    {
      esm_Glo_copy_from (lo_instance_p, &value, path_name_p);
      if (db_send (lo_instance_p, GLO_METHOD_INITIALIZE, &value) == 0)
	{
	  db_make_object (&value, lo_instance_p);
	}
    }
}

/*
 * esm_Glo_create_fbo() - creates a new instance of a glo (FBO)
 *      return: none
 *  esm_glo_class_p(in) : the glo class object
 *  return_argument_p(out) :  return the new instance
 *  path_name_p(in) : the path name of external file
 *
 */

void
esm_Glo_create_fbo (DB_OBJECT * esm_glo_class_p,
		    DB_VALUE * return_argument_p, DB_VALUE * path_name_p)
{
  esm_Glo_create (esm_glo_class_p, return_argument_p, path_name_p);
}

void
esm_load_esm_classes (void)
{
  DB_METHOD_LINK Static_methods[] = {
    {
     "esm_Glo_create", (METHOD_LINK_FUNCTION) esm_Glo_create},
    {
     "esm_Glo_create_lo", (METHOD_LINK_FUNCTION) esm_Glo_create_lo},
    {
     "esm_Glo_import_lo", (METHOD_LINK_FUNCTION) esm_Glo_import_lo},
    {
     "esm_Glo_create_fbo", (METHOD_LINK_FUNCTION) esm_Glo_create_fbo},
    {
     "esm_Glo_init", (METHOD_LINK_FUNCTION) esm_Glo_init},
    {
     "esm_Glo_size", (METHOD_LINK_FUNCTION) esm_Glo_size},
    {
     "esm_Glo_compress", (METHOD_LINK_FUNCTION) esm_Glo_compress},
    {
     "esm_Glo_destroy", (METHOD_LINK_FUNCTION) esm_Glo_destroy},
    {
     "esm_Glo_append", (METHOD_LINK_FUNCTION) esm_Glo_append},
    {
     "esm_Glo_truncate", (METHOD_LINK_FUNCTION) esm_Glo_truncate},
    {
     "esm_Glo_pathname", (METHOD_LINK_FUNCTION) esm_Glo_pathname},
    {
     "esm_Glo_full_pathname", (METHOD_LINK_FUNCTION) esm_Glo_full_pathname},
    {
     "esm_Glo_delete", (METHOD_LINK_FUNCTION) esm_Glo_delete},
    {
     "esm_Glo_insert", (METHOD_LINK_FUNCTION) esm_Glo_insert},
    {
     "esm_Glo_seek", (METHOD_LINK_FUNCTION) esm_Glo_seek},
    {
     "esm_Glo_write", (METHOD_LINK_FUNCTION) esm_Glo_write},
    {
     "esm_Glo_read", (METHOD_LINK_FUNCTION) esm_Glo_read},
    {
     "esm_Glo_print_read", (METHOD_LINK_FUNCTION) esm_Glo_print_read},
    {
     "esm_Glo_copy_to", (METHOD_LINK_FUNCTION) esm_Glo_copy_to},
    {
     "esm_Glo_copy_from", (METHOD_LINK_FUNCTION) esm_Glo_copy_from},
    {
     "esm_Glo_position", (METHOD_LINK_FUNCTION) esm_Glo_position},
    {
     "esm_Glo_like_search", (METHOD_LINK_FUNCTION) esm_Glo_like_search},
    {
     "esm_Glo_reg_search", (METHOD_LINK_FUNCTION) esm_Glo_reg_search},
    {
     "esm_Glo_binary_search", (METHOD_LINK_FUNCTION) esm_Glo_binary_search},
    {
     "esm_Glo_get_error", (METHOD_LINK_FUNCTION) esm_Glo_get_error},
    {
     "esm_Glo_set_error", (METHOD_LINK_FUNCTION) esm_Glo_set_error},
    {
     "Glo_create_holder", (METHOD_LINK_FUNCTION) Glo_create_holder},
    {
     "Glo_lock_holder", (METHOD_LINK_FUNCTION) Glo_lock_holder},
    {
     NULL, NULL}
  };

  db_link_static_methods (Static_methods);
}
