/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * jspsr.h - Java Stored Procedure Server Module Header
 *
 * Note: 
 */

#ifndef _JSPSR_H_
#define _JSPSR_H_

#ident "$Id$"

extern int jsp_start_server (const char *server_name, const char *path);
extern int jsp_stop_server (void);
extern int jsp_server_port (void);
extern int jsp_jvm_is_loaded (void);
extern int jsp_call_from_server (DB_VALUE * returnval, DB_VALUE ** argarray,
				 const char *name, const int arg_cnt);

#endif /* _JSPSR_H_ */
