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
// method_def.hpp - define structures used by method feature
//

#ifndef _METHOD_DEF_H_
#define _METHOD_DEF_H_

typedef enum
{
  METHOD_SUCCESS = 1,
  METHOD_EOF,
  METHOD_ERROR
} METHOD_CALL_STATUS;

typedef enum
{
  VACOMM_BUFFER_SEND = 1,
  VACOMM_BUFFER_ABORT
} VACOMM_BUFFER_CLIENT_ACTION;

#define VACOMM_BUFFER_HEADER_SIZE           (OR_INT_SIZE * 3)
#define VACOMM_BUFFER_HEADER_LENGTH_OFFSET  (0)
#define VACOMM_BUFFER_HEADER_STATUS_OFFSET  (OR_INT_SIZE)
#define VACOMM_BUFFER_HEADER_NO_VALS_OFFSET (OR_INT_SIZE * 2)
#define VACOMM_BUFFER_HEADER_ERROR_OFFSET   (OR_INT_SIZE * 2)

#endif // _METHOD_DEF_H_
