/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * net_buf.h - 
 */

#ifndef	_NET_BUF_H_
#define	_NET_BUF_H_

#ident "$Id$"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

#if (defined(SOLARIS) && !defined(SOLARIS_X86)) || defined(HPUX) || defined(AIX) || defined(PPC_LINUX)
#define BYTE_ORDER_BIG_ENDIAN
#elif defined(WIN32) || defined(LINUX) || defined(OSF1) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(SOLARIS_X86)
#ifdef BYTE_ORDER_BIG_ENDIAN
#error BYTE_ORDER_BIG_ENDIAN defined
#endif
#else
#error PLATFORM NOT DEFINED
#endif

#ifdef BYTE_ORDER_BIG_ENDIAN
#define net_htonf(X)		(X)
#define net_htond(X)		(X)
#define net_ntohf(X)		(X)
#define net_ntohd(X)		(X)
#else
#define net_ntohf(X)		net_htonf(X)
#define net_ntohd(X)		net_htond(X)
#endif

#ifdef CAS_DEBUG
#define NET_BUF_ERROR_MSG_SET(NET_BUF, ERR_CODE, ERR_MSG)	\
     net_buf_error_msg_set_debug(NET_BUF, ERR_CODE, ERR_MSG, __FILE__, __LINE__)
#else
#define NET_BUF_ERROR_MSG_SET(NET_BUF, ERR_CODE, ERR_MSG)	\
     net_buf_error_msg_set(NET_BUF, ERR_CODE, ERR_MSG)
#endif

#define NET_BUF_CP_OBJECT(BUF, OID_P)			\
	do {						\
	  T_OBJECT	*obj_p = (T_OBJECT*) (OID_P);	\
	  net_buf_cp_int(BUF, obj_p->pageid, NULL);	\
	  net_buf_cp_short(BUF, obj_p->slotid);		\
	  net_buf_cp_short(BUF, obj_p->volid);		\
	} while (0)

#define NET_BUF_CP_CACHE_TIME(BUF, CT)                  \
        do {                                            \
          net_buf_cp_int(BUF, (CT)->sec, NULL);         \
          net_buf_cp_int(BUF, (CT)->usec, NULL);        \
        } while (0)

#define NET_BUF_KBYTE                   1024
#define NET_BUF_SIZE                    (128 * NET_BUF_KBYTE)
#define NET_BUF_EXTRA_SIZE              (64 * NET_BUF_KBYTE)
#define NET_BUF_ALLOC_SIZE              (NET_BUF_SIZE + NET_BUF_EXTRA_SIZE)
#define NET_BUF_HEADER_SIZE             ((int) sizeof(int))
#define NET_BUF_CURR_SIZE(n)                            \
  (NET_BUF_HEADER_SIZE + (n)->data_size)
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

extern void net_buf_init (T_NET_BUF *);
extern void net_buf_clear (T_NET_BUF *);
extern void net_buf_destroy (T_NET_BUF *);
extern int net_buf_cp_post_send_file (T_NET_BUF * net_buf, int, char *str);
extern int net_buf_cp_byte (T_NET_BUF *, char);
extern int net_buf_cp_str (T_NET_BUF *, char *, int);
extern int net_buf_cp_int (T_NET_BUF *, int, int *);
extern void net_buf_overwrite_int (T_NET_BUF *, int, int);
extern int net_buf_cp_float (T_NET_BUF *, float);
extern int net_buf_cp_double (T_NET_BUF *, double);
extern int net_buf_cp_short (T_NET_BUF *, short);

#ifdef CAS_DEBUG
extern void net_buf_error_msg_set_debug (T_NET_BUF * net_buf, int errcode,
					 char *errstr, char *file, int line);
#else
extern void net_buf_error_msg_set (T_NET_BUF * net_buf, int errcode,
				   char *errstr);
#endif

#ifndef BYTE_ORDER_BIG_ENDIAN
extern float net_htonf (float from);
extern double net_htond (double from);
#endif

extern void net_buf_column_info_set (T_NET_BUF * net_buf, char ut,
				     short scale, int prec, char *name);

#endif /* _NET_BUF_H_ */
