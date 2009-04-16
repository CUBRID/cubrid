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
 * boot.h - Boot management common header
 */

#ifndef _BOOT_H_
#define _BOOT_H_

/* this enumeration should be matched with DB_CLIENT_TYPE_XXX in db.h */
typedef enum boot_client_type BOOT_CLIENT_TYPE;
enum boot_client_type
{
  BOOT_CLIENT_TYPE_NA = -1,
  BOOT_CLIENT_SYSTEM_INTERNAL = 0,
  BOOT_CLIENT_DEFAULT = 1,
  BOOT_CLIENT_CSQL = 2,
  BOOT_CLIENT_BROKER = 3,
  BOOT_CLIENT_ADMIN_UTILITY = 4,
  BOOT_CLIENT_LOG_REPLICATOR = 5
};
#define BOOT_NORMAL_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_DEFAULT \
         || (client_type) == BOOT_CLIENT_CSQL \
         || (client_type) == BOOT_CLIENT_BROKER)

#define BOOT_SPECIAL_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_ADMIN_UTILITY \
         || (client_type) == BOOT_CLIENT_LOG_REPLICATOR)

typedef struct boot_client_credential BOOT_CLIENT_CREDENTIAL;
struct boot_client_credential
{
  enum boot_client_type client_type;
  const char *client_info;	/* DB_MAX_IDENTIFIER_LENGTH */
  const char *db_name;		/* DB_MAX_IDENTIFIER_LENGTH */
  const char *db_user;		/* DB_MAX_USER_LENGTH */
  const char *db_password;	/* DB_MAX_PASSWORD_LENGTH */
  const char *program_name;	/* PATH_MAX */
  const char *login_name;	/* L_cuserid */
  const char *host_name;	/* MAXHOSTNAMELEN */
  int process_id;
};

typedef struct boot_db_path_info BOOT_DB_PATH_INFO;
struct boot_db_path_info
{
  const char *db_path;
  const char *vol_path;
  const char *log_path;
  const char *db_host;
  const char *db_comments;
};

#endif /* _BOOT_H_ */
