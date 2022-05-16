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
  char buffer[LINE_MAX];

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
  while (fgets ((char *) buffer, LINE_MAX, input_file) != NULL)
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
  const char *dot = NULL;
  int len = 0;

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
      const char *class_name_p = NULL;
      const char *class_name_only = NULL;
      char owner_name[DB_MAX_USER_LENGTH] = { '\0' };

      if (class_names[i] == NULL || (len = STATIC_CAST (int, strlen (class_names[i]))) == 0)
	{
	  goto error;
	}

      if (utility_check_class_name (class_names[i]) != NO_ERROR)
	{
	  goto error;
	}

      sm_qualifier_name (class_names[i], owner_name, DB_MAX_USER_LENGTH);
      class_name_only = sm_remove_qualifier_name (class_names[i]);
      if (strcasecmp (owner_name, "dba") == 0 && sm_check_system_class_by_name (class_name_only))
	{
	  class_name_p = class_name_only;
	}
      else
	{
	  class_name_p = class_names[i];
	}

      sm_user_specified_name (class_name_p, downcase_class_name, SM_MAX_IDENTIFIER_LENGTH);

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
  char buffer[LINE_MAX];
  char **class_names = NULL;
  int num_class = 0;
  int len = 0, sub_len = 0;
  const char *dot = NULL;

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

  class_names = (char **) calloc (num_class, DB_SIZEOF (char *));
  if (class_names == NULL)
    {
      if (input_file != nullptr)
	{
	  fclose (input_file);
	}
      return ER_FAILED;
    }

  for (i = 0; i < num_class; ++i)
    {
      if (fgets ((char *) buffer, LINE_MAX, input_file) == NULL)
	{
	  status = ER_FAILED;
	  goto end;
	}

      trim (buffer);
      len = STATIC_CAST (int, strlen (buffer));
      sub_len = len;

      if (len < 1)
	{
	  status = ER_FAILED;
	  goto end;
	}

      if (utility_check_class_name (buffer) != NO_ERROR)
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
