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
 * es_common.c - common utilities for external storage API  (at client and server)
 */

#include "config.h"

#include <string.h>
#include <sys/types.h>
#include <assert.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "stringl.h"
#include "memory_hash.h"
#include "es_common.h"

/*
 * es_get_type -
 *
 * return: enum ES_TYPE
 * uri(in):
 */
ES_TYPE
es_get_type (const char *uri)
{
  if (!strncmp (uri, ES_OWFS_PATH_PREFIX, sizeof (ES_OWFS_PATH_PREFIX) - 1))
    {
      return ES_OWFS;
    }
  else if (!strncmp (uri, ES_POSIX_PATH_PREFIX,
		     sizeof (ES_POSIX_PATH_PREFIX) - 1))
    {
      return ES_POSIX;
    }
  else if (!strncmp (uri, ES_LOCAL_PATH_PREFIX,
		     sizeof (ES_LOCAL_PATH_PREFIX) - 1))
    {
      return ES_LOCAL;
    }
  return ES_NONE;
}

/*
 * es_get_type_string -
 *
 * return: name of ES_TYPE
 * type(in):
 */
const char *
es_get_type_string (ES_TYPE type)
{
  if (type == ES_OWFS)
    {
      return ES_OWFS_PATH_PREFIX;
    }
  else if (type == ES_POSIX)
    {
      return ES_POSIX_PATH_PREFIX;
    }
  else if (type == ES_LOCAL)
    {
      return ES_LOCAL_PATH_PREFIX;
    }
  return "none";
}

/*
 * es_name_hash_func -
 *
 * return:
 * size(in):
 * name(in):
 */
unsigned int
es_name_hash_func (int size, const char *name)
{
  assert (size >= 0);
  return (int) mht_5strhash (name, (unsigned int) size);
}

/*
 * es_get_unique_num -
 */
UINT64
es_get_unique_num (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);

  return tv.tv_sec * 1000000ULL + tv.tv_usec;
}
