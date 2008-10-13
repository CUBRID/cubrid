/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * byteord.h - Definitions related to disk storage order
 */

#ifndef _BYTEORD_H_
#define _BYTEORD_H_

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

/* DOUBLE HANDLING:

   The 1.x versions of the gcc compiler and the xlc compiler on the IBM
   allowed double float values to be alligned on single word boundaries.
   The 2.x version of gcc however requires that they be alligned on double
   word boundaries.  The older version of gcc was compiling references
   to doubles with a pair of single word load/store assembly statements.  
   The new compiler is using a single double load/store assembly statement.

   This has particular importance for the disk representation of objects
   and the packing/unpacking of network comm buffers.  One option is to
   always check allignment and make the appropriate padding during 
   pack/unpack.  Unfortunately, this is relatively complex to do this
   correctly.  Another simpler option is to make the OR_GET_DOUBLE and
   OR_PUT_DOUBLE macros do what the old compiler was doing, namely, 
   make two single word fetches rather than a single double word fetch.

   The MOVING_VAN union below is used to accomplish this.
*/

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

#ifndef OR_HAVE_HTONF
#define htonf(ptr, value) (*(ptr) = *(value))
#endif /* !OR_HAVE_HTONF */

#ifndef OR_HAVE_HTOND
#define htond(ptr, value) OR_MOVE_DOUBLE(value, ptr)
#endif /* !OR_HAVE_HTOND */

#endif /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */

#endif /* _BYTEORD_H_ */
