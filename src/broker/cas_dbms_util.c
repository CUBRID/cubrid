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
 * cas_dbms_util.c -
 */

#include "assert.h"

#include "porting.h"
#include "environment_variable.h"
#include "cas.h"
#include "cas_execute.h"
#include "cas_error.h"
#include "broker_filename.h"
#if defined(CAS_FOR_ORACLE)
#include "cas_oracle.h"
#elif defined(CAS_FOR_MYSQL)
#include "cas_mysql.h"
#endif

#ident "$Id$"

/* base/environment_variable.c */
/* available root directory symbols; NULL terminated array */
static const char envvar_Prefix_name[] = "CUBRID";
static const char *envvar_Prefix = NULL;
static const char *envvar_Root = NULL;

#define _ENVVAR_MAX_LENGTH      255

static char *cfg_pop_token (char *str_p, char **token_p);
static char *cfg_next_char (char *str_p);

void
cfg_free_dbinfo_all (DB_INFO * databases)
{
  DB_INFO *db_info_p, *next_info_p;

  for (db_info_p = databases, next_info_p = NULL; db_info_p != NULL;
       db_info_p = next_info_p)
    {
      next_info_p = db_info_p->next;

      if (db_info_p->alias != NULL)
	{
	  free_and_init (db_info_p->alias);
	}
      if (db_info_p->dbinfo != NULL)
	{
	  free_and_init (db_info_p->dbinfo);
	}
      free_and_init (db_info_p);
    }
}

int
cfg_get_dbinfo (char *alias, char *dbinfo)
{
  FILE *file;
  char *save, *token;
  char delim[] = "|";
  char filename[BROKER_PATH_MAX];
  char line[DBINFO_MAX_LENGTH];

  if (shm_appl->db_connection_file[0] == '\0')
    {
#if defined(CAS_FOR_ORACLE)
      get_cubrid_file (FID_CAS_FOR_ORACLE_DBINFO, filename, BROKER_PATH_MAX);
#elif defined(CAS_FOR_MYSQL)
      get_cubrid_file (FID_CAS_FOR_MYSQL_DBINFO, filename, BROKER_PATH_MAX);
#endif
    }
  else
    {
      if (IS_ABS_PATH (shm_appl->db_connection_file))
	{
	  strncpy (filename, shm_appl->db_connection_file,
		   BROKER_PATH_MAX - 1);
	}
      else
	{
	  envvar_confdir_file (filename, BROKER_PATH_MAX,
			       shm_appl->db_connection_file);
	}
    }

  file = fopen (filename, "r");
  if (file == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_OPEN_FILE, CAS_ERROR_INDICATOR);
    }

  while (fgets (line, DBINFO_MAX_LENGTH - 1, file) != NULL)
    {
      if (line[0] == '\0' || line[0] == '#')
	{
	  continue;
	}
      token = strtok_r (line, delim, &save);
      if (token == NULL || strcmp (token, alias) != 0)
	{
	  continue;
	}
      token = strtok_r (NULL, delim, &save);
      if (token == NULL)
	{
	  continue;
	}

      strcpy (dbinfo, token);
      fclose (file);
      return 0;
    }

  fclose (file);
  return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
}

int
cfg_read_dbinfo (DB_INFO ** db_info_p)
{
  FILE *file;
  char filename[BROKER_PATH_MAX];
  char line[DBINFO_MAX_LENGTH];
  char *str = NULL;
  DB_INFO *databases, *db, *last;

  databases = last = NULL;

  if (shm_appl->db_connection_file[0] == '\0')
    {
#if defined(CAS_FOR_ORACLE)
      get_cubrid_file (FID_CAS_FOR_ORACLE_DBINFO, filename, BROKER_PATH_MAX);
#elif defined(CAS_FOR_MYSQL)
      get_cubrid_file (FID_CAS_FOR_MYSQL_DBINFO, filename, BROKER_PATH_MAX);
#endif
    }
  else
    {
      if (IS_ABS_PATH (shm_appl->db_connection_file))
	{
	  strncpy (filename, shm_appl->db_connection_file, BROKER_PATH_MAX);
	  filename[BROKER_PATH_MAX - 1] = 0;
	}
      else
	{
	  envvar_confdir_file (filename, BROKER_PATH_MAX,
			       shm_appl->db_connection_file);
	}
    }

  file = fopen (filename, "r");
  if (file == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_OPEN_FILE, CAS_ERROR_INDICATOR);
    }

  while (fgets (line, DBINFO_MAX_LENGTH - 1, file) != NULL)
    {
      str = cfg_next_char (line);
      if (*str != '\0' && *str != '#')
	{
	  db = (DB_INFO *) malloc (sizeof (DB_INFO));
	  if (db == NULL)
	    {
	      if (databases != NULL)
		{
		  cfg_free_dbinfo_all (databases);
		}
	      *db_info_p = NULL;
	      fclose (file);
	      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
	    }
	  db->next = NULL;
	  str = cfg_pop_token (str, &db->alias);
#if defined(CAS_FOR_ORACLE)
	  str = cfg_pop_token (str, &db->dbinfo);
#elif defined(CAS_FOR_MYSQL)
	  str = cfg_pop_token (str, &db->dbinfo);
#endif
	  if (databases == NULL)
	    {
	      databases = db;
	    }
	  else
	    {
	      last->next = db;
	    }
	  last = db;
#if defined(CAS_FOR_ORACLE)
	  if (db->alias == NULL || db->dbinfo == NULL)
#elif defined(CAS_FOR_MYSQL)
	  if (db->alias == NULL || db->dbinfo == NULL)
#endif
	    {
	      if (databases != NULL)
		{
		  cfg_free_dbinfo_all (databases);
		}
	      *db_info_p = NULL;
	      fclose (file);
	      return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	    }
	}
    }
  fclose (file);

  *db_info_p = databases;
  return 0;
}

