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

#include "dbtype_def.h"
#include "porting.h"

#include <array>

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

const size_t LOG_USERNAME_MAX = DB_MAX_USER_LENGTH + 1;

typedef struct clientids CLIENTIDS;
struct clientids		/* see BOOT_CLIENT_CREDENTIAL */
{
  public:
    boot_client_type client_type;
    char *client_info;// [DB_MAX_IDENTIFIER_LENGTH + 1];
    char *db_user;// [LOG_USERNAME_MAX];
    char *program_name;// [PATH_MAX + 1];
    char *login_name;// [L_cuserid + 1];
    char *host_name;// [MAXHOSTNAMELEN + 1];
    int process_id;

    clientids () = default;
    ~clientids ();

    void set_ids (boot_client_type type, const char *client_info, const char *db_user, const char *program_name,
		  const char *login_name, const char *host_name, int process_id);
    void set_ids (const clientids &other);

    void set_system_internal ();
    void set_system_internal_with_user (const char *db_user);
    void reset ();

  private:
    void clear ();
    void init_buffers ();

    struct membuf
    {
      std::array<char, DB_MAX_IDENTIFIER_LENGTH + 1> m_client_info_buffer;
      std::array<char, LOG_USERNAME_MAX + 1> m_db_user_buffer;
      std::array<char, PATH_MAX + 1> m_program_name_buffer;
      std::array<char, L_cuserid + 1> m_login_name_buffer;
      std::array<char, MAXHOSTNAMELEN + 1> m_host_name_buffer;

      membuf ();
      void clear ();
    };

    membuf *m_own_buffer;
};

typedef struct boot_client_credential BOOT_CLIENT_CREDENTIAL;
struct boot_client_credential
{
  clientids m_clientids;
  BOOT_CLIENT_TYPE client_type;
  char *client_info;		/* DB_MAX_IDENTIFIER_LENGTH */
  char *db_name;		/* DB_MAX_IDENTIFIER_LENGTH */
  char *db_user;		/* DB_MAX_USER_LENGTH */
  char *db_password;		/* DB_MAX_PASSWORD_LENGTH */
  char *program_name;		/* PATH_MAX */
  char *login_name;		/* L_cuserid */
  char *host_name;		/* MAXHOSTNAMELEN */
  char *preferred_hosts;	/* LINE_MAX */
  int connect_order;
  int process_id;

  boot_client_credential () = default;
};

#endif // !_CLIENTID_HPP_
