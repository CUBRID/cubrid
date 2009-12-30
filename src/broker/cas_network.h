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
 * cas_network.h -
 */

#ifndef	_CAS_NETWORK_H_
#define	_CAS_NETWORK_H_

#ident "$Id$"

#include <string.h>

#include "cas_net_buf.h"

#define NET_DEFAULT_TIMEOUT	60

#ifndef MIN
#define MIN(X, Y)	((X) < (Y) ? (X) : (Y))
#endif

#define SIZE_SHORT      sizeof(short)
#define SIZE_INT        sizeof(int)
#define SIZE_FLOAT      sizeof(float)
#define SIZE_DOUBLE     sizeof(double)
#define SIZE_INT64      sizeof(INT64)
#define SIZE_BIGINT     SIZE_INT64
#define SIZE_DATE       6
#define SIZE_TIME       6
#define SIZE_OBJECT     8

#define NET_WRITE_ERROR_CODE_WITH_MSG(SOCK_FD, cas_info, ERROR_CODE, ERROR_MSG) \
        do {                                                    	\
          net_write_int (SOCK_FD, strlen (ERROR_MSG) + SIZE_INT + 1);      	\
          if (cas_info_size > 0)					\
	  {								\
	    net_write_stream (SOCK_FD, cas_info, cas_info_size); 	\
	  }                                                     	\
          net_write_int (SOCK_FD, ERROR_CODE);                  	\
          net_write_stream (SOCK_FD, ERROR_MSG, strlen (ERROR_MSG) + 1);\
        } while (0)

#define NET_WRITE_ERROR_CODE(SOCK_FD, cas_info, ERROR_CODE)       	\
        do {                                            		\
          net_write_int(SOCK_FD, SIZE_INT);                    		\
          if (cas_info_size > 0) 					\
          {								\
            net_write_stream(SOCK_FD, cas_info, cas_info_size);       	\
          }								\
          net_write_int(SOCK_FD, ERROR_CODE);           		\
        } while (0)

#define NET_ARG_GET_SIZE(SIZE, ARG)                     \
	do {                                            \
	  int	tmp_i;                                  \
	  memcpy(&tmp_i, (char*) (ARG), SIZE_INT);      \
	  SIZE = ntohl(tmp_i);                          \
	} while (0)

#define NET_ARG_GET_BIGINT(VALUE, ARG)                                     \
        do {                                                               \
          DB_BIGINT   tmp_i;                                               \
          memcpy(&tmp_i, (char*) (ARG) + SIZE_INT, SIZE_BIGINT);           \
          VALUE = ntohi64(tmp_i);                                          \
        } while (0)

#define NET_ARG_GET_INT(VALUE, ARG)                                        \
	do {                                                               \
	  int	tmp_i;                                                     \
	  memcpy(&tmp_i, (char*) (ARG) + SIZE_INT, SIZE_INT);              \
	  VALUE = ntohl(tmp_i);                                            \
	} while (0)

#define NET_ARG_GET_SHORT(VALUE, ARG)                                      \
	do {                                                               \
	  short	tmp_s;				                           \
	  memcpy(&tmp_s, (char*) (ARG) + SIZE_INT, SIZE_SHORT);            \
	  VALUE = ntohs(tmp_s);                                            \
	} while (0)

#define NET_ARG_GET_FLOAT(VALUE, ARG)                                      \
	do {                                                               \
	  float	tmp_f;                                                     \
	  memcpy(&tmp_f, (char*) (ARG) + SIZE_INT, SIZE_FLOAT);            \
	  VALUE = net_ntohf(tmp_f);                                        \
	} while (0)

#define NET_ARG_GET_DOUBLE(VALUE, ARG)                                     \
	do {                                                               \
	  double	tmp_d;                                             \
	  memcpy(&tmp_d, (char*) (ARG) + SIZE_INT, SIZE_DOUBLE);           \
	  VALUE = net_ntohd(tmp_d);                                        \
	} while (0)

#define NET_ARG_GET_CHAR(VALUE, ARG)                                       \
	do {                                                               \
	  VALUE = (*((char*) (ARG) + SIZE_INT));                           \
	} while (0)

#define NET_ARG_GET_STR(VALUE, SIZE, ARG)               \
	do {                                            \
	  int	_size;                                  \
	  char	*cur_p = (char*) ARG;                   \
	  memcpy(&_size, cur_p, SIZE_INT);              \
	  _size = ntohl(_size);                         \
	  cur_p += SIZE_INT;                            \
	  if (_size <= 0) {                             \
	    VALUE = NULL;                               \
	    SIZE = 0;                                 	\
	  } else {                                      \
	    VALUE = ((char*) cur_p);                    \
	    SIZE = _size;                               \
	  }                                             \
	} while (0)

#define NET_ARG_GET_DATE(YEAR, MON, DAY, ARG)   		\
	do {		                                        \
	  char		*cur_p = (char*) ARG;	                \
	  short		tmp_s;			                \
	  int           pos = SIZE_INT;                         \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);		\
	  YEAR = ntohs(tmp_s);		                        \
	  pos += SIZE_SHORT;                                    \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);		\
	  MON = ntohs(tmp_s);		                        \
          pos += SIZE_SHORT;                                    \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);		\
	  DAY = ntohs(tmp_s);		                        \
	} while (0)

