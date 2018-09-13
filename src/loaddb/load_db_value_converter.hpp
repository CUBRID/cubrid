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
 * load_db_value_converter.hpp - conversion from string to DB_VALUE
 */

#ifndef _LOAD_DB_VALUE_CONVERTER_HPP_
#define _LOAD_DB_VALUE_CONVERTER_HPP_

#ident "$Id$"

#include "dbtype_def.h"
#include "load_common.hpp"

// forward declaration
struct tp_domain;

namespace cubload
{
  typedef void (*conv_func) (const char *, const tp_domain *, db_value *);

  conv_func &get_conv_func (const data_type ldr_type, const DB_TYPE db_type);

} // namespace cubload

#endif /* _LOAD_DB_VALUE_CONVERTER_HPP_ */
