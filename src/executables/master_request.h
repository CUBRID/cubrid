/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
 *
 */


/*
 * master_request.h - master request handling module
 */

#ifndef _MASTER_REQUEST_H_
#define _MASTER_REQUEST_H_

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
extern void css_process_start_shutdown (SOCKET_QUEUE_ENTRY * sock_entq,
					int timeout, char *buffer);

#endif /* _MASTER_REQUEST_H_ */
