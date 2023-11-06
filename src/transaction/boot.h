/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * boot.h - Boot management common header
 */

#ifndef _BOOT_H_
#define _BOOT_H_

#include "client_credentials.hpp"
#include "es_common.h"
#include "porting.h"
#include "storage_common.h"

#include <stdio.h>

// *INDENT-OFF*
#define BOOT_NORMAL_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_DEFAULT \
         || (client_type) == DB_CLIENT_TYPE_CSQL \
         || (client_type) == DB_CLIENT_TYPE_READ_ONLY_CSQL \
         || (client_type) == DB_CLIENT_TYPE_SKIP_VACUUM_CSQL \
         || (client_type) == DB_CLIENT_TYPE_BROKER \
         || (client_type) == DB_CLIENT_TYPE_READ_ONLY_BROKER \
         || (client_type) == DB_CLIENT_TYPE_RW_BROKER_REPLICA_ONLY \
         || (client_type) == DB_CLIENT_TYPE_RO_BROKER_REPLICA_ONLY \
         || (client_type) == DB_CLIENT_TYPE_LOADDB_UTILITY)

#define BOOT_READ_ONLY_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_READ_ONLY_CSQL \
         || (client_type) == DB_CLIENT_TYPE_READ_ONLY_BROKER \
         || (client_type) == DB_CLIENT_TYPE_SLAVE_ONLY_BROKER \
         || (client_type) == DB_CLIENT_TYPE_RO_BROKER_REPLICA_ONLY \
         || (client_type) == DB_CLIENT_TYPE_SO_BROKER_REPLICA_ONLY)

#define BOOT_ADMIN_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_ADMIN_UTILITY \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_REBUILD_CATALOG \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_WOS \
         || (client_type) == DB_CLIENT_TYPE_SKIP_VACUUM_ADMIN_CSQL \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_COMPACTDB_WOS \
	 || (client_type) == DB_CLIENT_TYPE_ADMIN_LOADDB_COMPAT)

#define BOOT_LOG_REPLICATOR_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_LOG_COPIER \
         || (client_type) == DB_CLIENT_TYPE_LOG_APPLIER)

#define BOOT_CSQL_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_CSQL \
         || (client_type) == DB_CLIENT_TYPE_READ_ONLY_CSQL \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_REBUILD_CATALOG \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_WOS \
         || (client_type) == DB_CLIENT_TYPE_SKIP_VACUUM_CSQL \
         || (client_type) == DB_CLIENT_TYPE_SKIP_VACUUM_ADMIN_CSQL)

/* DB_CLIENT_TYPE_ADMIN_CSQL_REBUILD_CATALOG is excluded
 * for using the `--sysadm_rebuild_catalog` option of the csql utility.
 *
 * See CBRD-24781 for the details.
 */
#define BOOT_ADMIN_CSQL_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_ADMIN_CSQL \
         || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_WOS \
         || (client_type) == DB_CLIENT_TYPE_SKIP_VACUUM_ADMIN_CSQL)

#define BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_DEFAULT \
         || (client_type) == DB_CLIENT_TYPE_BROKER \
         || (client_type) == DB_CLIENT_TYPE_READ_ONLY_BROKER \
         || (client_type) == DB_CLIENT_TYPE_SLAVE_ONLY_BROKER \
         || BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE (client_type) \
         || (client_type) == DB_CLIENT_TYPE_LOADDB_UTILITY)

#define BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE(client_type) \
        ((client_type) == DB_CLIENT_TYPE_RW_BROKER_REPLICA_ONLY \
         || (client_type) == DB_CLIENT_TYPE_RO_BROKER_REPLICA_ONLY \
         || (client_type) == DB_CLIENT_TYPE_SO_BROKER_REPLICA_ONLY)

#define BOOT_WRITE_ON_STANDY_CLIENT_TYPE(client_type) \
       ((client_type) == DB_CLIENT_TYPE_LOG_APPLIER \
        || (client_type) == DB_CLIENT_TYPE_RW_BROKER_REPLICA_ONLY \
        || (client_type) == DB_CLIENT_TYPE_ADMIN_CSQL_WOS \
        || (client_type) == DB_CLIENT_TYPE_ADMIN_COMPACTDB_WOS)

/*
 * BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE()
 * ((broker_default type || (remote && csql or broker_default type)) ? 0 : 1)
 */
