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


#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "porting.h"
#include "dbtype.h"
#include "load_object.h"
#include "db.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "object_accessor.h"
#include "set_object.h"
#include "message_catalog.h"
#include "network_interface_cl.h"
#include "server_interface.h"
#include "system_parameter.h"
#include "utility.h"
#include "authenticate.h"
#include "transaction_cl.h"
#include "execute_schema.h"
#include "object_primitive.h"

int get_class_mops (char **class_names, int num_class, MOP ** class_list, int *num_class_list);
int get_class_mops_from_file (const char *input_filename, MOP ** class_list, int *num_class_mops);

/*
 * get_num_requested_class - Get the number of class from specified
 * input file
 *    return: error code
 *    input_filename(in): input file name
 *    num_class(out) : pointer to returned number of classes
 */
static int
get_num_requested_class (const char *input_filename, int *num_class)
{
  FILE *input_file;
  char buffer[DB_MAX_IDENTIFIER_LENGTH];

  if (input_filename == NULL || num_class == NULL)
    {
      return ER_FAILED;
    }

  input_file = fopen (input_filename, "r");
  if (input_file == NULL)
    {
      perror (input_filename);
      return ER_FAILED;
    }

  *num_class = 0;
  while (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH, input_file) != NULL)
    {
      (*num_class)++;
    }

  fclose (input_file);

  return NO_ERROR;
}

/*
 * get_class_mops - Get the list of mops of specified classes
 *    return: error code
 *    class_names(in): the names of the classes
 *    num_class(in): the number of classes
 *    class_list(out): pointer to returned list of mops
 *    num_class_list(out): pointer to returned number of mops
 */
int
get_class_mops (char **class_names, int num_class, MOP ** class_list, int *num_class_list)
{
  int i;
  char downcase_class_name[SM_MAX_IDENTIFIER_LENGTH];
  DB_OBJECT *class_ = NULL;

  if (class_names == NULL || num_class <= 0 || class_list == NULL || num_class_list == NULL)
    {
      return ER_FAILED;
    }

  *num_class_list = 0;
  *class_list = (DB_OBJECT **) malloc (DB_SIZEOF (DB_OBJECT *) * (num_class));
  if (*class_list == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < num_class; ++i)
    {
      (*class_list)[i] = NULL;
    }

  for (i = 0; i < num_class; i++)
    {
      if (class_names[i] == NULL || strlen (class_names[i]) == 0)
	{
	  goto error;
	}

      sm_downcase_name (class_names[i], downcase_class_name, SM_MAX_IDENTIFIER_LENGTH);

      class_ = locator_find_class (downcase_class_name);
      if (class_ != NULL)
	{
	  (*class_list)[(*num_class_list)] = class_;
	  (*num_class_list)++;
	}
      else
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMPACTDB, COMPACTDB_MSG_CLASS),
		  downcase_class_name);

	  printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMPACTDB, COMPACTDB_MSG_INVALID_CLASS));
	}
    }

  return NO_ERROR;

error:
  if (*class_list)
    {
      free (*class_list);
      *class_list = NULL;
    }

  if (num_class_list)
    {
      *num_class_list = 0;
    }

  return ER_FAILED;
}

/*
 * get_class_mops_from_file - Get a list of class mops from specified file
 *    return: error code
 *    input_filename(in): input file name
 *    class_list(out): pointer to returned list of class mops
 *    num_class_mops(out): pointer to returned number of mops
 */
int
get_class_mops_from_file (const char *input_filename, MOP ** class_list, int *num_class_mops)
{
  int status = NO_ERROR;
  int i = 0;
  FILE *input_file;
  char buffer[DB_MAX_IDENTIFIER_LENGTH];
  char **class_names = NULL;
  int num_class = 0;
  int len = 0;
  char *ptr = NULL;

  if (input_filename == NULL || class_list == NULL || num_class_mops == NULL)
    {
      return ER_FAILED;
    }

  *class_list = NULL;
  *num_class_mops = 0;
  if (get_num_requested_class (input_filename, &num_class) != NO_ERROR || num_class == 0)
    {
      return ER_FAILED;
    }

  input_file = fopen (input_filename, "r");
  if (input_file == NULL)
    {
      perror (input_filename);
      return ER_FAILED;
    }

  class_names = (char **) malloc (DB_SIZEOF (char *) * num_class);
  if (class_names == NULL)
    {
      return ER_FAILED;
    }
  for (i = 0; i < num_class; i++)
    {
      class_names[i] = NULL;
    }

  for (i = 0; i < num_class; ++i)
    {
      if (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH, input_file) == NULL)
	{
	  status = ER_FAILED;
	  goto end;
	}

      ptr = strchr (buffer, '\n');
      if (ptr)
	{
	  len = CAST_BUFLEN (ptr - buffer);
	}
      else
	{
	  len = (int) strlen (buffer);
	}

      if (len < 1)
	{
	  status = ER_FAILED;
	  goto end;
	}

      class_names[i] = (char *) malloc (DB_SIZEOF (char) * (len + 1));
      if (class_names[i] == NULL)
	{
	  status = ER_FAILED;
	  goto end;
	}

      strncpy (class_names[i], buffer, len);
      class_names[i][len] = 0;
    }

  status = get_class_mops (class_names, num_class, class_list, num_class_mops);

end:

  if (input_file)
    {
      fclose (input_file);
    }

  if (class_names)
    {
      for (i = 0; i < num_class; i++)
	{
	  free_and_init (class_names[i]);
	}

      free (class_names);
      class_names = NULL;
    }

  return status;
}
