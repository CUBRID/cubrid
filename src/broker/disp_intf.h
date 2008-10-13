/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * disp_intf.h - Dispatcher Interface Header File 
 *               This file contains exported stuffs from Dispatcher
 */

#ifndef	_DISP_INTF_H_
#define	_DISP_INTF_H_

#ident "$Id$"

/* We use the following length to store any identifiers in the
 * implementation. The identifiers include application_name, path_name,
 * a line in the UniWeb application registry and so forth.
 * Note that, however, we DO NOT ensure that the length is enough for each
 * given identifier in the implementation. If too long identifier is given,
 * the behavior is undefined. We just recommend to increase the following
 * value in that case.
 */
#define	UW_MAX_LENGTH		1024

/* the magic string for UniWeb socket */
#ifdef _EDU_
#define	UW_SOCKET_MAGIC		"UW4.0EDU"
#else
#define	UW_SOCKET_MAGIC		"V3RQ4.0"
#endif

#define V3_HEADER_OK_COMPRESS           "V3_OK_COMP"
#define V3_HEADER_OK			"V3_OK"
#define V3_HEADER_ERR                   "V3_ERR"
#define V3_RESPONSE_HEADER_SIZE     16

/* PRE_SEND_DATA_SIZE = PRE_SEND_SCRIPT_SIZE + PRE_SEND_PRG_NAME_SIZE */
#ifdef _EDU_
#define	PRE_SEND_DATA_SIZE		114
#else
#define	PRE_SEND_DATA_SIZE		66
#endif
#define PRE_SEND_SCRIPT_SIZE		30
#define	PRE_SEND_PRG_NAME_SIZE		20
#define PRE_SEND_SESSION_ID_SIZE	16

#ifdef _EDU_
#define PRE_SEND_KEY_SIZE		48
#define PRE_SEND_KEY_OFFSET	\
      (PRE_SEND_SCRIPT_SIZE + PRE_SEND_PRG_NAME_SIZE + PRE_SEND_SESSION_ID_SIZE)
#endif

#endif /* _DISP_INTF_H_ */
