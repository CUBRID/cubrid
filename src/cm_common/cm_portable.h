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
 * cm_portable.h - 
 */

#ifndef _CM_PORTABLE_H_
#define _CM_PORTABLE_H_

#if defined(WINDOWS)
#include <string.h>

#define PATH_MAX	256
#define MAXHOSTNAMELEN 64

#if defined(_MSC_VER) && _MSC_VER < 1900
/* Ref: https://msdn.microsoft.com/en-us/library/2ts7cx93(v=vs.120).aspx */
#define snprintf        _snprintf
#endif /* _MSC_VER && _MSC_VER < 1900 */

#define getpid        _getpid
#define strcasecmp(str1, str2)  _stricmp(str1, str2)

#endif

#endif /* _CM_PORTABLE_H_ */
