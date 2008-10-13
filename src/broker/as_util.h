/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * as_util.h - 
 */

#ifndef	_AS_UTIL_H_
#define	_AS_UTIL_H_

#ident "$Id$"

#include "disp_intf.h"

#define UW_ERROR_MESSAGE()	\
	uw_ht_error_message(uw_get_error_code(), uw_get_os_error_code())

#define V3_WRITE_HEADER_OK_COMPRESS()	\
	do {				\
	  char          buf[V3_RESPONSE_HEADER_SIZE];	\
	  memset(buf, '\0', sizeof(buf));	\
	  sprintf(buf, V3_HEADER_OK_COMPRESS);	\
	  uw_write_to_client(buf, sizeof(buf));	\
	} while(0);

#define V3_WRITE_HEADER_OK_FILE()	\
	do {				\
	  char          buf[V3_RESPONSE_HEADER_SIZE];	\
	  memset(buf, '\0', sizeof(buf));	\
	  sprintf(buf, V3_HEADER_OK);	\
	  uw_write_to_client(buf, sizeof(buf));	\
	} while(0);

#define V3_WRITE_HEADER_ERR()		\
	do {				\
	  char          buf[V3_RESPONSE_HEADER_SIZE];	\
	  memset(buf, '\0', sizeof(buf));	\
	  sprintf(buf, V3_HEADER_ERR);		\
	  uw_write_to_client(buf, sizeof(buf)); \
	} while(0);

void uw_ht_error_message (int error_code, int os_errno);
void as_pid_file_create (char *br_name, int as_index);
int as_get_my_as_info (char *br_name, int *as_index);
void as_db_err_log_set (char *br_name, int as_index);

#endif /* _AS_UTIL_H_ */
