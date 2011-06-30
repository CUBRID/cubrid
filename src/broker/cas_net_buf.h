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
 * cas_net_buf.h -
 */

#ifndef	_CAS_NET_BUF_H_
#define	_CAS_NET_BUF_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

#include "cas_protocol.h"
#include "cas_network.h"
#include "cas.h"

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbtype.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if (defined(SOLARIS) && !defined(SOLARIS_X86)) || defined(HPUX) || defined(AIX) || defined(PPC_LINUX)
#define BYTE_ORDER_BIG_ENDIAN
#elif defined(WINDOWS) || defined(LINUX) || defined(OSF1) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(SOLARIS_X86)
#ifdef BYTE_ORDER_BIG_ENDIAN
#error BYTE_ORDER_BIG_ENDIAN defined
#endif
#else
#error PLATFORM NOT DEFINED
#endif

#ifdef BYTE_ORDER_BIG_ENDIAN
#define net_htoni64(X)		(X)
#define net_htonf(X)            (X)
#define net_htond(X)		(X)
#define net_ntohi64(X)          (X)
#define net_ntohf(X)		(X)
#define net_ntohd(X)		(X)
#else
#define net_ntohi64(X)          net_htoni64(X)
#define net_ntohf(X)		net_htonf(X)
#define net_ntohd(X)		net_htond(X)
#endif

#define NET_BUF_ERROR_MSG_SET(NET_BUF, ERR_INDICATOR, ERR_CODE, ERR_MSG)	\
     net_buf_error_msg_set(NET_BUF, ERR_INDICATOR, ERR_CODE, ERR_MSG, __FILE__, __LINE__)

#define NET_BUF_KBYTE                   1024
#define NET_BUF_SIZE                    (16 * NET_BUF_KBYTE)
#define NET_BUF_EXTRA_SIZE              (64 * NET_BUF_KBYTE)
#define NET_BUF_ALLOC_SIZE              (NET_BUF_SIZE + NET_BUF_EXTRA_SIZE)

#define NET_BUF_HEADER_MSG_SIZE         (NET_SIZE_INT)
#define NET_BUF_HEADER_SIZE             (NET_BUF_HEADER_MSG_SIZE + cas_info_size)
#define NET_BUF_CURR_SIZE(n)                            \
  (((n)->alloc_size > 0) ? (NET_BUF_HEADER_SIZE + (n)->data_size) : 0)
#define NET_BUF_FREE_SIZE(n)                            \
  ((n)->alloc_size - NET_BUF_CURR_SIZE(n))
#define NET_BUF_CURR_PTR(n)                             \
  ((n)->data + NET_BUF_CURR_SIZE(n))
#define CHECK_NET_BUF_SIZE(n)                           \
  (NET_BUF_CURR_SIZE(n) < NET_BUF_SIZE ? 1 : 0)

typedef struct t_net_buf T_NET_BUF;
struct t_net_buf
{
  int alloc_size;
  int data_size;
  char *data;
  int err_code;
  int post_file_size;
  char *post_send_file;
};

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
#define DB_BIGINT 	int64_t
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
extern void net_buf_init (T_NET_BUF * net_buf);
extern void net_buf_clear (T_NET_BUF * net_buf);
extern void net_buf_destroy (T_NET_BUF * net_buf);
extern int net_buf_cp_post_send_file (T_NET_BUF * net_buf, int, char *str);
extern int net_buf_cp_byte (T_NET_BUF * net_buf, char ch);
extern int net_buf_cp_str (T_NET_BUF * net_buf, const char *buf, int size);
extern int net_buf_cp_int (T_NET_BUF * net_buf, int value, int *begin_offset);
extern void net_buf_overwrite_int (T_NET_BUF * net_buf, int offset,
				   int value);
extern int net_buf_cp_bigint (T_NET_BUF * net_buf, DB_BIGINT value,
			      int *begin_offset);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void net_buf_overwrite_bigint (T_NET_BUF * net_buf, int offset,
				      DB_BIGINT value);
#endif
extern int net_buf_cp_float (T_NET_BUF * net_buf, float value);
extern int net_buf_cp_double (T_NET_BUF * net_buf, double value);
extern int net_buf_cp_short (T_NET_BUF * net_buf, short value);
extern int net_buf_cp_object (T_NET_BUF * net_buf, T_OBJECT * oid);
extern int net_buf_cp_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob);

extern void net_buf_error_msg_set (T_NET_BUF * net_buf, int errindicator,
				   int errcode, char *errstr,
				   const char *file, int line);

#ifndef BYTE_ORDER_BIG_ENDIAN
extern INT64 net_htoni64 (INT64 from);
extern float net_htonf (float from);
extern double net_htond (double from);
#endif

extern void net_buf_column_info_set (T_NET_BUF * net_buf, char ut,
				     short scale, int prec, const char *name);

extern void net_arg_get_size (int *size, void *arg);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern void net_arg_get_bigint (int64_t * value, void *arg);
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
extern void net_arg_get_bigint (DB_BIGINT * value, void *arg);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void net_arg_get_int (int *value, void *arg);
extern void net_arg_get_short (short *value, void *arg);
extern void net_arg_get_float (float *value, void *arg);
extern void net_arg_get_double (double *value, void *arg);
#define net_arg_get_char(value, arg) \
	((value)= *((char *) (arg) + NET_SIZE_INT));
extern void net_arg_get_str (char **value, int *size, void *arg);
extern void net_arg_get_date (short *year, short *mon, short *day, void *arg);
extern void net_arg_get_time (short *hh, short *mm, short *ss, void *arg);
extern void net_arg_get_timestamp (short *yr, short *mon, short *day,
				   short *hh, short *mm, short *ss,
				   void *arg);
extern void net_arg_get_datetime (short *yr, short *mon, short *day,
				  short *hh, short *mm, short *ss, short *ms,
				  void *arg);
extern void net_arg_get_cache_time (void *ct, void *arg);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void net_arg_get_object (T_OBJECT * obj, void *arg);
extern void net_arg_get_dbobject (DB_OBJECT ** obj, void *arg);
extern void net_arg_get_cci_object (int *pageid, short *slotid, short *volid,
				    void *arg);
extern void net_arg_get_lob_handle (T_LOB_HANDLE * lob, void *arg);
extern void net_arg_get_lob_value (DB_VALUE * db_lob, void *arg);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#endif /* _CAS_NET_BUF_H_ */
