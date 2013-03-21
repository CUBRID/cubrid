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

#ifndef CUBRIDMOCK_H_
#define CUBRIDMOCK_H_

#include "dbgw3/client/Mock.h"

#ifdef BUILD_MOCK
#define cci_connect_with_url cci_mock_connect_with_url
#define cci_prepare cci_mock_prepare
#define cci_execute cci_mock_execute
#define cci_execute_array cci_mock_execute_array
#define cci_set_autocommit cci_mock_set_autocommit
#define cci_end_tran cci_mock_end_tran
#endif

#endif
