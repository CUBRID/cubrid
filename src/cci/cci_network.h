/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
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
