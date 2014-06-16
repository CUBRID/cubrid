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
 * connection_globals.c - The global variable, function definitions used by connection
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"
#include "memory_alloc.h"
#include "boot.h"
#include "connection_globals.h"
#include "utility.h"
#include "system_parameter.h"

const char *css_Service_name = "cubrid";
int css_Service_id = 1523;

SOCKET css_Pipe_to_master = INVALID_SOCKET;	/* socket for Master->Slave communication */

/* Stuff for the new client/server/master protocol */
int css_Server_inhibit_connection_socket = 0;
SOCKET css_Server_connection_socket = INVALID_SOCKET;

/* For Windows, we only support the new style of connection protocol. */
#if defined(WINDOWS)
int css_Server_use_new_connection_protocol = 1;
#else /* WINDOWS */
int css_Server_use_new_connection_protocol = 0;
#endif /* WINDOWS */

/* do not change first 4 bytes of css_Net_magic */
char css_Net_magic[CSS_NET_MAGIC_SIZE] =
  { 0x00, 0x00, 0x00, 0x01, 0x20, 0x08, 0x11, 0x22 };

static bool css_Is_conn_rules_initialized = false;

static int css_get_normal_client_max_conn (void);
static int css_get_admin_client_max_conn (void);
static int css_get_ha_client_max_conn (void);

static bool css_is_normal_client (BOOT_CLIENT_TYPE client_type);
static bool css_is_admin_client (BOOT_CLIENT_TYPE client_type);
static bool css_is_ha_client (BOOT_CLIENT_TYPE client_type);

static int css_get_required_conn_num_for_ha (void);

CSS_CONN_RULE_INFO css_Conn_rules[] = {
  {css_is_normal_client,
   css_get_normal_client_max_conn,
   CR_NORMAL_ONLY, 0, 0},
  {css_is_admin_client,
   css_get_admin_client_max_conn,
   CR_NORMAL_FIRST, 0, 0},
  {css_is_ha_client,
   css_get_ha_client_max_conn,
   CR_RESERVED_FIRST, 0, 0}
};
const int css_Conn_rules_size = DIM (css_Conn_rules);

/*
 * css_get_normal_client_max_conn() -
 *   return: max_clients set by user
 */
static int
css_get_normal_client_max_conn (void)
{
  return prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS);
}

/*
 * css_get_admin_client_max_conn() -
 *   return: a num of reserved conn for admin clients
 */
static int
css_get_admin_client_max_conn (void)
{
  return 1;
}

/*
 * css_get_ha_client_max_conn() -
 *   return: a num of reserved conn for HA clients
 */
static int
css_get_ha_client_max_conn (void)
{
  return css_get_required_conn_num_for_ha ();
}

/*
 * css_is_normal_client() -
 *   return: whether a client is a normal client or not
 */
static bool
css_is_normal_client (BOOT_CLIENT_TYPE client_type)
{
  int i;

  for (i = 0; i < css_Conn_rules_size; i++)
    {
      if (i == CSS_CR_NORMAL_ONLY_IDX)
	{
	  continue;
	}

      if (css_Conn_rules[i].check_client_type_fn (client_type))
	{
	  return false;
	}
    }
  return true;
}

/*
 * css_is_admin_client() -
 *   return: whether a client is a admin client or not
 */
static bool
css_is_admin_client (BOOT_CLIENT_TYPE client_type)
{
  return BOOT_ADMIN_CLIENT_TYPE (client_type);
}

/*
 * css_is_ha_client() -
 *   return: whether a client is a HA client or not
 */
static bool
css_is_ha_client (BOOT_CLIENT_TYPE client_type)
{
  return BOOT_LOG_REPLICATOR_TYPE (client_type);
}

/*
 * css_get_required_conn_num_for_ha() - calculate the number
 *      of connections required for HA
 *   return: the number of connections required for HA
 */
static int
css_get_required_conn_num_for_ha (void)
{
  int required_conn_num = 0, num_of_nodes = 0;
  char *ha_node_list_p = NULL;
  char *ha_replica_list_p = NULL;
  int curr_ha_mode;
  unsigned int prefetcher_max_thread_count = 0;

#if defined (SA_MODE)
  curr_ha_mode = util_get_ha_mode_for_sa_utils ();
#else /* SA_MODE */
  curr_ha_mode = prm_get_integer_value (PRM_ID_HA_MODE);
#endif /* !SA_MODE */

  if (curr_ha_mode == HA_MODE_OFF)
    {
      return 0;
    }

  /* server must prepare that the prefetchlogdb util is executed. */
  prefetcher_max_thread_count =
    prm_get_integer_value (PRM_ID_HA_PREFETCHLOGDB_MAX_THREAD_COUNT);

  ha_node_list_p = prm_get_string_value (PRM_ID_HA_NODE_LIST);
  num_of_nodes = util_get_num_of_ha_nodes (ha_node_list_p);

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_REPLICA)
    {
      /* one applylogdb and prefetchlogdb for each node */
      return num_of_nodes * 2 + prefetcher_max_thread_count;
    }

  ha_replica_list_p = prm_get_string_value (PRM_ID_HA_REPLICA_LIST);
  /* one copylogdb for each replica */
  required_conn_num = util_get_num_of_ha_nodes (ha_replica_list_p);

  /* applylogdb, prefetchlogdb and copylogdb for each node */
  required_conn_num += (num_of_nodes - 1) * 3 + prefetcher_max_thread_count;

  return required_conn_num;
}

/*
 * css_init_conn_rules() - initialize connection rules
 *  which are determined in runtime
 *   return:
 */
void
css_init_conn_rules (void)
{
  int i;

  for (i = 0; i < css_Conn_rules_size; i++)
    {
      assert (css_Conn_rules[i].get_max_conn_num_fn != NULL);

      css_Conn_rules[i].max_num_conn =
	css_Conn_rules[i].get_max_conn_num_fn ();
    }

  css_Is_conn_rules_initialized = true;

  return;
}

/*
 * css_get_max_conn() -
 *   return: max_clients + a total num of reserved connections
 */
int
css_get_max_conn (void)
{
  int i, total = 0;

  if (!css_Is_conn_rules_initialized)
    {
      css_init_conn_rules ();
    }

  for (i = 0; i < css_Conn_rules_size; i++)
    {
      total += css_Conn_rules[i].max_num_conn;
    }

  return total;
}
