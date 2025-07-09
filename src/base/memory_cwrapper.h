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

/* ***IMPORTANT!!***
 * memory_wrapper.hpp has a restriction that it must locate at the end of including section
 * because the user-defined new for overloaded format can make build error in glibc
 * when glibc header use "placement new" or another overloaded format of new.
 * So memory_wrapper.hpp cannot be included in header file, but memory_cwrapper.h can be included.
 * You can include memory_cwrapper.h in a header file when the header file use allocation function.
 *                        HEADER FILE(.h/.hpp)    |   SOURCE FILE(.c/.cpp)    |   INCLUDE LOCATION
 * memory_cwrapper.h          CAN INCLUDE         |     CAN INCLUDE           |       ANYWHERE
 * memory_wrapper.hpp         CANNOT INCLUDE      |     CANNOT INCLUDE        |   END OF INCLUDE
 */

#if !defined(WINDOWS)
#ifdef SERVER_MODE
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "memory_monitor_sr.hpp"

#if defined(__SVR4)
// *INDENT-OFF*
extern "C" size_t malloc_usable_size (void *);
// *INDENT-ON*
#elif defined(__APPLE__)
#include <malloc/malloc.h>

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif
#elif defined(__linux__)
#include <malloc.h>

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif
#endif

inline size_t
get_allocated_size (void *ptr)
{
  if (ptr == NULL)
    {
      return 0;
    }
  else
    {
      return mmon_get_allocated_size ((char *) ptr);
    }
}

inline void
cub_free (void *ptr)
{
  if (mmon_is_memory_monitor_enabled () && ptr != NULL)
    {
      assert (malloc_usable_size (ptr) != 0);
      mmon_sub_stat ((char *) ptr);
    }
  free (ptr);
}

inline void *
cub_alloc (size_t size, const char *file, const int line)
{
  void *p = NULL;

  if (mmon_is_memory_monitor_enabled ())
    {
      p = malloc (size + cubmem::MMON_METAINFO_SIZE);
      if (p != NULL)
	{
	  mmon_add_stat ((char *) p, malloc_usable_size (p), file, line);
	}
    }
  else
    {
      p = malloc (size);
    }

  return p;
}

inline void *
cub_calloc (size_t num, size_t size, const char *file, const int line)
{
  void *p = NULL;

  if (mmon_is_memory_monitor_enabled ())
    {
      p = malloc (num * size + cubmem::MMON_METAINFO_SIZE);
      if (p != NULL)
	{
	  memset (p, 0, num * size + cubmem::MMON_METAINFO_SIZE);
	  mmon_add_stat ((char *) p, malloc_usable_size (p), file, line);
	}
    }
  else
    {
      p = calloc (num, size);
    }

  return p;
}

inline void *
cub_realloc (void *ptr, size_t size, const char *file, const int line)
{
  void *new_ptr = NULL;

  if (mmon_is_memory_monitor_enabled ())
    {
      /* Realloc spec.: Realloc should work same as malloc if ptr == NULL */
      if (ptr == NULL)
	{
	  return cub_alloc (size, file, line);
	}

      /* Realloc spec.: If input size is zero, just free ptr */
      if (size == 0)
	{
	  cub_free (ptr);
	}
      else
	{
	  new_ptr = malloc (size + cubmem::MMON_METAINFO_SIZE);
	  if (new_ptr != NULL)
	    {
	      mmon_add_stat ((char *) new_ptr, malloc_usable_size (new_ptr), file, line);

	      size_t old_size = get_allocated_size (ptr);
	      size_t copy_size = old_size < size ? old_size : size;
	      memcpy (new_ptr, ptr, copy_size);
	      cub_free (ptr);
	    }
	}
    }
  else
    {
      new_ptr = realloc (ptr, size);
    }

  return new_ptr;
}

inline char *
cub_strdup (const char *str, const char *file, const int line)
{
  void *p = NULL;

  p = cub_alloc (strlen (str) + 1, file, line);
  if (p != NULL)
    {
      memcpy (p, str, strlen (str) + 1);
    }

  return (char *) p;
}

#define malloc(sz) cub_alloc(sz, __FILE__, __LINE__)
#define calloc(num, sz) cub_calloc(num, sz, __FILE__, __LINE__)
#define realloc(ptr, sz) cub_realloc(ptr, sz, __FILE__, __LINE__)
#define strdup(str) cub_strdup(str, __FILE__, __LINE__)
#define free(ptr) cub_free(ptr)
#endif // SERVER_MODE
#endif // !WINDOWS

#endif // _MEMORY_CWRAPPER_H_
