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
 * oob_handler.h - interface definitions for the out of band message handler 
 */

#ifndef _OOB_HANDLER_H_
#define _OOB_HANDLER_H_

#ident "$Id$"

/*
 * The message types that can be used with Out Of Band Messages
 * if adding more types, always add BEFORE OOB_LAST_ENUM.
 */
typedef enum oob_types
{
  OOB_SERVER_SHUTDOWN = 0,	/* Notification from server: going down       */
  OOB_SERVER_DOWN,		/* Server is now down, close socket on client */
  OOB_CLIENT_TO_SERVER,		/* Sent from client to server                 */
  OOB_WAITING_POLL,		/* Sent from server to client                 */
  OOB_LAST_ENUM			/* MUST BE LAST (defines number of enums)     */
} OOB_ENUM_TYPE;

#if !defined(SERVER_MODE)
typedef void (*OOB_HANDLER_FUNCTION) (unsigned int eid);

extern void css_init_oob_handler (void);
extern void css_set_oob_handler (OOB_ENUM_TYPE oob_type,
				 OOB_HANDLER_FUNCTION handler);
#endif /* !SERVER_MODE */

#endif /* _OOB_HANDLER_H_ */
