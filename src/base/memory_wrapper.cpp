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
 * memory_wrapper.cpp - Replacing global operator new, delete with wrapping allocation functions
 */

#if !defined(WINDOWS) && defined(SERVER_MODE)

#include <new>

#include "memory_cwrapper.h"

/* memory_wrapper.hpp gives CUBRID source a new(__FILE__, __LINE__) form, but STL containers, headers
 * included before memory_wrapper.hpp and the standard library code linked into libcubrid allocate
 * with the global operator new. Both the global operator new and delete are replaced here, so that
 * every block in libcubrid is allocated by cub_alloc () or malloc () and released by cub_free ().
 *
 * The replacement functions must not be inline and must be defined only once, so they are defined
 * in this file instead of memory_wrapper.hpp. memory_wrapper.map keeps them local to libcubrid. */

/* memory_monitor::add_stat () builds std::string keys, and they are allocated by the operator new
 * below. While add_stat () is running for an operator new, the allocations are not tracked,
 * so that add_stat () is not entered again. */
static thread_local bool mmon_in_operator_new = false;

static void *
wrapped_operator_new (size_t size)
{
  void *p = NULL;

  if (!mmon_is_memory_monitor_enabled () || mmon_in_operator_new)
    {
      /* (malloc) is not expanded by the malloc () macro of memory_cwrapper.h */
      return (malloc) (size);
    }

  mmon_in_operator_new = true;
  p = cub_alloc (size, __FILE__, __LINE__);
  mmon_in_operator_new = false;

  return p;
}

void *
operator new (size_t size)
{
  void *p = NULL;

  /* operator new must return a unique pointer even for size 0 */
  if (size == 0)
    {
      size = 1;
    }

  while ((p = wrapped_operator_new (size)) == NULL)
    {
      std::new_handler handler = std::get_new_handler ();

      if (handler == NULL)
	{
	  throw std::bad_alloc ();
	}
      handler ();
    }

  return p;
}

void *
operator new[] (size_t size)
{
  return operator new (size);
}

void *
operator new (size_t size, const std::nothrow_t &) noexcept
{
  try
    {
      return operator new (size);
    }
  catch (...)
    {
      return NULL;
    }
}

void *
operator new[] (size_t size, const std::nothrow_t &) noexcept
{
  try
    {
      return operator new[] (size);
    }
  catch (...)
    {
      return NULL;
    }
}

/* Mainly delete (void *ptr, size_t sz) / delete [] (void *ptr, size_t sz) is called,
 * but when deleting arrays of destructible class types, including incomplete types,
 * either delete (void *ptr) / delete [] (void *ptr) or delete (void *ptr, size_t sz) /
 * delete [] (void *ptr, size_t sz) can be called. */
void
operator delete (void *ptr) noexcept
{
  cub_free (ptr);
}

void
operator delete (void *ptr, size_t sz) noexcept
{
  cub_free (ptr);
}

void
operator delete [] (void *ptr) noexcept
{
  cub_free (ptr);
}

void
operator delete [] (void *ptr, size_t sz) noexcept
{
  cub_free (ptr);
}

void
operator delete (void *ptr, const std::nothrow_t &) noexcept
{
  cub_free (ptr);
}

void
operator delete [] (void *ptr, const std::nothrow_t &) noexcept
{
  cub_free (ptr);
}

#endif // !WINDOWS && SERVER_MODE
