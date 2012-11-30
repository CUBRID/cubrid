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
 * get_shard_id.c -
 */

#ident "$Id$"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <strings.h>
#include "shard_key.h"


#define DEFAULT_NUM_KEY			1
#define MAX_SHARD_KEY			2
#define SHARD_KEY_COLUMN_LEN	32
#define SHARD_KEY_RANGE_MAX		256
#define NAME_MAX				256
#define PATH_MAX				256
#define LINE_MAX 				2048


/* SHARD KEY */
typedef struct t_shard_key_range T_SHARD_KEY_RANGE;
struct t_shard_key_range
{
  int key_index;
  int range_index;

  int min;
  int max;
  int shard_id;
};

typedef struct t_shard_key T_SHARD_KEY;
struct t_shard_key
{
  char key_column[SHARD_KEY_COLUMN_LEN];
  int num_key_range;
  T_SHARD_KEY_RANGE range[SHARD_KEY_RANGE_MAX];
};

typedef struct t_shm_shard_key T_SHM_SHARD_KEY;
struct t_shm_shard_key
{
  int num_shard_key;
  T_SHARD_KEY shard_key[1];
};


int shard_key_modular = 256;
FN_GET_SHARD_KEY fn_get_shard_key = NULL;
void *handle = NULL;


int
load_shard_key_function (const char *library_name, const char *function_name)
{
  handle = dlopen (library_name, RTLD_NOW | RTLD_GLOBAL);

  if (handle == NULL)
    {
      return -1;
    }

  dlerror ();
  fn_get_shard_key = dlsym (handle, function_name);

  if (fn_get_shard_key == NULL)
    {
      return -1;
    }
  return 0;
}

void
close_shard_key_function (void)
{
  fn_get_shard_key = NULL;
  if (handle != NULL)
    {
      dlclose (handle);
    }
  handle = NULL;
}

int
fn_get_shard_key_default (const char *key_column, T_SHARD_U_TYPE type,
			  const void *value, int value_len)
{
  int modular_key;

  if (value == NULL)
    {
      return ERROR_ON_ARGUMENT;
    }

  modular_key = shard_key_modular;
  if (modular_key < 0)
    {
      return ERROR_ON_MAKE_SHARD_KEY;
    }

  switch (type)
    {
    case SHARD_U_TYPE_INT:
      {
	unsigned int ival;
	ival = (unsigned int) (*(unsigned int *) value);
	return ival % modular_key;
      }
      break;
    case SHARD_U_TYPE_STRING:
      {
	unsigned char c;
	c = (unsigned char) (((unsigned char *) value)[0]);
	return c % modular_key;
      }
      break;
    default:
      return ERROR_ON_ARGUMENT;
    }

  return ERROR_ON_MAKE_SHARD_KEY;
}

int
set_fn_get_shard_key (char *library_name, char *function_name)
{
  int error;
  void *handle;

  if (strncmp (library_name, "", PATH_MAX) != 0 &&
      strncmp (function_name, "", NAME_MAX) != 0)
    {
      error = load_shard_key_function (library_name, function_name);
      if (error < 0)
	{
	  close_shard_key_function ();
	  printf ("fail load library\n");
	  return -1;
	}
      return 0;
    }

  fn_get_shard_key = fn_get_shard_key_default;
  return 0;
}

int
find_shard_id_by_int_key (int value, const char *key_column)
{
  int shard_key_id = -1;

  shard_key_id = (*fn_get_shard_key) (key_column, SHARD_U_TYPE_INT,
				      &value, sizeof (int));

  return shard_key_id;
}

int
find_shard_id_by_string_key (char *value, int len, const char *key_column)
{
  int shard_key_id = -1;

  shard_key_id = (*fn_get_shard_key) (key_column, SHARD_U_TYPE_STRING,
				      value, len);

  return shard_key_id;
}

T_SHARD_KEY *
bsearch_key (T_SHM_SHARD_KEY * shm_key_p, const char *keycolumn)
{
  int min, mid, max;
  int result;
  T_SHARD_KEY *key_p;

  min = 0;
  max = shm_key_p->num_shard_key - 1;

  do
    {
      mid = (min + max) / 2;
      key_p = &(shm_key_p->shard_key[mid]);
      result = strcasecmp (keycolumn, key_p->key_column);

      if (result < 0)
	{
	  max = mid - 1;
	}
      else if (result > 0)
	{
	  min = mid + 1;
	}
      else
	{
	  return key_p;
	}
    }
  while (min <= max);

  return NULL;
}

