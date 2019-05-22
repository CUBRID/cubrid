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
// Applying row replication
//

#ifndef _REPLICATION_ROW_APPLY_HPP_
#define _REPLICATION_ROW_APPLY_HPP_

#include <string>

// forward declarations
struct db_value;
class record_descriptor;

namespace cubreplication
{
  int row_apply_insert (const std::string &classname, const record_descriptor &record);
  int row_apply_delete (const std::string &classname, const db_value &key_value);
  int row_apply_update (const std::string &classname, const db_value &key_value, const record_descriptor &record);

} // namespace cubreplication

#endif // !_REPLICATION_ROW_APPLY_HPP_
