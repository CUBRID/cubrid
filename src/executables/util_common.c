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
 * util_common.c - utility common functions
 */

#ident "$Id$"

#include <ctype.h>

#include "config.h"
#include "porting.h"
#include "utility.h"
#include "message_catalog.h"
#include "error_code.h"

static int utility_get_option_index (UTIL_ARG_MAP * arg_map, int arg_ch);

/*
 * utility_initialize() - initialize cubrid library
 *   return: 0 if success, otherwise -1
 */
int
utility_initialize ()
{
  if (msgcat_init () != NO_ERROR)
    {
      fprintf (stderr, "Unable to access system message catalog.\n");
      return ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG;
    }

  return NO_ERROR;
}

/*
 * utility_get_generic_message() - get a string of the generic-utility from the catalog
 *   return: message string
 *   message_index(in): an index of the message string
 */
const char *
utility_get_generic_message (int message_index)
{
  return (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_GENERIC, message_index));
}

/*
 * check_database_name() - check validation of the name of a database
 *   return: error code
 *   name(in): the name of a database
 */
int
check_database_name (const char *name)
{
  int status = NO_ERROR;
  int i = 0;

  if (name[0] == '#')
    {
      status = ER_GENERIC_ERROR;
    }
  else
    {
      for (i = 0; name[i] != 0; i++)
	{
	  if (isspace (name[i]) || name[i] == '/' || name[i] == '\\'
	      || !isprint (name[i]))
	    {
	      status = ER_GENERIC_ERROR;
	      break;
	    }
	}
    }

  if (status == ER_GENERIC_ERROR)
    {
      const char *message =
	utility_get_generic_message (MSGCAT_UTIL_GENERIC_BAD_DATABASE_NAME);
      if (message != NULL)
	{
	  fprintf (stderr, message, name[i], name);
	}
    }
  return status;
}

/*
 * check_volume_name() - check validation of the name of a volume
 *   return: error code
 *   name(in): the name of a volume
 */
int
check_volume_name (const char *name)
{
  int status = NO_ERROR;
  int i = 0;

  if (name == NULL)
    {
      return NO_ERROR;
    }

  if (name[0] == '#')
    {
      status = ER_GENERIC_ERROR;
    }
  else
    {
      for (i = 0; name[i] != 0; i++)
	{
	  if (isspace (name[i]) || name[i] == '/' || name[i] == '\\'
	      || !isprint (name[i]))
	    {
	      status = ER_GENERIC_ERROR;
	      break;
	    }
	}
    }

  if (status == ER_GENERIC_ERROR)
    {
      const char *message =
	utility_get_generic_message (MSGCAT_UTIL_GENERIC_BAD_VOLUME_NAME);
      if (message != NULL)
	{
	  fprintf (stderr, message, name[i], name);
	}
    }
  return status;
}

/*
 * utility_get_option_index() - search an option in the map of arguments
 *   return: an index of a founded option
 *   arg_map(in): the map of arguments
 *   arg_ch(in): the value of an argument
 */
static int
utility_get_option_index (UTIL_ARG_MAP * arg_map, int arg_ch)
{
  int i;

  for (i = 0; arg_map[i].arg_ch; i++)
    {
      if (arg_map[i].arg_ch == arg_ch)
	{
	  return i;
	}
    }
  return -1;
}

/*
 * utility_get_option_int_value() - search an option in the map of arguments
 *      and return that value
 *   return: the value of a searched argument
 *   arg_map(in): the map of arguments
 *   arg_ch(in): the value of an argument
 */
int
utility_get_option_int_value (UTIL_ARG_MAP * arg_map, int arg_ch)
{
  int index = utility_get_option_index (arg_map, arg_ch);
  if (index != -1)
    {
      return ((int) arg_map[index].arg_value);
    }
  return 0;
}

/*
 * get_option_bool_value() - search an option in the map of arguments
 *      and return that value
 *   return: the value of a searched argument
 *   arg_map(in): the map of arguments
 *   arg_ch(in): the value of an argument
 */
bool
utility_get_option_bool_value (UTIL_ARG_MAP * arg_map, int arg_ch)
{
  int index = utility_get_option_index (arg_map, arg_ch);
  if (index != -1)
    {
      if (arg_map[index].arg_value != NULL)
	return true;
    }
  return false;
}

/*
 * get_option_string_value() - search an option in the map of arguments
 *      and return that value
 *   return: the value of a searched argument
 *   arg_map(in): the map of arguments
 *   arg_ch(in): the value of an argument
 */
char *
utility_get_option_string_value (UTIL_ARG_MAP * arg_map, int arg_ch,
				 int index)
{
  int arg_index = utility_get_option_index (arg_map, arg_ch);
  if (arg_index != -1)
    {
      if (arg_ch == OPTION_STRING_TABLE)
	{
	  if (index < arg_map[arg_index].value_info.num_strings)
	    {
	      return (((char **) arg_map[arg_index].arg_value)[index]);
	    }
	}
      else
	{
	  return ((char *) arg_map[arg_index].arg_value);
	}
    }
  return NULL;
}

int
utility_get_option_string_table_size (UTIL_ARG_MAP * arg_map)
{
  int arg_index = utility_get_option_index (arg_map, OPTION_STRING_TABLE);
  if (arg_index != -1)
    {
      return arg_map[arg_index].value_info.num_strings;
    }
  return 0;
}

/*
 * fopen_ex - open a file for variable architecture
 *    return: FILE *
 *    filename(in): path to the file to open
 *    type(in): open type
 */
FILE *
fopen_ex (const char *filename, const char *type)
{
#if defined(SOLARIS)
  size_t r = 0;
  char buf[1024];

  extern size_t confstr (int, char *, size_t);
  r = confstr (_CS_LFS_CFLAGS, buf, 1024);

  if (r > 0)
    return fopen64 (filename, type);
  else
    return fopen (filename, type);
#elif defined(HPUX)
#if _LFS64_LARGEFILE == 1
  return fopen64 (filename, type);
#else
  return fopen (filename, type);
#endif
#elif defined(AIX) || (defined(I386) && defined(LINUX))
  return fopen64 (filename, type);
#else /* NT, ALPHA_OSF, and the others */
  return fopen (filename, type);
#endif
}

/*
 * utility_keyword_search
 */
int
utility_keyword_search (UTIL_KEYWORD * keywords, int *keyval_p,
			char **keystr_p)
{
  UTIL_KEYWORD *keyp;

  if (*keyval_p >= 0 && *keystr_p == NULL)
    {
      /* get keyword string from keyword value */
      for (keyp = keywords; keyp->keyval >= 0; keyp++)
	{
	  if (*keyval_p == keyp->keyval)
	    {
	      *keystr_p = keyp->keystr;
	      return NO_ERROR;
	    }
	}
    }
  else if (*keyval_p < 0 && *keystr_p != NULL)
    {
      /* get keyword value from keyword string */
      for (keyp = keywords; keyp->keystr != NULL; keyp++)
	{
	  if (!strcasecmp (*keystr_p, keyp->keystr))
	    {
	      *keyval_p = keyp->keyval;
	      return NO_ERROR;
	    }
	}
    }
  return ER_FAILED;
}
