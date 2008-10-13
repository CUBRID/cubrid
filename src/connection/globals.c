/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * globals.c - The global variable definitions used by css
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "memory_manager_2.h"
#include "globals.h"

const char *css_Service_name = "cubrid";
int css_Service_id = 1523;

#if !defined(WINDOWS)
char css_Master_unix_domain_path[TMP_MAX] = "";
#endif
int css_Pipe_to_master;		/* socket for Master->Slave communication */

/* Stuff for the new client/server/master protocol */
int css_Server_inhibit_connection_socket = 0;
int css_Server_connection_socket = -1;

/* For Windows, we only support the new style of connection protocol. */
#if defined(WINDOWS)
int css_Server_use_new_connection_protocol = 1;
#else /* WINDOWS */
int css_Server_use_new_connection_protocol = 0;
#endif /* WINDOWS */
