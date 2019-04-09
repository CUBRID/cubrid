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
 * authenticate_extract.hpp - authorization schema extraction
 *
 */

#ifndef _AUTHENTICATE_EXTRACT_HPP_
#define _AUTHENTICATE_EXTRACT_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "dbtype_def.h"

class print_output;

extern int au_export_users (print_output &output_ctx);
extern int au_export_grants (print_output &output_ctx, MOP class_mop);

#endif /* _AUTHENTICATE_EXTRACT_HPP_ */
