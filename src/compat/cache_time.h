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
