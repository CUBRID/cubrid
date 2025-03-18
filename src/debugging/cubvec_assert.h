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
 * cubvec_assert.h - Utility macros for debugging CUBVEC
 */

#ifndef CUBVEC_ASSERT_H
#define CUBVEC_ASSERT_H

#include <assert.h>

#ifdef CUBVEC_TEAM
#ifdef NDEBUG
    // Custom assertion that works even in release mode
#define ASSERT_CUBVEC(expr) \
      do { \
        if (!(expr)) { \
          fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #expr, __FILE__, __LINE__); \
          abort(); \
        } \
      } while (0)
#else
    // In debug mode, use the standard assert
#define ASSERT_CUBVEC(expr) assert(expr)
#endif
#else
#define ASSERT_CUBVEC(expr) ((void)0)
#endif

#ifdef HORNETMJ
#define ASSERT_HORNETMJ(expr) assert(expr)
#else
#define ASSERT_HORNETMJ(expr) ((void)0)
#endif

#ifdef HGRYOO
#define ASSERT_HGRYOO(expr) assert(expr)
#else
#define ASSERT_HGRYOO(expr) ((void)0)
#endif

#ifdef YEUNJUNLEE
#define ASSERT_YEUNJUNLEE(expr) assert(expr)
#else
#define ASSERT_YEUNJUNLEE(expr) ((void)0)
#endif

#ifdef VIMKIM
#define ASSERT_VIMKIM(expr) assert(expr)
#else
#define ASSERT_VIMKIM(expr) ((void)0)
#endif

#endif // CUBVEC_ASSERT_H