T_SHARD_KEY_RANGE *
bsearch_range (T_SHARD_KEY * key_p, unsigned int hash_res)
{
  int min, mid, max;
  int result;
  T_SHARD_KEY_RANGE *range_p;

  min = 0;
  max = key_p->num_key_range - 1;

  do
    {
      mid = (min + max) / 2;
      range_p = &(key_p->range[mid]);

      if ((int) hash_res < range_p->min)
	{
	  max = mid - 1;
	}
      else if (hash_res > (unsigned int) range_p->max)
	{
	  min = mid + 1;
	}
      else
	{
	  return range_p;
	}
    }
  while (min <= max);

  return NULL;
}

int
find_shard_id (T_SHM_SHARD_KEY * shm_key_p, const char *key,
	       unsigned int hash_res)
{
  T_SHARD_KEY_RANGE *range_p = NULL;
  T_SHARD_KEY *key_p = bsearch_key (shm_key_p, key);
  if (key_p == NULL)
    {
      return -1;
    }

  range_p = bsearch_range (key_p, hash_res);
  if (range_p == NULL)
    {
      return -1;
    }

  return range_p->shard_id;
}

void
key_resize (T_SHM_SHARD_KEY ** shm_key_pp, int max_key)
{
  int mem_size = sizeof (int) + (max_key * sizeof (T_SHARD_KEY));

  assert (shm_key_pp);

  if (*shm_key_pp)
    {
      (*shm_key_pp) = (T_SHM_SHARD_KEY *) realloc ((*shm_key_pp), mem_size);
    }
  else
    {
      (*shm_key_pp) = (T_SHM_SHARD_KEY *) malloc (mem_size);
    }

  return;
}

char *
trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    {
      return (str);
    }

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    {
    }

  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  for (p = s; *p != '\0'; p++)
    {
    }

  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    {
    }

  *++p = '\0';

  if (s != str)
    memcpy (str, s, strlen (s) + 1);

  return (str);
}

T_SHM_SHARD_KEY *
read_key (const char *filepath)
{
  int error = 0;
  int nargs;
  int idx_key, idx_range, max_key;
  char line[LINE_MAX], *p;
  char section[LINE_MAX];
  int len;
  FILE *file = NULL;

  char key_column[SHARD_KEY_COLUMN_LEN];

  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHARD_KEY *key_p = NULL;
  T_SHARD_KEY_RANGE *range_p = NULL;

  file = fopen (filepath, "r");
  if (file == NULL)
    {
      goto error_return;
    }

  max_key = DEFAULT_NUM_KEY;
  key_resize (&shm_key_p, max_key);
  if (shm_key_p == NULL)
    {
      goto error_return;
    }
  shm_key_p->num_shard_key = 0;

  idx_key = idx_range = 0;
  key_column[0] = '\0';
  while (fgets (line, LINE_MAX - 1, file) != NULL)
    {
      trim (line);

      p = strchr (line, '#');
      if (p)
	{
	  *p = '\0';
	}

      len = strlen (line);
      if (line[0] == '\0' || len <= 0)
	{
	  continue;
	}
      else if (line[0] == '[' && line[len - 1] == ']')
	{
	  nargs = sscanf (line, "[%%%[^]]", section);
	  if (nargs == 1)
	    {
	      trim (section);
	      if (strncasecmp (section, key_column,
			       SHARD_KEY_COLUMN_LEN) != 0)
		{
		  strncpy (key_column, section, sizeof (key_column) - 1);

		  shm_key_p->num_shard_key++;
		  idx_key = shm_key_p->num_shard_key - 1;
		  idx_range = 0;
		}
	    }
	  continue;
	}

      if (shm_key_p->num_shard_key > MAX_SHARD_KEY)
	{
	  shm_key_p->num_shard_key = MAX_SHARD_KEY;
	  break;
	}

      if (shm_key_p->num_shard_key > max_key)
	{
	  shm_key_p->num_shard_key = max_key;
	  break;
	}

      if (key_column[0] == '\0')
	{
	  continue;
	}

      key_p = &(shm_key_p->shard_key[idx_key]);
      strncpy (key_p->key_column, key_column, sizeof (key_p->key_column) - 1);

      if (idx_range >= SHARD_KEY_RANGE_MAX)
	{
	  continue;
	}

      assert (idx_range >= 0 && idx_range < SHARD_KEY_RANGE_MAX);
      range_p = (T_SHARD_KEY_RANGE *) & ((key_p->range[idx_range]));
      nargs = sscanf (line, "%d %d %d", &range_p->min,
		      &range_p->max, &range_p->shard_id);

      range_p->key_index = idx_key;
      range_p->range_index = idx_range;

      if (nargs != 3)
	{
	  continue;
	}

      key_p->num_key_range = ++idx_range;
    }

  return shm_key_p;

error_return:
  if (shm_key_p)
    {
      free (shm_key_p);
    }

  if (file != NULL)
    {
      fclose (file);
    }

  return NULL;
}

