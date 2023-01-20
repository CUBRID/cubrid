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
 * byte_order.h - Definitions related to disk storage order
 */

#ifndef _BYTE_ORDER_H_
#define _BYTE_ORDER_H_

#ident "$Id$"

#include "system.h"

#if defined (LINUX)
#include <arpa/inet.h>
#endif /* LINUX */

#if defined(sun)
#include <sys/types.h>
#endif /* sun */

#if defined(HPUX)
#include <netinet/in.h>
#endif /* HPUX */

#if defined(_AIX)
#include <net/nh.h>
#endif /* _AIX */

#if defined(WINDOWS)
#include <winsock2.h>
#if defined(_MSC_VER) && _MSC_VER >= 1700
/* We could not find when ntohf/ntohd/htonf/htond were exactly added. The only reference we could find are here:
* https://msdn.microsoft.com/en-us/library/windows/desktop/jj710201(v=vs.85).aspx (ntohf, links to others at the bottom).
*/
#define OR_HAVE_NTOHF
#define OR_HAVE_NTOHD
#define OR_HAVE_HTONF
#define OR_HAVE_HTOND
#endif
#endif /* WINDOWS */

#define OR_LITTLE_ENDIAN 1234
#define OR_BIG_ENDIAN    4321

#if defined(HPUX) || defined(_AIX) || defined(sparc)
#define OR_BYTE_ORDER OR_BIG_ENDIAN
#else /* HPUX || _AIX || sparc */
#define OR_BYTE_ORDER OR_LITTLE_ENDIAN	/* WINDOWS, LINUX, x86_SOLARIS */
#endif /* HPUX || _AIX || sparc */

#if defined(HPUX) || defined(_AIX) || defined(WINDOWS) || defined(LINUX)
#define OR_HAVE_NTOHS
#define OR_HAVE_NTOHL
#define OR_HAVE_HTONS
#define OR_HAVE_HTONL
#endif /* HPUX || _AIX || WINDOWS || LINUX */

#ifdef __cplusplus
extern "C"
{
#endif

typedef union moving_van MOVING_VAN;
union moving_van
{
  struct
  {
    unsigned int buf[2];
  } bits;
  double dbl;
};

#if !defined(IA64)
#define OR_MOVE_DOUBLE(src, dst) \
  (((MOVING_VAN *)(dst))->bits = ((MOVING_VAN *)(src))->bits)
#else /* !IA64 */
#define OR_MOVE_DOUBLE(src, dst) \
  memcpy(((MOVING_VAN*)(dst))->bits.buf, ((MOVING_VAN *)(src))->bits.buf, \
          sizeof(MOVING_VAN))
#endif /* !IA64 */

#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN

#ifndef OR_HAVE_NTOHS
extern unsigned short ntohs (unsigned short);
#endif /* !OR_HAVE_NTOHS */

#ifndef OR_HAVE_NTOHL
extern unsigned int ntohl (unsigned int);
#endif /* !OR_HAVE_NTOHL */

extern UINT64 ntohi64 (UINT64);

#ifndef OR_HAVE_NTOHF
extern float ntohf (UINT32 from);
#endif /* !OR_HAVE_NTOHF */

#ifndef OR_HAVE_NTOHD
extern double ntohd (UINT64 from);
#endif /* !OR_HAVE_NTOHD */

#ifndef OR_HAVE_HTONS
extern unsigned short htons (unsigned short);
#endif /* !OR_HAVE_HTONS */

#ifndef OR_HAVE_HTONL
extern unsigned int htonl (unsigned int);
#endif /* !OR_HAVE_HTONL */

extern UINT64 htoni64 (UINT64);

#ifndef OR_HAVE_HTONF
extern UINT32 htonf (float);
#endif /* !OR_HAVE_HTONF */

#ifndef OR_HAVE_HTOND
extern UINT64 htond (double);
#endif /* !OR_HAVE_HTOND */

#else /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */

#ifndef OR_HAVE_NTOHS
#define ntohs(x) (x)
#endif /* !OR_HAVE_NTOHS */

#ifndef OR_HAVE_NTOHL
#define ntohl(x) (x)
#endif /* !OR_HAVE_NTOHL */

#define ntohi64(x) (x)

#ifndef OR_HAVE_NTOHF
#define ntohf(ptr, value) (*(value) = *(ptr))
#endif /* !OR_HAVE_NTOHF */

#ifndef OR_HAVE_NTOHD
#define ntohd(ptr, value) OR_MOVE_DOUBLE(ptr, value)
#endif /* !OR_HAVE_NTOHD */

#ifndef OR_HAVE_HTONS
#define htons(x) (x)
#endif /* !OR_HAVE_HTONS */

#ifndef OR_HAVE_HTONL
#define htonl(x) (x)
#endif /* !OR_HAVE_HTONL */

#define htoni64(x) (x)

#ifndef OR_HAVE_HTONF
#define htonf(ptr, value) (*(ptr) = *(value))
#endif /* !OR_HAVE_HTONF */

#ifndef OR_HAVE_HTOND
#define htond(ptr, value) OR_MOVE_DOUBLE(value, ptr)
#endif /* !OR_HAVE_HTOND */

#endif /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */

#ifdef __cplusplus
}
#endif

#endif /* _BYTE_ORDER_H_ */
