/*
 *
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
 * memory_cwrapper.h - managing memory with allocating wrapper functions
 *                     and memory monitor
 */

#ifndef _MEMORY_CWRAPPER_H_
#define _MEMORY_CWRAPPER_H_

#ifdef SERVER_MODE
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

#include "memory_monitor_sr.hpp"

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif

inline size_t
get_alloc_size (void *ptr)
{
  if (ptr == NULL)
    {
      return 0;
    }
  else
    {
      return mmon_get_alloc_size ((char *) ptr);
    }
}

inline void
cub_free (void *ptr)
{
  char *p = (char *) ptr;

  if (is_mem_tracked && ptr != NULL)
    {
      mmon_sub_stat (p);
      assert (malloc_usable_size (p) != 0);
    }
  // XXX: for debug / it will be deleted when the last phase
  //fprintf(stdout, "cub_free called\n");
  free (p);
}

inline void *
cub_alloc (size_t size, const char *file)
{
  void *p = NULL;

  if (is_mem_tracked)
    {
      p = malloc (size + MMON_ALLOC_META_SIZE);
      if (p != NULL)
	{
	  memset (p, 0, size + MMON_ALLOC_META_SIZE);
	  mmon_add_stat ((char *) p, size + MMON_ALLOC_META_SIZE, file);
	}
    }
  else
    {
      p = malloc (size);
    }

  return p;
}

inline void *
cub_calloc (size_t num, size_t size, const char *file)
{
  void *p = NULL;

  if (is_mem_tracked)
    {
      p = malloc (num * size + MMON_ALLOC_META_SIZE);
      if (p != NULL)
	{
	  memset (p, 0, num * size + MMON_ALLOC_META_SIZE);
	  mmon_add_stat ((char *) p, size + MMON_ALLOC_META_SIZE, file);
	}
    }
  else
    {
      p = calloc (num, size);
    }

  return p;
}

inline void *
cub_realloc (void *ptr, size_t size, const char *file)
{
  void *p = NULL;

  if (is_mem_tracked)
    {
      p = malloc (size + MMON_ALLOC_META_SIZE);
      if (p != NULL)
	{
	  memset (p, 0, size + MMON_ALLOC_META_SIZE);
	  mmon_add_stat ((char *) p, size + MMON_ALLOC_META_SIZE, file);

	  if (ptr != NULL)
	    {
	      memcpy (p, ptr, get_alloc_size (ptr));
	      cub_free (ptr);
	    }
	}
    }
  else
    {
      p = realloc (ptr, size);
    }

  return p;
}

inline char *
cub_strdup (const char *str, const char *file)
{
  void *p = NULL;
  char *ret = NULL;

  p = cub_alloc (strlen (str) + 1, file);
  ret = (char *) p;
  memcpy (ret, str, strlen (str) + 1);

  return ret;
}

#define malloc(sz) cub_alloc(sz, __FILE__)
#define calloc(num, sz) cub_calloc(num, sz, __FILE__)
#define realloc(ptr, sz) cub_realloc(ptr, sz, __FILE__)
#define strdup(str) cub_strdup(str, __FILE__)
#define free(ptr) cub_free(ptr)
#endif // SERVER_MODE

#endif // _MEMORY_CWRAPPER_H_
