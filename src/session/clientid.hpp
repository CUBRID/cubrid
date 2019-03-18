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
// clientid.hpp -
//

#ifndef _CLIENTID_HPP_
#define _CLIENTID_HPP_

/* this enumeration should be matched with DB_CLIENT_TYPE_XXX in db.h */
enum boot_client_type
{
  BOOT_CLIENT_UNKNOWN = -1,
  BOOT_CLIENT_SYSTEM_INTERNAL = 0,
  BOOT_CLIENT_DEFAULT = 1,
  BOOT_CLIENT_CSQL = 2,
  BOOT_CLIENT_READ_ONLY_CSQL = 3,
  BOOT_CLIENT_BROKER = 4,
  BOOT_CLIENT_READ_ONLY_BROKER = 5,
  BOOT_CLIENT_SLAVE_ONLY_BROKER = 6,
  BOOT_CLIENT_ADMIN_UTILITY = 7,
  BOOT_CLIENT_ADMIN_CSQL = 8,
  BOOT_CLIENT_LOG_COPIER = 9,
  BOOT_CLIENT_LOG_APPLIER = 10,
  BOOT_CLIENT_RW_BROKER_REPLICA_ONLY = 11,
  BOOT_CLIENT_RO_BROKER_REPLICA_ONLY = 12,
  BOOT_CLIENT_SO_BROKER_REPLICA_ONLY = 13,
  BOOT_CLIENT_ADMIN_CSQL_WOS = 14,	/* admin csql that can write on standby */
};
typedef enum boot_client_type BOOT_CLIENT_TYPE;

typedef struct clientids CLIENTIDS;
struct clientids		/* see BOOT_CLIENT_CREDENTIAL */
{
  boot_client_type client_type;
  char *client_info;// [DB_MAX_IDENTIFIER_LENGTH + 1];
  char *db_user;// [LOG_USERNAME_MAX];
  char *program_name;// [PATH_MAX + 1];
  char *login_name;// [L_cuserid + 1];
  char *host_name;// [MAXHOSTNAMELEN + 1];
  int process_id;
};

#endif // !_CLIENTID_HPP_
