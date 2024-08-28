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
#include "memory_hash.h"
#include "es_common.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
  else if (!strncmp (uri, ES_POSIX_PATH_PREFIX, sizeof (ES_POSIX_PATH_PREFIX) - 1))
    {
      return ES_POSIX;
    }
  else if (!strncmp (uri, ES_LOCAL_PATH_PREFIX, sizeof (ES_LOCAL_PATH_PREFIX) - 1))
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
