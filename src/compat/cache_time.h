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

/*
 *	cache_time.h -  CACHE TIME RELATED DEFINITIONS
 */

#ifndef _CACHE_TIME_H_
#define _CACHE_TIME_H_

#ident "$Id$"

typedef struct cache_time CACHE_TIME;
struct cache_time
{
  int sec;
  int usec;
};

#define CACHE_TIME_AS_ARGS(ct)	(ct)->sec, (ct)->usec

#define CACHE_TIME_EQ(T1, T2) \
  (((T1)->sec != 0) && ((T1)->sec == (T2)->sec) && ((T1)->usec == (T2)->usec))

#define CACHE_TIME_RESET(T) \
  do \
    { \
      (T)->sec = 0; \
      (T)->usec = 0; \
    } \
  while (0)

#define CACHE_TIME_MAKE(CT, TV) \
  do \
    { \
      (CT)->sec = (TV)->tv_sec; \
      (CT)->usec = (TV)->tv_usec; \
    } \
  while (0)

#define OR_CACHE_TIME_SIZE (OR_INT_SIZE * 2)

#define OR_PACK_CACHE_TIME(PTR, T) \
  do \
    { \
      if ((CACHE_TIME *) (T) != NULL) \
        { \
          PTR = or_pack_int (PTR, (T)->sec); \
          PTR = or_pack_int (PTR, (T)->usec); \
        } \
    else \
      { \
        PTR = or_pack_int (PTR, 0); \
        PTR = or_pack_int (PTR, 0); \
      } \
    } \
  while (0)

#define OR_UNPACK_CACHE_TIME(PTR, T) \
  do \
    { \
      if ((CACHE_TIME *) (T) != NULL) \
        { \
          PTR = or_unpack_int (PTR, &((T)->sec)); \
          PTR = or_unpack_int (PTR, &((T)->usec)); \
        } \
    } \
  while (0)

#endif /* _CACHE_TIME_T_ */
