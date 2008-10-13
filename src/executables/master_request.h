/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * master_request.h - master request handling module
 */

#ifndef _MASTER_REQUEST_H
#define _MASTER_REQUEST_H

#ident "$Id$"

extern int css_Master_socket_fd[2];
extern struct timeval *css_Master_timeout;
extern time_t css_Start_time;
extern int css_Total_server_count;
extern MSG_CAT Msg_catalog;

#define MASTER_GET_MSG(msgnum) \
    util_get_message(Msg_catalog, MSG_SET_MASTER, msgnum)

extern void css_process_info_request (CSS_CONN * conn);
extern void css_process_stop_shutdown (void);

#endif /* _MASTER_REQUEST_H */