#define NET_ARG_GET_TIME(HH, MM, SS, ARG)                            \
	do {                                                         \
	  char		*cur_p = (char*) ARG;	                     \
	  short		tmp_s;			                     \
          int           pos = SIZE_INT + SIZE_SHORT * 3;             \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
	  HH = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
	  MM = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
	  memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
	  SS = ntohs(tmp_s);                                         \
	} while (0)

#define NET_ARG_GET_TIMESTAMP(YR, MON, DAY, HH, MM, SS, ARG)         \
        do {                                                         \
          char          *cur_p = (char*) ARG;                        \
          short         tmp_s;                                       \
          int           pos = SIZE_INT;                              \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          YR = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          MON = ntohs(tmp_s);                                        \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          DAY = ntohs(tmp_s);                                        \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          HH = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          MM = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          SS = ntohs(tmp_s);                                         \
        } while (0)

#define NET_ARG_GET_DATETIME(YR, MON, DAY, HH, MM, SS, MS, ARG)      \
        do {                                                         \
          char          *cur_p = (char*) ARG;                        \
          short         tmp_s;                                       \
          int           pos = SIZE_INT;                              \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          YR = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          MON = ntohs(tmp_s);                                        \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          DAY = ntohs(tmp_s);                                        \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          HH = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          MM = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          SS = ntohs(tmp_s);                                         \
          pos += SIZE_SHORT;                                         \
          memcpy(&tmp_s, cur_p + pos, SIZE_SHORT);                   \
          MS = ntohs(tmp_s);                                         \
        } while (0)

#define NET_ARG_GET_OBJECT(VALUE, ARG)		\
	do {					\
	  char		*cur_p = (char*) ARG;	\
	  int		pageid;			\
	  short		slotid, volid;		\
	  DB_IDENTIFIER	tmp_oid;		\
	  DB_OBJECT	*tmp_obj;		\
	  int           pos = SIZE_INT;         \
	  memcpy(&pageid, cur_p + pos, SIZE_INT);\
	  pageid = ntohl(pageid);		\
	  pos += SIZE_INT;                      \
	  memcpy(&slotid, cur_p + pos, SIZE_SHORT);\
	  slotid = ntohs(slotid);		\
	  pos += SIZE_SHORT;                    \
          memcpy(&volid, cur_p + pos, SIZE_SHORT);\
	  volid = ntohs(volid);		        \
	  pos += SIZE_SHORT;                    \
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
	  int           pos = SIZE_INT;                         \
          memcpy(&_tmp_pageid, _macro_tmp_p + pos, SIZE_INT);   \
	  PAGEID = ntohl(_tmp_pageid);				\
	  pos += SIZE_INT;                                      \
	  memcpy(&_tmp_slotid, _macro_tmp_p + pos, SIZE_SHORT); \
	  SLOTID = ntohs(_tmp_slotid);				\
	  pos += SIZE_SHORT;                                    \
          memcpy(&_tmp_volid, _macro_tmp_p + pos, SIZE_SHORT);  \
	  VOLID = ntohs(_tmp_volid);				\
	} while (0)

#define CAS_TIMESTAMP_SIZE	12
#define CAS_DATETIME_SIZE       14

#define NET_ARG_GET_CACHE_TIME(CT, ARG)             \
        do {                                        \
          char *cur_p = (char*) ARG;                \
          int sec, usec;                            \
          int pos = SIZE_INT;                       \
          memcpy(&sec, cur_p + pos, SIZE_INT);      \
          pos += SIZE_INT;                          \
          memcpy(&usec, cur_p + pos, SIZE_INT);     \
          (CT)->sec = ntohl(sec);                   \
          (CT)->usec = ntohl(usec);                 \
        } while (0)

extern SOCKET
#if defined(WINDOWS)
  net_init_env (int *new_port);
#else
  net_init_env (void);
#endif

typedef struct
{
  int *msg_body_size_ptr;
  char *info_ptr;
  char buf[MSG_HEADER_SIZE];
} MSG_HEADER;

extern SOCKET net_connect_client (SOCKET srv_sock_fd);
extern int net_read_stream (SOCKET sock_fd, char *buf, int size);
extern int net_write_stream (SOCKET sock_fd, const char *buf, int size);
extern int net_write_int (SOCKET sock_fd, int value);
extern int net_read_int (SOCKET sock_fd, int *value);
extern int net_decode_str (char *msg, int msg_size, char *func_code,
			   void ***ret_argv);
extern int net_read_to_file (SOCKET sock_fd, int file_size, char *filename);
extern int net_write_from_file (SOCKET sock_fd, int file_size,
				char *filename);
extern void net_timeout_set (int timeout_sec);
extern void init_msg_header (MSG_HEADER * header);
extern int net_read_header (SOCKET sock_fd, MSG_HEADER * header);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int net_write_header (SOCKET sock_fd, MSG_HEADER * header);
#endif

extern int net_timeout_flag;

#endif /* _CAS_NETWORK_H_ */