#define BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE(host1, host2, client_type) \
        ((BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type) || \
          ((host1 != NULL && strcmp (host1, host2)) && (BOOT_CSQL_CLIENT_TYPE(client_type) || BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE(client_type))) \
         ) ? 0 : 1)

#define BOOT_IS_PREFERRED_HOSTS_SET(credential) ((credential)->preferred_hosts != NULL && (credential)->preferred_hosts[0] != '\0')

// *INDENT-ON*

typedef struct boot_db_path_info BOOT_DB_PATH_INFO;
struct boot_db_path_info
{
  const char *db_path;
  const char *vol_path;
  const char *log_path;
  const char *lob_path;
  const char *db_host;
  const char *db_comments;
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
  char *host_name;		/* CUB_MAXHOSTNAMELEN */
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

extern char boot_Host_name[CUB_MAXHOSTNAMELEN];

#define LOB_PATH_PREFIX_MAX     ES_URI_PREFIX_MAX
#define LOB_PATH_DEFAULT_PREFIX ES_POSIX_PATH_PREFIX
#define ES_DEFAULT_TYPE         ES_POSIX

// Inline functions
inline void COMPOSE_FULL_NAME (char *buf, size_t buf_size, const char *path, const char *name);
inline void boot_remove_useless_path_separator (const char *path, char *new_path);

//
// Inline implementation
//

/* Compose the full name of a database */

inline void
COMPOSE_FULL_NAME (char *buf, size_t buf_size, const char *path, const char *name)
{
  size_t len = strlen (path);
  int ret;
  if (len > 0 && path[len - 1] != PATH_SEPARATOR)
    {
      ret = snprintf (buf, buf_size - 1, "%s%c%s", path, PATH_SEPARATOR, name);
    }
  else
    {
      ret = snprintf (buf, buf_size - 1, "%s%s", path, name);
    }
  if (ret < 0)
    {
      abort ();
    }
}

/*
 * boot_remove_useless_path_separator () - Remove useless PATH_SEPARATOR in path string
 *
 * return : true or false(in case of fail)
 *
 *   path(in): Original path.
 *   new_path(out): Transformed path.
 *
 * Note: This function removes useless PATH_SEPARATOR in path string.
 *       For example,
 *       /home3/CUBRID/DB/               -->  /home3/CUBRID/DB
 *       C:\CUBRID\\\Databases\\         -->  C:\CUBRID\Databases
 *       \\pooh\user\                    -->  \\pooh\user
 *
 *       After transform..
 *       If new path string is "/" or "\", don't remove the last slash.
 *       It is survived.
 */
inline void
boot_remove_useless_path_separator (const char *path, char *new_path)
{
  int slash_num = 0;		/* path separator counter */

  /* path must be not null */
  assert (path != NULL);
  assert (new_path != NULL);

  /*
   * Before transform.
   *   / h o m e 3 / / w o r k / c u b r i d / / / w o r k /
   *
   * After transform.
   *   / h o m e 3   / w o r k / c u b r i d     / w o r k
   */

  /* Consume the preceding continuous slash chars. */
  while (*path == PATH_SEPARATOR)
    {
      slash_num++;
      path++;
    }

  /* If there is preceding consumed slash, append PATH_SEPARATOR */
  if (slash_num)
    {
      *new_path++ = PATH_SEPARATOR;
#if defined(WINDOWS)
      /*
       * In Windows/NT,
       * If first duplicated PATH_SEPARATORs are appeared, they are survived.
       * For example,
       * \\pooh\user\ -> \\pooh\user(don't touch the first duplicated PATH_SEPARATORs)
       */
      if (slash_num > 1)
	{
	  *new_path++ = PATH_SEPARATOR;
	}
#endif /* WINDOWS */
    }

  /* Initialize separator counter again. */
  slash_num = 0;

  /*
   * If current character is PATH_SEPARATOR,
   *    skip after increasing separator counter.
   * If current character is normal character, copy to new_path.
   */
  while (*path)
    {
      if (*path == PATH_SEPARATOR)
	{
	  slash_num++;
	}
      else
	{
	  /*
	   * If there is consumed slash, append PATH_SEPARATOR.
	   * Initialize separator counter.
	   */
	  if (slash_num)
	    {
	      *new_path++ = PATH_SEPARATOR;
	      slash_num = 0;
	    }
	  *new_path++ = *path;
	}
      path++;
    }

  /* Assure null terminated string */
  *new_path = '\0';
}
#endif /* _BOOT_H_ */
