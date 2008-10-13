/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas_intf.h -
 */

#ifndef _CAS_INTF_H_
#define _CAS_INTF_H_

#ident "$Id$"

#include "cas_protocol.h"

#define CAS_ER_COMMUNICATION            -1003
#define CAS_ER_VERSION                  -1016
#define CAS_ER_FREE_SERVER              -1017
#define CAS_ER_NOT_AUTHORIZED_CLIENT    -1018
#define CAS_ER_QUERY_CANCEL		-1019

#define CAS_SEND_ERROR_CODE(FD, VAL)	\
	do {				\
	  int   write_val;		\
	  write_val = htonl(VAL);	\
	  write_to_client(FD, (char*) &write_val, 4);	\
	} while (0)

#endif /* _CAS_INTF_H_ */
