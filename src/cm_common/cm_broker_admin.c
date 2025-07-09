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
 * broker_admin.c -
 */

#ident "$Id$"

#include "cm_stat.h"
#include "cm_errmsg.h"
#include "cm_portable.h"
#include "cm_defines.h"
#define INT64 int64_t		/* broker_admin_so.h need this type */
#include "broker_admin_so.h"
#undef INT64
#include "broker_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#if !defined(WINDOWS)
#include <strings.h>
#endif

#if defined(WINDOWS)
#elif defined(HPUX)
#include <dl.h>
#else
#include <dlfcn.h>
#endif


typedef struct
{
  T_UNICAS_FILE_ID fid;
  char dir_name[200];
  char file_name[100];
} T_UNICAS_FILE_INFO;

#define NUM_UNICAS_FILE 4
static T_UNICAS_FILE_INFO unicas_file[NUM_UNICAS_FILE] = {
  {UC_FID_ADMIN_LOG, BROKER_LOG_DIR, "cubrid_broker.log"},
  {UC_FID_UNICAS_CONF, UNICAS_CONF_DIR, "unicas.conf"},
  {UC_FID_CUBRID_CAS_CONF, UNICAS_CONF_DIR, "cubrid_cas.conf"},
  {UC_FID_CUBRID_BROKER_CONF, UNICAS_CONF_DIR, "cubrid_broker.conf"}
};

static void as_info_copy (T_CM_CAS_INFO * dest_info, T_AS_INFO * src_info);
static void job_info_copy (T_CM_JOB_INFO * dest_info, T_JOB_INFO * src_info);
static void br_info_copy (T_CM_BROKER_INFO * dest_info, T_BR_INFO * src_info);

