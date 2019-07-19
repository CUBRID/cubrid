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
 * replication_common.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_COMMON_HPP_
#define _REPLICATION_COMMON_HPP_

#include "error_manager.h"
#include <string>

#define er_log_debug_replication(...) if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA)) _er_log_debug(__VA_ARGS__)

const std::string REPL_ONLINE_CHANNEL_NAME = "online_replication";
const std::string REPL_COPY_CHANNEL_NAME = "copy_db_replication";
const std::string REPL_CONTROL_CHANNEL_NAME = "control_replication";

namespace cubreplication
{
  typedef enum
  {
    REPL_SEMISYNC_ACK_ON_CONSUME,
    REPL_SEMISYNC_ACK_ON_FLUSH
  } REPL_SEMISYNC_ACK_MODE;
};

#endif /* _REPLICATION_COMMON_HPP_ */
