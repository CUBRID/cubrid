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
 * memory_wrapper.hpp - Overloading operator new, delete with all wrapping allocation functions
 */

#ifndef _MEMORY_WRAPPER_HPP_
#define _MEMORY_WRAPPER_HPP_

#if !defined(WINDOWS)

#include "memory_cwrapper.h"

/* ***IMPORTANT!!***
 * memory_wrapper.hpp has a restriction that it must locate at the end of including section
 * because the user-defined new for overloaded format can make build error in glibc
 * when glibc header use "placement new" or another overloaded format of new.
 * So memory_wrapper.hpp cannot be included in header file, but memory_cwrapper.h can be included.
 * You can include memory_cwrapper.h in a header file when the header file use allocation function.
 *                        HEADER FILE(.h/.hpp)    |   SOURCE FILE(.c/.cpp)    |   INCLUDE LOCATION
 * memory_cwrapper.h          CAN INCLUDE         |     CAN INCLUDE           |       ANYWHERE
 * memory_wrapper.hpp         CANNOT INCLUDE      |     CAN INCLUDE           |   END OF INCLUDE
 */

#ifdef SERVER_MODE
// TODO: The usage of operator new encompasses various additional methods beyond basic usage.
// However, as CUBRID does not currently utilize such additional methods, they are not overloaded.
// It has been decided that overloading will be undertaken should any issues arise from
// the discovery of the utilization of these additional methods.
inline void *operator new (size_t size, const char *file, const int line)
{
  return cub_alloc (size, file, line);
}

inline void *operator new[] (size_t size, const char *file, const int line)
{
  return cub_alloc (size, file, line);
}

/* Mainly delete (void *ptr, size_t sz) / delete [] (void *ptr, size_t sz) is called,
 * but when deleting arrays of destructible class types, including incomplete types,
 * either delete (void *ptr) / delete [] (void *ptr) or delete (void *ptr, size_t sz) /
 * delete [] (void *ptr, size_t sz) can be called. */
inline void operator delete (void *ptr) noexcept
{
  cub_free (ptr);
}

inline void operator delete (void *ptr, size_t sz) noexcept
{
  cub_free (ptr);
}

inline void operator delete [] (void *ptr) noexcept
{
  cub_free (ptr);
}

inline void operator delete [] (void *ptr, size_t sz) noexcept
{
  cub_free (ptr);
}

#define new new(__FILE__, __LINE__)
#endif // SERVER_MODE
#endif // !WINDOWS

#endif // _MEMORY_WRAPPER_HPP_
