/**
 * collectd - cubrid_broker.c
 * Copyright (C) 1999-2008 NHN. 
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   DBMS Development lab.
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/* cubrid broker include files */
#include "cubrid_broker.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "cas_common.h"

#define PLUGIN_NAME	"cubrid_broker"

static int cubrid_broker_init(void);
static int cubrid_broker_read(void);
static void submit(char *type, char *type_instance, value_t *values, int value_cnt);

static int master_shm_id = 0;
static unsigned long *old_transactions_processed = NULL;
static unsigned long *old_queries_processed = NULL;
static time_t old_time;

typedef enum {
  TOTAL,
  BUSY,
  CLOSE_WAIT,
  CLIENT_WAIT,
  IDLE
} CAS_STATUS;

static int cubrid_broker_init(void)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker;
  int i;
  char err_msg[1024], admin_log_file[512];

  if (broker_config_read(NULL, br_info, &num_broker, &master_shm_id,
			 admin_log_file, 0, err_msg)  < 0)
  {
    ERROR("cubrid_broker: %s", err_msg);
    return -1;
  }

  if (old_transactions_processed == NULL)
  {
    old_transactions_processed = (unsigned long *) calloc (sizeof (unsigned long), num_broker);
  }
  if (old_queries_processed == NULL)
  {
    old_queries_processed = (unsigned long *) calloc (sizeof (unsigned long), num_broker);
  }
  for (i = 0; i < num_broker; i++)
  {
    old_transactions_processed[i] = -1;
    old_queries_processed[i] = -1;
  }
  (void) time (&old_time);
  return 0;
}

static int cubrid_broker_read(void)
{
  T_SHM_BROKER	*shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, j;
  int total_trs = 0; 
  int total_qps = 0;
  value_t trs[1];
  value_t trs_for_qps[1];
  value_t cas_status[5];
  time_t cur_time;

  shm_br = (T_SHM_BROKER*) uw_shm_open(master_shm_id, SHM_BROKER, SHM_MODE_MONITOR);
  if (shm_br == NULL) {
    return 0;
  }
  time(&cur_time);
  for (i=0 ; i < shm_br->num_broker ; i++) {
    if (shm_br->br_info[i].service_flag != ON)
      continue;

    memset(trs, 0, sizeof(trs));
    memset(trs_for_qps, 0, sizeof(trs_for_qps));
    memset(cas_status, 0, sizeof(cas_status));

    shm_appl = (T_SHM_APPL_SERVER*)
		uw_shm_open(shm_br->br_info[i].appl_server_shm_id,
			    SHM_APPL_SERVER,
			    SHM_MODE_MONITOR);
 
    if (shm_appl == NULL)
    {
      return 0;
    }
    for (j=0 ; j < shm_br->br_info[i].appl_server_max_num ; j++) {
      total_trs += shm_appl->as_info[j].num_transactions_processed;
      total_qps += shm_appl->as_info[j].num_queries_processed;
 
      if (shm_appl->as_info[j].service_flag == SERVICE_ON) {
        cas_status[TOTAL].gauge++;
	if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY) {
	  if (shm_br->br_info[i].appl_server == APPL_SERVER_CAS) {
	    if (shm_appl->as_info[j].con_status == CON_STATUS_OUT_TRAN)
	      cas_status[CLOSE_WAIT].gauge++;
	    else if (shm_appl->as_info[j].log_msg[0] == '\0')
	      cas_status[CLIENT_WAIT].gauge++;
	    else
	      cas_status[BUSY].gauge++;
	  }
	  else {
	    cas_status[BUSY].gauge++;
	  }
	}
	else {
	  cas_status[IDLE].gauge++;
	}
      }

    }
    uw_shm_detach(shm_appl);

    if (old_transactions_processed[i] == -1)
    {
      trs[0].gauge = 0;
    }
    else
    { 
      trs[0].gauge = ((total_trs - old_transactions_processed[i]) / difftime (cur_time, old_time));
    }
    if (old_queries_processed[i] == -1)
    {
      trs_for_qps[0].gauge = 0;
    }
    else
    {
      trs_for_qps[0].gauge = ((total_qps - old_queries_processed[i]) / difftime (cur_time, old_time));
    }
    old_transactions_processed[i] = total_trs;
    old_queries_processed[i] = total_qps;
 
    total_trs = 0;
    total_qps = 0;

    submit("cubrid_broker_trs", shm_br->br_info[i].name, trs, 1);
    submit("cubrid_broker_qps", shm_br->br_info[i].name, trs_for_qps, 1);
    submit("cubrid_broker_status", shm_br->br_info[i].name, cas_status, 5);
  }
  old_time = cur_time;
  uw_shm_detach(shm_br);
  return 0;
}

static void submit(char *type, char *type_instance, value_t *values, int value_cnt)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = value_cnt;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, PLUGIN_NAME);
  strcpy(vl.plugin_instance, "");
  strcpy (vl.type_instance, type_instance);

  plugin_dispatch_values (type, &vl);
}

void module_register (void)
{
printf("cubrid_broker module register-0\n");
  plugin_register_init(PLUGIN_NAME, cubrid_broker_init);
  plugin_register_read(PLUGIN_NAME, cubrid_broker_read);
}
