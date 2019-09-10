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
 * db_client_type.hpp -  Definitions for client types
 */

#ifndef _DB_CLIENT_TYPE_HPP
#define _DB_CLIENT_TYPE_HPP

enum db_client_type
{
  DB_CLIENT_TYPE_UNKNOWN = -1,
  DB_CLIENT_TYPE_SYSTEM_INTERNAL = 0,

  DB_CLIENT_TYPE_DEFAULT = 1,
  DB_CLIENT_TYPE_CSQL = 2,
  DB_CLIENT_TYPE_READ_ONLY_CSQL = 3,
  DB_CLIENT_TYPE_BROKER = 4,
  DB_CLIENT_TYPE_READ_ONLY_BROKER = 5,
  DB_CLIENT_TYPE_SLAVE_ONLY_BROKER = 6,
  DB_CLIENT_TYPE_ADMIN_UTILITY = 7,
  DB_CLIENT_TYPE_ADMIN_CSQL = 8,
  DB_CLIENT_TYPE_LOG_COPIER = 9,
  DB_CLIENT_TYPE_LOG_APPLIER = 10,
  DB_CLIENT_TYPE_RW_BROKER_REPLICA_ONLY = 11,
  DB_CLIENT_TYPE_RO_BROKER_REPLICA_ONLY = 12,
  DB_CLIENT_TYPE_SO_BROKER_REPLICA_ONLY = 13,
  DB_CLIENT_TYPE_ADMIN_CSQL_WOS = 14,	/* admin csql that can write on standby */
  DB_CLIENT_TYPE_DDL_PROXY = 15,

  DB_CLIENT_TYPE_MAX
};

#endif /* _DB_CLIENT_TYPE_HPP */
