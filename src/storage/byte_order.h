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
 * byte_order.h - Definitions related to disk storage order
 */

#ifndef _BYTE_ORDER_H_
#define _BYTE_ORDER_H_

#ident "$Id$"

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
extern void ntohf (float *, float *);
#endif /* !OR_HAVE_NTOHF */

#ifndef OR_HAVE_NTOHD
extern void ntohd (double *, double *);
#endif /* !OR_HAVE_NTOHD */

#ifndef OR_HAVE_HTONS
extern unsigned short htons (unsigned short);
#endif /* !OR_HAVE_HTONS */

#ifndef OR_HAVE_HTONL
extern unsigned int htonl (unsigned int);
#endif /* !OR_HAVE_HTONL */

extern UINT64 htoni64 (UINT64);

#ifndef OR_HAVE_HTONF
extern void htonf (float *, float *);
#endif /* !OR_HAVE_HTONF */

#ifndef OR_HAVE_HTOND
extern void htond (double *, double *);
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

#endif /* _BYTE_ORDER_H_ */
