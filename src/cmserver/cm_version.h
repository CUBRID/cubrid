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
 * cm_version.h - 
 */

#ifndef _CM_VERSION_H_
#define _CM_VERSION_H_

#ident "$Id$"

#define EMGR_CUR_VERSION	EMGR_MAKE_VER(3, 0)
#define EMGR_MAKE_VER(MAJOR, MINOR)	\
	((T_EMGR_VERSION) (((MAJOR) << 8) | (MINOR)))
typedef short T_EMGR_VERSION;

#endif /* _CM_VERSION_H_ */
