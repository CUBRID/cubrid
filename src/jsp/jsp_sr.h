/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * jsp_sr.h - Java Stored Procedure Server Module Header
 *
 * Note: 
 */

#ifndef _JSP_SR_H_
#define _JSP_SR_H_

#ident "$Id$"

extern int jsp_start_server (const char *server_name, const char *path);
extern int jsp_stop_server (void);
extern int jsp_server_port (void);
extern int jsp_jvm_is_loaded (void);
extern int jsp_call_from_server (DB_VALUE * returnval, DB_VALUE ** argarray,
				 const char *name, const int arg_cnt);

#endif /* _JSP_SR_H_ */
