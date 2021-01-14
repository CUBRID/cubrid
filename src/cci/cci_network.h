/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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
 * cci_network.h -
 */

#ifndef	_CCI_NETWORK_H_
#define	_CCI_NETWORK_H_

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

#include "cci_handle_mng.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#ifndef MIN
#define MIN(X, Y)	((X) < (Y) ? (X) : (Y))
#endif

#define CAS_ERROR_INDICATOR     -1
#define DBMS_ERROR_INDICATOR    -2

#define CAS_PROTOCOL_ERR_INDICATOR_SIZE     sizeof(int)
#define CAS_PROTOCOL_ERR_CODE_SIZE          sizeof(int)

#define CAS_PROTOCOL_ERR_INDICATOR_INDEX    0
#define CAS_PROTOCOL_ERR_CODE_INDEX         (CAS_PROTOCOL_ERR_INDICATOR_SIZE)
#define CAS_PROTOCOL_ERR_MSG_INDEX          (CAS_PROTOCOL_ERR_INDICATOR_SIZE + CAS_PROTOCOL_ERR_CODE_SIZE)

#define BROKER_HEALTH_CHECK_TIMEOUT	5000

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/
typedef struct
{
  int *msg_body_size_ptr;
  char *info_ptr;
  char buf[MSG_HEADER_SIZE];
} MSG_HEADER;
/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int net_connect_srv (T_CON_HANDLE * con_handle, int host_id, T_CCI_ERROR * err_buf, int login_timeout);
extern int net_send_msg (T_CON_HANDLE * con_handle, char *msg, int size);
extern int net_recv_msg (T_CON_HANDLE * con_handle, char **msg, int *size, T_CCI_ERROR * err_buf);
extern int net_recv_msg_timeout (T_CON_HANDLE * con_handle, char **msg, int *msg_size, T_CCI_ERROR * err_buf,
				 int timeout);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int net_send_file (SOCKET sock_fd, char *filename, int filesize);
extern int net_recv_file (SOCKET sock_fd, int port, int file_size, int out_fd);
#endif
extern int net_cancel_request (T_CON_HANDLE * con_handle);
extern int net_check_cas_request (T_CON_HANDLE * con_handle);
extern bool net_peer_alive (unsigned char *ip_addr, int port, int timeout_msec);
extern bool net_check_broker_alive (unsigned char *ip_addr, int port, int timeout_msec, char useSSL);
/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/


#endif /* _CCI_NETWORK_H_ */
