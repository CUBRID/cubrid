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
 * replication_common.cpp
 */

#include "replication_common.hpp"

namespace cubreplication
{
  const int REPL_DEBUG_PROCESS = 0x001;
  const int REPL_DEBUG_SHORT_DUMP = 0x002;
  const int REPL_DEBUG_DETAILED_DUMP = 0x004;
  const int REPL_DEBUG_COMMUNICATION_DATA_DUMP = 0x010;

  bool is_debug_process_enabled ()
  {
    return prm_get_integer_value (PRM_ID_DEBUG_REPLICATION_DATA) & REPL_DEBUG_PROCESS;
  }

  bool is_debug_short_dump_enabled ()
  {
    return prm_get_integer_value (PRM_ID_DEBUG_REPLICATION_DATA) & REPL_DEBUG_SHORT_DUMP;
  }

  bool is_debug_detailed_dump_enabled ()
  {
    return prm_get_integer_value (PRM_ID_DEBUG_REPLICATION_DATA) & REPL_DEBUG_DETAILED_DUMP;
  }

  bool is_debug_communication_data_dump_enabled ()
  {
    return prm_get_integer_value (PRM_ID_DEBUG_REPLICATION_DATA) & REPL_DEBUG_COMMUNICATION_DATA_DUMP;
  }
}
