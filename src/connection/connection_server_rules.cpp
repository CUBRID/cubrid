
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "connection_server_rules.hpp"
#include "porting.h"
#include "memory_alloc.h"
#include "server_type.hpp"
#include "boot.h"
#include "utility.h"
#include "system_parameter.h"

static int css_get_admin_client_max_conn (void);
static int css_get_ha_client_max_conn (void);

static bool css_is_normal_client (BOOT_CLIENT_TYPE client_type);
static bool css_is_admin_client (BOOT_CLIENT_TYPE client_type);
static bool css_is_ha_client (BOOT_CLIENT_TYPE client_type);

CSS_CONN_RULE_INFO css_Conn_rules[] = {
  {css_is_normal_client, css_get_max_normal_conn, CR_NORMAL_ONLY, 0, 0},
  {css_is_admin_client, css_get_admin_client_max_conn, CR_NORMAL_FIRST, 0, 0},
  {css_is_ha_client, css_get_ha_client_max_conn, CR_RESERVED_FIRST, 0, 0}
};

static bool css_Is_conn_rules_initialized = false;
const int css_Conn_rules_size = DIM (css_Conn_rules);

static int css_get_required_conn_num_for_ha (void);

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
 * css_get_admin_client_max_conn() -
 *   return: a num of reserved conn for admin clients
 */
static int
css_get_admin_client_max_conn (void)
{
  return 1;
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

      css_Conn_rules[i].max_num_conn = css_Conn_rules[i].get_max_conn_num_fn ();
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


/*
 * css_get_max_normal_conn() -
 *    return: max number of client connections
 */
int
css_get_max_normal_conn (void)
{
  if (get_server_type () == SERVER_TYPE_PAGE)
    {
      return 0;
    }
  return prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS);
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

#if defined (SA_MODE)
  curr_ha_mode = util_get_ha_mode_for_sa_utils ();
#else /* SA_MODE */
  curr_ha_mode = HA_GET_MODE ();
#endif /* !SA_MODE */

  if (curr_ha_mode == HA_MODE_OFF)
    {
      return 0;
    }

  ha_node_list_p = prm_get_string_value (PRM_ID_HA_NODE_LIST);
  num_of_nodes = util_get_num_of_ha_nodes (ha_node_list_p);

  if (HA_GET_MODE () == HA_MODE_REPLICA)
    {
      /* one applylogdb for each node */
      return num_of_nodes * 2;
    }

  ha_replica_list_p = prm_get_string_value (PRM_ID_HA_REPLICA_LIST);
  /* one copylogdb for each replica */
  required_conn_num = util_get_num_of_ha_nodes (ha_replica_list_p);

  /* applylogdb and copylogdb for each node */
  required_conn_num += (num_of_nodes - 1) * 3;

  return required_conn_num;
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
