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

//
// porting inline
//

#ifndef _PORTING_INLINE_HPP_
#define _PORTING_INLINE_HPP_

#if !defined (__GNUC__)
#define __attribute__(X)
#endif

#if defined (__GNUC__) && defined (NDEBUG)
#define ALWAYS_INLINE always_inline
#else
#define ALWAYS_INLINE
#endif

#if defined (__cplusplus) || defined (__GNUC__)
#define STATIC_INLINE static inline
#define INLINE inline
#elif _MSC_VER >= 1000
#define STATIC_INLINE __forceinline static
#define INLINE __forceinline
#else
/* TODO: we have several cases of using INLINE/STATIC_INLINE and adding function definition in headers. This won't
 * work. */
#define STATIC_INLINE static
#define INLINE
#endif

#endif // _PORTING_INLINE_HPP_
