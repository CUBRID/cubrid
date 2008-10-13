/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * net_cas.h -
 */

#ifndef	_NET_CAS_H_
#define	_NET_CAS_H_

#ident "$Id$"

#include <string.h>

#include "net_buf.h"

#define NET_DEFAULT_TIMEOUT	60

#ifndef MIN
#define MIN(X, Y)	((X) < (Y) ? (X) : (Y))
#endif

#define NET_WRITE_ERROR_CODE(SOCK_FD, ERROR_CODE)	\
	do {						\
	  net_write_int(SOCK_FD, 4);			\
	  net_write_int(SOCK_FD, ERROR_CODE);		\
	} while (0)

#define NET_ARG_GET_SIZE(SIZE, ARG)		\
	do {					\
	  int	tmp_i;				\
	  memcpy(&tmp_i, (char*) (ARG), 4);	\
	  SIZE = ntohl(tmp_i);			\
	} while (0)

#define NET_ARG_GET_INT(VALUE, ARG)	\
	do {				\
	  int	tmp_i;			\
	  memcpy(&tmp_i, (char*) (ARG) + 4, 4);	\
	  VALUE = ntohl(tmp_i);		\
	} while (0)

#define NET_ARG_GET_SHORT(VALUE, ARG)		\
	do {					\
	  short	tmp_s;				\
	  memcpy(&tmp_s, (char*) (ARG) + 4, 2);	\
	  VALUE = ntohs(tmp_s);			\
	} while (0)

#define NET_ARG_GET_FLOAT(VALUE, ARG)		\
	do {					\
	  float	tmp_f;				\
	  memcpy(&tmp_f, (char*) (ARG) + 4, 4);	\
	  VALUE = net_ntohf(tmp_f);			\
	} while (0)

#define NET_ARG_GET_DOUBLE(VALUE, ARG)		\
	do {					\
	  double	tmp_d;				\
	  memcpy(&tmp_d, (char*) (ARG) + 4, 8);	\
	  VALUE = net_ntohd(tmp_d);			\
	} while (0)

#define NET_ARG_GET_CHAR(VALUE, ARG) 	\
	do {				\
	  VALUE = (*((char*) (ARG) + 4));	\
	} while (0)

#define NET_ARG_GET_STR(VALUE, SIZE, ARG)	\
	do {					\
	  int	_size;				\
	  char	*cur_p = (char*) ARG;		\
	  memcpy(&_size, cur_p, 4);		\
	  _size = ntohl(_size);			\
	  cur_p += 4;				\
	  if (_size <= 0)			\
	    VALUE = NULL;			\
	  else					\
	    VALUE = ((char*) cur_p);		\
	  SIZE = _size;				\
	} while (0)

#define NET_ARG_GET_DATE(YEAR, MON, DAY, ARG)		\
	do {					\
	  char		*cur_p = (char*) ARG;	\
	  short		tmp_s;			\
	  memcpy(&tmp_s, cur_p + 4, 2);		\
	  YEAR = ntohs(tmp_s);		\
	  memcpy(&tmp_s, cur_p + 6, 2);		\
	  MON = ntohs(tmp_s);		\
	  memcpy(&tmp_s, cur_p + 8, 2);		\
	  DAY = ntohs(tmp_s);		\
	} while (0)

#define NET_ARG_GET_TIME(HH, MM, SS, ARG)		\
	do {					\
	  char		*cur_p = (char*) ARG;	\
	  short		tmp_s;			\
	  memcpy(&tmp_s, cur_p + 10, 2);		\
	  HH = ntohs(tmp_s);		\
	  memcpy(&tmp_s, cur_p + 12, 2);		\
	  MM = ntohs(tmp_s);		\
	  memcpy(&tmp_s, cur_p + 14, 2);		\
	  SS = ntohs(tmp_s);		\
	} while (0)

#define NET_ARG_GET_TIMESTAMP(YR, MON, DAY, HH, MM, SS, ARG)	\
	do {					\
	  char		*cur_p = (char*) ARG;	\
	  short		tmp_s;			\
	  memcpy(&tmp_s, cur_p + 4, 2);		\
	  YR = ntohs(tmp_s);			\
	  memcpy(&tmp_s, cur_p + 6, 2);		\
	  MON = ntohs(tmp_s);			\
	  memcpy(&tmp_s, cur_p + 8, 2);		\
	  DAY = ntohs(tmp_s);			\
	  memcpy(&tmp_s, cur_p + 10, 2);	\
	  HH = ntohs(tmp_s);			\
	  memcpy(&tmp_s, cur_p + 12, 2);	\
	  MM = ntohs(tmp_s);			\
	  memcpy(&tmp_s, cur_p + 14, 2);	\
	  SS = ntohs(tmp_s);			\
	} while (0)

#define NET_ARG_GET_OBJECT(VALUE, ARG)		\
	do {					\
	  char		*cur_p = (char*) ARG;	\
	  int		pageid;			\
	  short		slotid, volid;		\
	  DB_IDENTIFIER	tmp_oid;		\
	  DB_OBJECT	*tmp_obj;		\
	  memcpy(&pageid, cur_p + 4, 4);	\
	  pageid = ntohl(pageid);		\
	  memcpy(&slotid, cur_p + 8, 2);	\
	  slotid = ntohs(slotid);		\
	  memcpy(&volid, cur_p + 10, 2);	\
	  volid = ntohs(volid);		\
	  tmp_oid.pageid = pageid;		\
	  tmp_oid.slotid = slotid;		\
	  tmp_oid.volid = volid;		\
	  tmp_obj = db_object(&tmp_oid);	\
	  VALUE = tmp_obj;			\
	} while (0)

#define NET_ARG_GET_CCI_OBJECT(PAGEID, SLOTID, VOLID, ARG)	\
	do {							\
	  char	*_macro_tmp_p = (char*) ARG;			\
	  int	_tmp_pageid;					\
	  short	_tmp_slotid, _tmp_volid;			\
	  memcpy(&_tmp_pageid, _macro_tmp_p + 4, 4);		\
	  PAGEID = ntohl(_tmp_pageid);				\
	  memcpy(&_tmp_slotid, _macro_tmp_p + 8, 2);		\
	  SLOTID = ntohs(_tmp_slotid);				\
	  memcpy(&_tmp_volid, _macro_tmp_p + 10, 2);		\
	  VOLID = ntohs(_tmp_volid);				\
	} while (0)

#define CAS_TIMESTAMP_SIZE	12

#define NET_ARG_GET_CACHE_TIME(CT, ARG) \
        do {                            \
          char *cur_p = (char*) ARG;    \
          int sec, usec;                \
          memcpy(&sec, cur_p + 4, 4);   \
          memcpy(&usec, cur_p + 8, 4);  \
          (CT)->sec = ntohl(sec);              \
          (CT)->usec = ntohl(usec);            \
        } while (0)

#ifdef WIN32
extern int net_init_env (int *new_port);
#else
extern int net_init_env (void);
#endif
extern int net_connect_client (int srv_sock_fd);
extern int net_read_stream (int sock_fd, char *buf, int size);
extern int net_write_stream (int sock_fd, char *buf, int size);
extern int net_write_int (int sock_fd, int value);
extern int net_read_int (int sock_fd, int *value);
extern int net_decode_str (char *msg, int msg_size, char *func_code,
			   void ***ret_argv);
extern int net_read_to_file (int sock_fd, int file_size, char *filename);
extern int net_write_from_file (int sock_fd, int file_size, char *filename);
extern void net_timeout_set (int timeout_sec);

extern int net_timeout_flag;

#endif /* _NET_CAS_H_ */