/*
 * cfg_find_db_list()
 *    return: database descriptor
 *    dir(in): descriptor list
 *    name(in): database name
 */
DB_INFO *
cfg_find_db_list (DB_INFO * db_info_list_p, const char *name)
{
  DB_INFO *db_info_p, *found_info_p;

  found_info_p = NULL;
  for (db_info_p = db_info_list_p; db_info_p != NULL && found_info_p == NULL;
       db_info_p = db_info_p->next)
    {
      if (strcmp (db_info_p->alias, name) == 0)
	{
	  found_info_p = db_info_p;
	}
    }

  return (found_info_p);
}

/*
 * char_is_delim() - test for a delimiter character
 *   return: non-zero if c is a delimiter character,
 *           0 otherwise.
 *   c (in): the character to be tested
 *   delim (in): the delimiter character
 */
int
char_is_delim (int c, int delim)
{
  return ((c) == delim || (c) == '\t' || (c) == '\r' || (c) == '\n');
}

/* PARSING UTILITIES */
/*
 * cfg_next_char() - Advances the given pointer until a non-whitespace character
 *               or the end of the string are encountered.
 *    return: char *
 *    str_p(in): buffer pointer
 */
static char *
cfg_next_char (char *str_p)
{
  char *p;
  char delim[] = " ";

  p = str_p;
  while (char_is_delim ((int) *p, (int) *delim) && *p != '\0')
    {
      p++;
    }

  return (p);
}

/*
 * cfg_pop_token() - This looks in the buffer for the next token which is define as
 *               a string of characters started by | 
 *    return: char
 *    str_p(in): buffer with tokens
 *    token_p(in/out): returned next token string
 *
 *    Note : When found the token characters are copied into a new string
 *           and returned.
 *           The pointer to the first character following the new token in
 *           the buffer is returned.
 */
static char *
cfg_pop_token (char *str_p, char **token_p)
{
  char *p, *end, *token = NULL;
  char delim[] = "|";
  int length;

  token = NULL;
  p = str_p;
  while (char_is_delim ((int) *p, (int) *delim) && *p != '\0')
    {
      p++;
    }
  end = p;
  while (!char_is_delim ((int) *end, (int) *delim) && *end != '\0')
    {
      end++;
    }

  length = (int) (end - p);

  token = (char *) malloc (length + 1);

  if (length > 0 && token != NULL)
    {
      strncpy (token, p, length);
      token[length] = '\0';
    }

  *token_p = token;
  return (end);
}

UINT64
ntohi64 (UINT64 from)
{
  UINT64 to;
  char *ptr, *vptr;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[7];
  vptr[1] = ptr[6];
  vptr[2] = ptr[5];
  vptr[3] = ptr[4];
  vptr[4] = ptr[3];
  vptr[5] = ptr[2];
  vptr[6] = ptr[1];
  vptr[7] = ptr[0];

  return to;
}

/*
 * char_islower() - test for a lower case character
 *   return: non-zero if c is a lower case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_islower (int c)
{
  return ((c) >= 'a' && (c) <= 'z');
}

/*
 * char_isupper() - test for a upper case character
 *   return: non-zero if c is a upper case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isupper (int c)
{
  return ((c) >= 'A' && (c) <= 'Z');
}

/*
 * char_isalpha() - test for a alphabetic character
 *   return: non-zero if c is a alphabetic character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isalpha (int c)
{
  return (char_islower ((c)) || char_isupper ((c)));
}
