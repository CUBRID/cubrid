/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * globals.h -
 */

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#ident "$Id$"

#include "defs.h"

extern int css_Service_id;
extern const char *css_Service_name;

extern int css_Server_use_new_connection_protocol;
extern int css_Server_inhibit_connection_socket;
extern int css_Server_connection_socket;

#if !defined(WINDOWS)
extern char css_Master_unix_domain_path[];
#endif
extern int css_Pipe_to_master;

#endif /* _GLOBALS_H_ */
