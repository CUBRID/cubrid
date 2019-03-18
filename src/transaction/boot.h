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

#include "clientid.hpp"
#include "es_common.h"
#include "porting.h"
#include "storage_common.h"

#define BOOT_NORMAL_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_DEFAULT \
         || (client_type) == BOOT_CLIENT_CSQL \
         || (client_type) == BOOT_CLIENT_READ_ONLY_CSQL \
         || (client_type) == BOOT_CLIENT_BROKER \
         || (client_type) == BOOT_CLIENT_READ_ONLY_BROKER \
         || (client_type) == BOOT_CLIENT_RW_BROKER_REPLICA_ONLY \
         || (client_type) == BOOT_CLIENT_RO_BROKER_REPLICA_ONLY)

#define BOOT_READ_ONLY_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_READ_ONLY_CSQL \
         || (client_type) == BOOT_CLIENT_READ_ONLY_BROKER \
         || (client_type) == BOOT_CLIENT_SLAVE_ONLY_BROKER \
         || (client_type) == BOOT_CLIENT_RO_BROKER_REPLICA_ONLY \
         || (client_type) == BOOT_CLIENT_SO_BROKER_REPLICA_ONLY)

#define BOOT_ADMIN_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_ADMIN_UTILITY \
         || (client_type) == BOOT_CLIENT_ADMIN_CSQL \
         || (client_type) == BOOT_CLIENT_ADMIN_CSQL_WOS)

#define BOOT_LOG_REPLICATOR_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_LOG_COPIER \
         || (client_type) == BOOT_CLIENT_LOG_APPLIER)

#define BOOT_CSQL_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_CSQL \
        || (client_type) == BOOT_CLIENT_READ_ONLY_CSQL \
        || (client_type) == BOOT_CLIENT_ADMIN_CSQL \
        || (client_type) == BOOT_CLIENT_ADMIN_CSQL_WOS)

#define BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type) \
        ((client_type) == BOOT_CLIENT_DEFAULT \
         || (client_type) == BOOT_CLIENT_BROKER \
         || (client_type) == BOOT_CLIENT_READ_ONLY_BROKER \
         || (client_type) == BOOT_CLIENT_SLAVE_ONLY_BROKER \
         || BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE(client_type))

#define BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE(client_type) \
    ((client_type) == BOOT_CLIENT_RW_BROKER_REPLICA_ONLY \
        || (client_type) == BOOT_CLIENT_RO_BROKER_REPLICA_ONLY \
        || (client_type) == BOOT_CLIENT_SO_BROKER_REPLICA_ONLY)

#define BOOT_WRITE_ON_STANDY_CLIENT_TYPE(client_type) \
  ((client_type) == BOOT_CLIENT_LOG_APPLIER \
      || (client_type) == BOOT_CLIENT_RW_BROKER_REPLICA_ONLY \
      || (client_type) == BOOT_CLIENT_ADMIN_CSQL_WOS)

/*
 * BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE()
 * ((broker_default type || (remote && csql or broker_default type)) ? 0 : 1)
 */
#define BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE(host1, host2, client_type) \
        ((BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type) || \
          ((host1 != NULL && strcmp (host1, host2)) && \
           (BOOT_CSQL_CLIENT_TYPE(client_type) \
            || BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type)))) ? 0 : 1)

#define BOOT_IS_PREFERRED_HOSTS_SET(credential) \
        ((credential)->preferred_hosts != NULL \
        && (credential)->preferred_hosts[0] != '\0')

typedef struct boot_client_credential BOOT_CLIENT_CREDENTIAL;
struct boot_client_credential
{
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
};

typedef struct boot_db_path_info BOOT_DB_PATH_INFO;
struct boot_db_path_info
{
  char *db_path;
  char *vol_path;
  char *log_path;
  char *lob_path;
  char *db_host;
  char *db_comments;
};

/*
 * HA server state
 */
enum ha_server_state
{
  HA_SERVER_STATE_NA = -1,	/* N/A */
  HA_SERVER_STATE_IDLE = 0,	/* initial state */
  HA_SERVER_STATE_ACTIVE = 1,
  HA_SERVER_STATE_TO_BE_ACTIVE = 2,
  HA_SERVER_STATE_STANDBY = 3,
  HA_SERVER_STATE_TO_BE_STANDBY = 4,
  HA_SERVER_STATE_MAINTENANCE = 5,	/* maintenance mode */
  HA_SERVER_STATE_DEAD = 6	/* server is dead - virtual state; not exists */
};
typedef enum ha_server_state HA_SERVER_STATE;
#define HA_SERVER_STATE_IDLE_STR                "idle"
#define HA_SERVER_STATE_ACTIVE_STR              "active"
#define HA_SERVER_STATE_TO_BE_ACTIVE_STR        "to-be-active"
#define HA_SERVER_STATE_STANDBY_STR             "standby"
#define HA_SERVER_STATE_TO_BE_STANDBY_STR       "to-be-standby"
#define HA_SERVER_STATE_MAINTENANCE_STR         "maintenance"
#define HA_SERVER_STATE_DEAD_STR                "dead"

typedef struct boot_server_credential BOOT_SERVER_CREDENTIAL;
struct boot_server_credential
{
  char *db_full_name;		/* PATH_MAX */
  char *host_name;		/* MAXHOSTNAMELEN */
  char *lob_path;		/* PATH_MAX + LOB_PATH_PREFIX_MAX */
  int process_id;
  OID root_class_oid;
  HFID root_class_hfid;
  PGLENGTH page_size;
  PGLENGTH log_page_size;
  float disk_compatibility;
  HA_SERVER_STATE ha_server_state;	/* HA_SERVER_STATE */
  char server_session_key[SERVER_SESSION_KEY_SIZE];
  int db_charset;
  char *db_lang;
};

extern char boot_Host_name[MAXHOSTNAMELEN];

#define LOB_PATH_PREFIX_MAX     ES_URI_PREFIX_MAX
#define LOB_PATH_DEFAULT_PREFIX ES_POSIX_PATH_PREFIX

#endif /* _BOOT_H_ */
