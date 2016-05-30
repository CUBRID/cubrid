/*
 * Copyright (C) 2016 Search Solution Corporation. All rights reserved.
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

#ifndef _VERSION_H_
#define _VERSION_H_

#define MAJOR_VERSION @CUBRID_MAJOR_VERSION@
#define MINOR_VERSION @CUBRID_MINOR_VERSION@
#define PATCH_VERSION @CUBRID_PATCH_VERSION@
#define EXTRA_VERSION @CUBRID_EXTRA_VERSION@
#define MAJOR_RELEASE_STRING @MAJOR_RELEASE_STRING@
#define RELEASE_STRING @RELEASE_STRING@

#define BUILD_NUMBER @BUILD_NUMBER@
#define BUILD_OS @CMAKE_SYSTEM_NAME@

#define PACKAGE_STRING "@PACKAGE_STRING@"
#define PRODUCT_STRING "@PRODUCT_STRING@"
#define VERSION_STRING "@CUBRID_VERSION@"

#endif /* _VERSION_H_ */