int
key_range_comp (const void *p1, const void *p2)
{
  T_SHARD_KEY_RANGE *arg1 = (T_SHARD_KEY_RANGE *) p1;
  T_SHARD_KEY_RANGE *arg2 = (T_SHARD_KEY_RANGE *) p2;

  if (arg1->min > arg2->min)
    {
      return 1;
    }
  else if (arg1->min == arg2->min)
    {
      return 0;
    }
  else
    {
      return -1;
    }
}

void
sort_key (T_SHM_SHARD_KEY * shm_key_p)
{
  int i;
  T_SHARD_KEY *key_p;

  for (i = 0; i < shm_key_p->num_shard_key; i++)
    {
      key_p = &shm_key_p->shard_key[i];
      qsort ((void *) key_p->range, key_p->num_key_range,
	     sizeof (T_SHARD_KEY_RANGE), key_range_comp);
    }
  return;
}

int
is_integer (const char *value)
{
  const char *p = value;

  while (*p != '\0')
    {
      if (*p > 47 && *p < 58)
	{
	  p++;
	}
      else
	{
	  return 0;
	}
    }
  return 1;
}

int
main (int argc, char **argv)
{
  int error, optchar;
  int mod_flag = 0;
  int lib_flag = 0;
  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHARD_KEY *key_p = NULL;
  int shard_key_id = 0;
  int shard_id = 0;
  const char *key_column;
  char *key_value;
  char function_name[NAME_MAX] = { 0 };
  char key_file[PATH_MAX] = { 0 };
  char lib_file[PATH_MAX] = { 0 };
  extern char *optarg;
  char buf[1024];
  int buf_len = 0;

  if (argc != 6 && argc != 8)
    {
      goto usage;
    }

  while ((optchar = getopt (argc, argv, "c:m:l:f:")) != EOF)
    {
      switch (optchar)
	{
	case 'c':
	  strncpy (key_file, optarg, PATH_MAX);
	  break;
	case 'm':
	  shard_key_modular = atoi (optarg);
	  if (shard_key_modular < 0)
	    {
	      printf ("invalid shard key modular\n");
	      return -1;
	    }
	  mod_flag = 1;
	  break;
	case 'l':
	  strncpy (lib_file, optarg, PATH_MAX);
	  lib_flag = 1;
	  break;
	case 'f':
	  strncpy (function_name, optarg, NAME_MAX);
	  break;
	default:
	  break;
	}
    }

  if (mod_flag == 1 && lib_flag == 1)
    {
      goto usage;
    }

  key_value = argv[argc - 1];

  shm_key_p = read_key (key_file);
  if (shm_key_p == NULL)
    {
      printf ("file open error\n");
      return -1;
    }
  sort_key (shm_key_p);

  error = set_fn_get_shard_key (lib_file, function_name);
  if (error < 0)
    {
      goto usage;
    }

  key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[0]));
  key_column = key_p->key_column;

  if (is_integer (key_value))
    {
      shard_key_id = find_shard_id_by_int_key (atoi (key_value), key_column);
    }
  else
    {
      shard_key_id = find_shard_id_by_string_key (key_value,
						  strlen (key_value),
						  key_column);
    }

  if (shard_key_id < 0)
    {
      printf ("invalid shard key value\n");
      return -1;
    }

  shard_id = find_shard_id (shm_key_p, (char *) key_column, shard_key_id);

  buf_len += sprintf (buf + buf_len, " @shard getid\n");
  buf_len += sprintf (buf + buf_len, " SHARD_ID : %d, ", shard_id);
  buf_len += sprintf (buf + buf_len, "SHARD_KEY : %s", key_value);
  buf_len += sprintf (buf + buf_len, ", KEY_COLUMN : %s\n", key_column);

  printf ("%s", buf);
  return 0;

usage:
  printf ("Default hash function :\n");
  printf ("     %s -c SHARD_KEY_FILE_PATH -m SHARD_KEY_MODULAR "
	  "SHARD_KEY\n", argv[0]);
  printf ("User defined hash function :\n");
  printf ("     %s -c SHARD_KEY_FILE_PATH -l SHARD_KEY_LIBRARY_PATH "
	  "-f SHARD_KEY_FUNCTION_NAME SHARD_KEY\n", argv[0]);
  return -1;
}
