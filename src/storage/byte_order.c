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
 * byte_order.c - Definitions related to disk storage order
 */

#ident "$Id$"

#include "byte_order.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * LITTLE ENDIAN TRANSFORMATION FUNCTIONS
 */

/*
 * little endian support functions.
 * Could just leave these in all the time.
 * Try to speed these up, consider making them inline.
 *
 */
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN

#if !defined (OR_HAVE_NTOHS)
unsigned short
ntohs (unsigned short from)
{
  unsigned short to;
  char *ptr, *vptr;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[1];
  vptr[1] = ptr[0];

  return to;
}
#endif /* !OR_HAVE_NTOHS */

#if !defined (OR_HAVE_NTOHL)
unsigned int
ntohl (unsigned int from)
{
  unsigned int to;
  char *ptr, *vptr;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[3];
  vptr[1] = ptr[2];
  vptr[2] = ptr[1];
  vptr[3] = ptr[0];

  return to;
}
#endif /* !OR_HAVE_NTOHL */

#if !defined (OR_HAVE_NTOHF)
float
ntohf (UINT32 from)
{
  char *ptr, *vptr;
  float to;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[3];
  vptr[1] = ptr[2];
  vptr[2] = ptr[1];
  vptr[3] = ptr[0];

  return to;
}
#endif /* !OR_HAVE_NTOHF */

#if !defined (OR_HAVE_NTOHD)
double
ntohd (UINT64 from)
{
  char *ptr, *vptr;
  double to;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[7];
  vptr[1] = ptr[6];
  vptr[2] = ptr[5];
  vptr[3] = ptr[4];
  vptr[4] = ptr[3];
  vptr[5] = ptr[2];
  vptr[6] = ptr[1];
  vptr[7] = ptr[0];

  return to;
}
#endif /* !OR_HAVE_NTOHD */

UINT64
ntohi64 (UINT64 from)
{
  UINT64 to;
  char *ptr, *vptr;

  ptr = (char *) &from;
  vptr = (char *) &to;
  vptr[0] = ptr[7];
  vptr[1] = ptr[6];
  vptr[2] = ptr[5];
  vptr[3] = ptr[4];
  vptr[4] = ptr[3];
  vptr[5] = ptr[2];
  vptr[6] = ptr[1];
  vptr[7] = ptr[0];

  return to;
}

#if !defined (OR_HAVE_HTONS)
unsigned short
htons (unsigned short from)
{
  return ntohs (from);
}
#endif /* !OR_HAVE_HTONS */

#if !defined (OR_HAVE_HTONL)
unsigned int
htonl (unsigned int from)
{
  return ntohl (from);
}
#endif /* !OR_HAVE_HTONL */

UINT64
htoni64 (UINT64 from)
{
  return ntohi64 (from);
}

#if !defined (OR_HAVE_HTONF)
UINT32
htonf (float from)
{
  UINT32 to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
}
#endif /* !OR_HAVE_HTONF */

#if !defined (OR_HAVE_HTOND)
UINT64
htond (double from)
{
  UINT64 to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}
#endif /* ! OR_HAVE_HTOND */

#endif /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */
