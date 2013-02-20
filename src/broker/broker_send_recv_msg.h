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
 * broker_send_recv_msg.h - 
 */

#ifndef _BROKER_SEND_RECV_MSG_H_
#define _BROKER_SEND_RECV_MSG_H_

#ident "$Id$"

struct sendmsg_s
{
  int rid;
  int client_version;
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
};

#endif /* _BROKER_SEND_RECV_MSG_H_ */
