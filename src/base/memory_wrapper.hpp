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

#include <utility>

template <typename T, typename... Args>
inline T *placement_new (T *ptr, Args &&... args)
{
  return new (ptr) T (std::forward<Args> (args)...);
}

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
inline void *operator new (size_t size, const char *file, const int line) noexcept
{
  return cub_alloc (size, file, line);
}

inline void *operator new[] (size_t size, const char *file, const int line) noexcept
{
  return cub_alloc (size, file, line);
}

/* Mainly delete (void *ptr, size_t sz) / delete [] (void *ptr, size_t sz) is called,
 * but when deleting arrays of destructible class types, including incomplete types,
 * either delete (void *ptr) / delete [] (void *ptr) or delete (void *ptr, size_t sz) /
 * delete [] (void *ptr, size_t sz) can be called.
 *
 * The four operator delete overloads named above used to be defined here, inline.
 * They are *replacement* functions, and [basic.stc.dynamic]/3 forbids declaring one
 * inline: an inline definition is only picked up by the translation units that happen
 * to include this header, so the replacement disappears whenever the compiler decides
 * not to emit a weak definition. That is what a release build did, leaving the
 * replacement out of the server entirely. They now live in memory_wrapper.cpp, which
 * is linked into the server, and they are deliberately not declared here: the compiler
 * declares them implicitly, and GCC rejects a redeclaration under -Wredundant-decls. */

#define new new(__FILE__, __LINE__)
#endif // SERVER_MODE
#endif // !WINDOWS

#endif // _MEMORY_WRAPPER_HPP_
