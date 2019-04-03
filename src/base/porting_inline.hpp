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
