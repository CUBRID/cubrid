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

//
// object_fetch.h - how to fetch objects on client
//

#ifndef _OBJECT_FETCH_H_
#define _OBJECT_FETCH_H_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

typedef enum au_fetchmode
{
  AU_FETCH_READ,
  AU_FETCH_SCAN,		/* scan that does not allow write */
  AU_FETCH_EXCLUSIVE_SCAN,	/* scan that does allow neither write nor other exclusive scan, i.e, scan for load
				 * index. */
  AU_FETCH_WRITE,
  AU_FETCH_UPDATE
} AU_FETCHMODE;

#endif // not _OBJECT_FETCH_H_