int
cm_broker_env_start (T_CM_ERROR * err_buf)
{
  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_start (err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;
}

int
cm_broker_env_stop (T_CM_ERROR * err_buf)
{
  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_stop (err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;

}


int
cm_broker_as_restart (const char *br_name, int as_index, T_CM_ERROR * err_buf)
{
  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_restart (br_name, as_index, err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;


}

int
cm_broker_on (const char *br_name, T_CM_ERROR * err_buf)
{

  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_on (br_name, err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;


}

int
cm_broker_off (const char *br_name, T_CM_ERROR * err_buf)
{
  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_off (br_name, err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;


}

int
cm_get_cas_info (const char *br_name, T_CM_CAS_INFO_ALL * cas_info_all, T_CM_JOB_INFO_ALL * job_info_all,
		 T_CM_ERROR * err_buf)
{
  T_AS_INFO *as_info = NULL;
  T_JOB_INFO *job_info = NULL;
  int res, i, num_as_info, num_job = 0;

  cm_err_buf_reset (err_buf);

  memset (cas_info_all, 0, sizeof (T_CM_CAS_INFO_ALL));
  if (job_info_all == NULL)
    {
      num_as_info = uc_as_info (br_name, &as_info, NULL, NULL, err_buf->err_msg);
    }
  else
    {
      memset (job_info_all, 0, sizeof (T_CM_JOB_INFO_ALL));
      num_as_info = uc_as_info (br_name, &as_info, &job_info, &num_job, err_buf->err_msg);
    }
  if (num_as_info < 0)
    {
      err_buf->err_code = CM_GENERAL_ERROR;
      res = -1;
      goto as_info_finale;
    }

  cas_info_all->as_info = (T_CM_CAS_INFO *) malloc (sizeof (T_CM_CAS_INFO) * num_as_info);
  if (cas_info_all->as_info == NULL)
    {
      err_buf->err_code = CM_GENERAL_ERROR;
      strcpy (err_buf->err_msg, strerror (errno));
      res = -1;
      goto as_info_finale;
    }
  memset (cas_info_all->as_info, 0, sizeof (T_CM_CAS_INFO) * num_as_info);
  if (job_info_all != NULL)
    {
      job_info_all->job_info = (T_CM_JOB_INFO *) malloc (sizeof (T_CM_JOB_INFO) * num_job);
      if (job_info_all->job_info == NULL)
	num_job = 0;
      else
	memset (job_info_all->job_info, 0, sizeof (T_CM_JOB_INFO) * num_job);
      for (i = 0; i < num_job; i++)
	job_info_copy (&(job_info_all->job_info[i]), &(job_info[i]));
      job_info_all->num_info = num_job;
    }
  for (i = 0; i < num_as_info; i++)
    {
      cas_info_all->as_info[i].id = i + 1;
      as_info_copy (&(cas_info_all->as_info[i]), &(as_info[i]));
    }
  cas_info_all->num_info = num_as_info;
  res = num_as_info;

as_info_finale:
  uc_info_free (as_info);
  uc_info_free (job_info);
  return res;
}

void
cm_cas_info_free (T_CM_CAS_INFO_ALL * cas_info_all, T_CM_JOB_INFO_ALL * job_info_all)
{
  int i;
  T_CM_CAS_INFO *as_info;

  if (cas_info_all != NULL)
    {
      as_info = cas_info_all->as_info;
      for (i = 0; i < cas_info_all->num_info; i++)
	{
	  FREE_MEM (as_info[i].clt_ip_addr);
	  FREE_MEM (as_info[i].clt_appl_name);
	  FREE_MEM (as_info[i].request_file);
	  FREE_MEM (as_info[i].log_msg);
	  FREE_MEM (as_info[i].database_name);
	  FREE_MEM (as_info[i].database_host);
	  FREE_MEM (as_info[i].service_flag);
	  FREE_MEM (as_info[i].status);
	}
      FREE_MEM (cas_info_all->as_info);
      cas_info_all->num_info = 0;
    }
  if (job_info_all != NULL)
    {
      FREE_MEM (job_info_all->job_info);
    }
}

int
cm_get_broker_info (T_CM_BROKER_INFO_ALL * broker_info_all, T_CM_ERROR * err_buf)
{
  T_BR_INFO *br_info;
  int num_br, i, res;

  cm_err_buf_reset (err_buf);
  memset (broker_info_all, 0, sizeof (T_CM_BROKER_INFO_ALL));

  num_br = uc_br_info (&br_info, err_buf->err_msg);
  if (num_br < 0)
    {
      err_buf->err_code = CM_GENERAL_ERROR;
      return -1;
    }

  broker_info_all->br_info = (T_CM_BROKER_INFO *) malloc (sizeof (T_CM_BROKER_INFO) * num_br);
  if (broker_info_all->br_info == NULL)
    {
      err_buf->err_code = CM_GENERAL_ERROR;
      strcpy (err_buf->err_msg, strerror (errno));
      res = -1;
      goto br_info_finale;
    }
  memset (broker_info_all->br_info, 0, sizeof (T_CM_BROKER_INFO) * num_br);
  for (i = 0; i < num_br; i++)
    {
      br_info_copy (&(broker_info_all->br_info[i]), &(br_info[i]));
    }
  broker_info_all->num_info = num_br;
  res = num_br;

br_info_finale:
  uc_info_free (br_info);
  return res;
}

void
cm_broker_info_free (T_CM_BROKER_INFO_ALL * broker_info_all)
{
  int i;
  T_CM_BROKER_INFO *br_info;
  if (broker_info_all != NULL)
    {
      br_info = broker_info_all->br_info;
      for (i = 0; i < broker_info_all->num_info; i++)
	{
	  FREE_MEM (br_info[i].name);
	  FREE_MEM (br_info[i].as_type);
	  FREE_MEM (br_info[i].session_timeout);
	  FREE_MEM (br_info[i].access_mode);
	  FREE_MEM (br_info[i].status);
	  FREE_MEM (br_info[i].auto_add);
	  FREE_MEM (br_info[i].log_dir);
	  FREE_MEM (br_info[i].keep_connection);
	  FREE_MEM (br_info[i].sql_log_mode);
	}
      FREE_MEM (broker_info_all->br_info);
      broker_info_all->num_info = 0;
    }
}

int
cm_get_broker_conf (T_CM_BROKER_CONF * dm_uc_conf, int *ret_mst_shmid, T_CM_ERROR * err_buf)
{
  T_UC_CONF uc_conf;
  int master_shm_id;

  cm_err_buf_reset (err_buf);
  if (uc_unicas_conf (&uc_conf, &master_shm_id, err_buf->err_msg) < 0)
    {
      err_buf->err_code = CM_GENERAL_ERROR;
      return -1;
    }
  dm_uc_conf->num_header = uc_conf.num_header;
  dm_uc_conf->header_conf = (T_CM_BROKER_CONF_ITEM *) uc_conf.header_conf;
  dm_uc_conf->num_broker = uc_conf.num_broker;
  dm_uc_conf->br_conf = (T_CM_BR_CONF *) uc_conf.br_conf;

  if (ret_mst_shmid != NULL)
    {
      *ret_mst_shmid = master_shm_id;
    }

  return 0;
}

void
cm_broker_conf_free (T_CM_BROKER_CONF * dm_uc_conf)
{
  T_UC_CONF uc_conf;
  uc_conf.num_header = dm_uc_conf->num_header;
  uc_conf.header_conf = (T_UC_CONF_ITEM *) dm_uc_conf->header_conf;
  uc_conf.num_broker = dm_uc_conf->num_broker;
  uc_conf.br_conf = (T_BR_CONF *) dm_uc_conf->br_conf;
  uc_unicas_conf_free (&uc_conf);
  memset (dm_uc_conf, 0, sizeof (T_CM_BROKER_CONF));
}


int
cm_del_cas_log (char *br_name, int as_id, T_CM_ERROR * err_buf)
{
  int ret;
  cm_err_buf_reset (err_buf);

  ret = uc_del_cas_log (br_name, as_id, err_buf->err_msg);
  if (ret < 0)
    err_buf->err_code = CM_GENERAL_ERROR;
  return ret;

}


char *
cm_cpu_time_str (int t, char *buf)
{
  int min, sec;

  min = t / 60;
  sec = t % 60;
  sprintf (buf, "%d:%02d", min, sec);
  return buf;
}


T_CM_BR_CONF *
cm_conf_find_broker (T_CM_BROKER_CONF * uc_conf, char *br_name)
{
  int i;
  char *p;

  if (uc_conf == NULL || br_name == NULL)
    return NULL;
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      p = cm_br_conf_get_value (&(uc_conf->br_conf[i]), "%");
      if ((p != NULL) && (strcasecmp (p, br_name) == 0))
	{
	  return (&(uc_conf->br_conf[i]));
	}
    }
  return NULL;
}

char *
cm_br_conf_get_value (T_CM_BR_CONF * br_conf, const char *name)
{
  int i;

  if (br_conf == NULL)
    return NULL;
  for (i = 0; i < br_conf->num; i++)
    {
      if (strcmp (br_conf->item[i].name, name) == 0)
	return (br_conf->item[i].value);
    }
  return NULL;
}



static void
as_info_copy (T_CM_CAS_INFO * dest_info, T_AS_INFO * src_info)
{
  switch (src_info->service_flag)
    {
    case 1 /* SERVICE_ON */ :
      dest_info->service_flag = strdup ("ON");
      break;
    case 0 /* SERVICE_OFF */ :
      dest_info->service_flag = strdup ("OFF");
      break;
    case 2 /* SERVICE_OFF_ACK */ :
      dest_info->service_flag = strdup ("OFF_ACK");
      break;
    default:
      dest_info->service_flag = strdup ("UNKNOWN");
    }

  dest_info->pid = src_info->pid;
  dest_info->num_request = src_info->num_request;
  dest_info->as_port = src_info->as_port;
  dest_info->psize = src_info->psize;
  dest_info->num_thr = src_info->num_thr;
  dest_info->cpu_time = src_info->cpu_time;
  dest_info->pcpu = src_info->pcpu;
  if (src_info->status == AS_STATUS_RESTART)
    dest_info->status = strdup ("RESTART");
  else if (src_info->status == AS_STATUS_BUSY)
    dest_info->status = strdup ("BUSY");
  else if (src_info->status == AS_STATUS_CLIENT_WAIT)
    dest_info->status = strdup ("CLIENT WAIT");
  else if (src_info->status == AS_STATUS_CLOSE_WAIT)
    dest_info->status = strdup ("CLOSE WAIT");
  else
    dest_info->status = strdup ("IDLE");
  dest_info->last_access_time = src_info->last_access_time;
  dest_info->clt_ip_addr = strdup (src_info->clt_ip_addr);
  dest_info->clt_appl_name = strdup (src_info->clt_appl_name);
  dest_info->request_file = strdup (src_info->request_file);
  dest_info->log_msg = strdup (src_info->log_msg);
  dest_info->database_name = strdup (src_info->database_name);
  dest_info->database_host = strdup (src_info->database_host);
  dest_info->last_connect_time = src_info->last_connect_time;
  dest_info->num_requests_received = src_info->num_requests_received;
  dest_info->num_queries_processed = src_info->num_queries_processed;
  dest_info->num_transactions_processed = src_info->num_transactions_processed;
  dest_info->num_long_queries = src_info->num_long_queries;
  dest_info->num_long_transactions = src_info->num_long_transactions;
  dest_info->num_error_queries = src_info->num_error_queries;
  dest_info->num_interrupts = src_info->num_interrupts;
}

static char *
ip2str (unsigned char *ip, char *ip_str)
{
  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip[0], (unsigned char) ip[1], (unsigned char) ip[2],
	   (unsigned char) ip[3]);
  return ip_str;
}

static void
job_info_copy (T_CM_JOB_INFO * dest_info, T_JOB_INFO * src_info)
{
  dest_info->id = src_info->id;
  dest_info->priority = src_info->priority;
  dest_info->recv_time = src_info->recv_time;
  /* memcpy (dest_info->ip, src_info->ip, 4); */
  ip2str (src_info->ip, dest_info->ipstr);
  strncpy_bufsize (dest_info->script, src_info->script);
  strncpy_bufsize (dest_info->prgname, src_info->prgname);
}

static void
br_info_copy (T_CM_BROKER_INFO * dest_info, T_BR_INFO * src_info)
{
  char strbuf[64];
  dest_info->name = strdup (src_info->name);
  /* strcpy (dest_info->as_type, src_info->as_type); */
  dest_info->as_type = strdup (src_info->as_type);
  dest_info->pid = src_info->pid;
  dest_info->port = src_info->port;
  dest_info->num_as = src_info->num_as;
  dest_info->max_as = src_info->max_as;
  dest_info->min_as = src_info->min_as;
  dest_info->num_job_q = src_info->num_job_q;
  dest_info->num_thr = src_info->num_thr;
  dest_info->pcpu = src_info->pcpu;
  dest_info->cpu_time = src_info->cpu_time;
  dest_info->num_busy_count = src_info->num_busy_count;
  dest_info->num_req = src_info->num_req;
  dest_info->num_tran = src_info->num_tran;
  dest_info->num_query = src_info->num_query;
  dest_info->num_long_tran = src_info->num_long_tran;
  dest_info->num_long_query = src_info->num_long_query;
  dest_info->num_error_query = src_info->num_error_query;
  dest_info->long_query_time = src_info->long_query_time;
  dest_info->long_transaction_time = src_info->long_transaction_time;

  sprintf (strbuf, "%d", src_info->session_timeout);
  dest_info->session_timeout = strdup (strbuf);
  switch (src_info->keep_connection)
    {
    case KEEP_CON_AUTO:
      dest_info->keep_connection = strdup ("AUTO");
      break;
    case KEEP_CON_ON:
      dest_info->keep_connection = strdup ("ON");
      break;
    }
  switch (src_info->sql_log_mode)
    {
    case SQL_LOG_MODE_NONE:
      dest_info->sql_log_mode = strdup ("NONE");
      break;
    case SQL_LOG_MODE_ERROR:
      dest_info->sql_log_mode = strdup ("ERROR");
      break;
    case SQL_LOG_MODE_TIMEOUT:
      dest_info->sql_log_mode = strdup ("TIMEOUT");
      break;
    case SQL_LOG_MODE_NOTICE:
      dest_info->sql_log_mode = strdup ("NOTICE");
      break;
    case SQL_LOG_MODE_ALL:
      dest_info->sql_log_mode = strdup ("ALL");
      break;
    }

  dest_info->shm_id = src_info->shm_id;
  if (src_info->status == FLAG_SUSPEND)
    dest_info->status = strdup ("SUSPENDED");
  else if (src_info->status == FLAG_ON)
    dest_info->status = strdup ("ON");
  else
    dest_info->status = strdup ("OFF");
  if (src_info->auto_add_flag == FLAG_ON)
    dest_info->auto_add = strdup ("ON");
  else
    dest_info->auto_add = strdup ("OFF");
  if (src_info->access_mode == FLAG_READ_WRITE)
    dest_info->access_mode = strdup ("RW");
  else if (src_info->access_mode == FLAG_READ_ONLY)
    dest_info->access_mode = strdup ("RO");
  else
    dest_info->access_mode = strdup ("SO");
  dest_info->log_dir = strdup (src_info->log_dir);
  dest_info->as_max_size = src_info->as_max_size;
  dest_info->log_backup = src_info->log_backup_flag;
  dest_info->time_to_kill = src_info->time_to_kill;
  dest_info->access_list_flag = src_info->access_list_flag;
  dest_info->source_env_flag = src_info->source_env_flag;
}

char *
cm_get_broker_file (T_UNICAS_FILE_ID uc_fid, char *buf)
{
  int i;

  buf[0] = '\0';
  for (i = 0; i < NUM_UNICAS_FILE; i++)
    {
      if (uc_fid == unicas_file[i].fid)
	{
#if !defined (DO_NOT_USE_CUBRIDENV)
	  sprintf (buf, "%s/%s/%s", getenv (CUBRID_ENV), unicas_file[i].dir_name, unicas_file[i].file_name);
#else
	  sprintf (buf, "%s/%s", unicas_file[i].dir_name, unicas_file[i].file_name);
#endif
	  break;
	}
    }
  return buf;
}
