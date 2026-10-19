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
 * memory_wrapper.cpp - the single definition of the replaced global operator delete
 *
 * A replacement operator delete may not be declared inline ([basic.stc.dynamic]/3),
 * so the definitions cannot live in memory_wrapper.hpp. They are gathered here and
 * this file is linked into the server library only, which is the only target that
 * is built with SERVER_MODE.
 */

#include "config.h"

#if defined(SERVER_MODE) && !defined(WINDOWS)
#include <new>

#include "memory_cwrapper.h"

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
operator delete[] (void *ptr) noexcept
{
  cub_free (ptr);
}

void
operator delete[] (void *ptr, size_t sz) noexcept
{
  cub_free (ptr);
}
#endif /* SERVER_MODE && !WINDOWS */
