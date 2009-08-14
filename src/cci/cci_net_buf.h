/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */


/*
 * cci_net_buf.h -
 */

#ifndef	_CCI_NET_BUF_H_
#define	_CCI_NET_BUF_H_

#ident "$Id$"

#ifdef CAS
#error include error
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#if (defined(SOLARIS) && !defined(SOLARIS_X86)) || defined(HPUX) || defined(AIX)
#define BYTE_ORDER_BIG_ENDIAN
#elif defined(WINDOWS) || defined(LINUX) || defined(SOLARIS_X86)
#ifdef BYTE_ORDER_BIG_ENDIAN
#error BYTE_ORDER_BIG_ENDIAN defined
#endif
#else
#error PLATFORM NOT DEFINED
#endif

#ifdef BYTE_ORDER_BIG_ENDIAN
#define htoni64(X)              (X)
#define htonf(X)                (X)
#define htond(X)                (X)
#define ntohf(X)                (X)
#define ntohd(X)                (X)
#else
#define ntohi64(X)              htoni64(X)
#define ntohf(X)		htonf(X)
#define ntohd(X)		htond(X)
#endif

#define SIZE_SHORT      ((int) sizeof(short))
#define SIZE_INT        ((int) sizeof(int))
#define SIZE_INT64      ((int) sizeof(INT64))
#define SIZE_BIGINT     SIZE_INT64
#define SIZE_DOUBLE     ((int) sizeof(double))
#define SIZE_FLOAT      ((int) sizeof(float))
#define SIZE_DATE       6
#define SIZE_TIME       6
#define SIZE_TIMESTAMP  12
#define SIZE_DATETIME   14
#define SIZE_OBJECT     8

/*
 *  change function names to avoid naming conflict with cas server.
  */
#define net_buf_init		cnet_buf_init
#define net_buf_clear		cnet_buf_clear
#define net_buf_cp_str		cnet_buf_cp_str
#define net_buf_cp_int		cnet_buf_cp_int
#define net_buf_cp_bigint       cnet_buf_cp_bigint
#define net_buf_cp_float	cnet_buf_cp_float
#define net_buf_cp_double	cnet_buf_cp_double
#define net_buf_cp_short	cnet_buf_cp_short
#ifndef BYTE_ORDER_BIG_ENDIAN
#define htoni64                 cnet_buf_htoni64
#define htonf			cnet_buf_htonf
#define htond			cnet_buf_htond
#endif

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  int alloc_size;
  int data_size;
  char *data;
  int err_code;
} T_NET_BUF;

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern void cnet_buf_init (T_NET_BUF *);
extern void cnet_buf_clear (T_NET_BUF *);
extern int cnet_buf_cp_str (T_NET_BUF *, const char *, int);
extern int cnet_buf_cp_int (T_NET_BUF *, int);
extern int cnet_buf_cp_bigint (T_NET_BUF *, INT64);
extern int cnet_buf_cp_float (T_NET_BUF *, float);
extern int cnet_buf_cp_double (T_NET_BUF *, double);
extern int cnet_buf_cp_short (T_NET_BUF *, short);

#ifndef BYTE_ORDER_BIG_ENDIAN
extern INT64 cnet_buf_htoni64 (INT64 from);
extern float cnet_buf_htonf (float from);
extern double cnet_buf_htond (double from);
#endif

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_NET_BUF_H_ */
