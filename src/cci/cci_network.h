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


/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#ifndef MIN
#define MIN(X, Y)	((X) < (Y) ? (X) : (Y))
#endif

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int net_connect_srv (unsigned char *ip_addr, int port, char *db_name,
			    char *db_user, char *db_passwd, char is_first,
			    T_CCI_ERROR * err_buf, char *broker_info,
			    int *cas_pid);
extern int net_send_msg (int sock_fd, char *msg, int size);
extern int net_recv_msg (int sock_fd, char **msg, int *size,
			 T_CCI_ERROR * err_buf);
extern int net_send_file (int sock_fd, char *filename, int filesize);
extern int net_recv_file (int sock_fd, int file_size, int out_fd);


extern int net_cancel_request (unsigned char *ip_addr, int port, int pid);
extern int net_check_cas_request (int sock_fd);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/


#endif /* _CCI_NETWORK_H_ */
