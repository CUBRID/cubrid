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
 * broker_admin.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(WIN32)
#elif defined(HPUX)
#include <dl.h>
#else
#include <dlfcn.h>
#endif

#include "cm_porting.h"
#include "cm_config.h"
#include "cm_broker_admin.h"
#include "broker_admin_so.h"
#include "cm_server_util.h"

#ifdef WIN32
#define UC_ADMIN_SO		"brokeradmin.dll"
#else
#define UC_ADMIN_SO		"libbrokeradmin.so"
#endif

#define CHECK_UCA_INIT(ERR_MSG_BUF)		\
	do {					\
	  if (uca_init_flag == 0) {		\
	    if (ERR_MSG_BUF) {			\
	      strcpy(ERR_MSG_BUF, uca_err_msg);	\
	    }					\
	    return -1;				\
	  }					\
	} while (0)

#if defined(WIN32)
#define CP_INIT_ERR_MSG()					\
	do {							\
	  LPSTR	lpMsgBuf;				\
	  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |	\
			FORMAT_MESSAGE_FROM_SYSTEM |		\
			FORMAT_MESSAGE_IGNORE_INSERTS,		\
			NULL,					\
			GetLastError(),				\
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),	\
			(LPTSTR) &lpMsgBuf,			\
			0,					\
			NULL 					\
			);					\
	  sprintf(uca_err_msg, "%s : ", UC_ADMIN_SO);		\
	  strncpy(uca_err_msg + strlen(uca_err_msg), lpMsgBuf, 1000);	\
	  LocalFree(lpMsgBuf);					\
	} while (0)
#elif defined(HPUX)
#define CP_INIT_ERR_MSG()						\
	do {								\
	  strncpy(uca_err_msg, strerror(errno), sizeof(uca_err_msg) - 1); \
	} while (0);
#else
#define CP_INIT_ERR_MSG()						\
	do {								\
	  strncpy(uca_err_msg, dlerror(), sizeof(uca_err_msg) - 1);	\
	} while (0);
#endif

#if defined(WIN32)
#define SH_LIB_LOAD(PATH)		LoadLibrary(PATH)
#elif defined(HPUX)
#define SH_LIB_LOAD(PATH)		shl_load(PATH, BIND_IMMEDIATE, 0)
#else
#define SH_LIB_LOAD(PATH)		dlopen(PATH, RTLD_NOW)
#endif

#if defined(WIN32)
#define SH_LIB_GET_ADDRESS(PTR, MODULE, FUNC_NAME)	\
	do {						\
	  PTR = GetProcAddress((HMODULE) MODULE, FUNC_NAME);	\
	} while (0)
#elif defined(HPUX)
#define SH_LIB_GET_ADDRESS(PTR, MODULE, FUNC_NAME)	\
	do {						\
	  if (shl_findsym((shl_t*) &(MODULE), FUNC_NAME, TYPE_PROCEDURE, &(PTR)) < 0) {		\
	    PTR = NULL;					\
	    errno = ENOSYM;				\
	  }						\
	} while (0)
#else
#define SH_LIB_GET_ADDRESS(PTR, MODULE, FUNC_NAME)	\
	do {						\
	  PTR = dlsym(MODULE, FUNC_NAME);		\
	} while (0)
#endif

typedef enum
{
  UC_ADM_START = 0,
  UC_ADM_STOP = 1,
  UC_ADM_ADD = 2,
  UC_ADM_RESTART = 3,
  UC_ADM_DROP = 4,
  UC_ADM_ON = 5,
  UC_ADM_OFF = 6,
  UC_ADM_SUSPEND = 7,
  UC_ADM_RESUME = 8,
  UC_ADM_JOB_FIRST = 9,
  UC_ADM_JOB_Q_SIZE = 10,
  UC_ADM_AS_INFO = 11,
  UC_ADM_INFO_FREE = 12,
  UC_ADM_BR_INFO = 13,
  UC_ADM_UNICAS_CONF = 14,
  UC_ADM_UNICAS_CONF_FREE = 15,
  UC_ADM_CONF_BROKER_ADD = 16,
  UC_ADM_CHANGE_CONFIG = 17,
  UC_ADM_CHANGER = 18,
  UC_ADM_VERSION = 19,
#ifdef DIAG_DEVEL
  UC_ADM_GET_ACTIVE_SESSION_WITH_OPENED_SHM = 20,
  UC_ADM_BROKER_SHM_OPEN = 21,
  UC_ADM_GET_BR_NUM_WITH_OPENED_SHM = 22,
  UC_ADM_GET_BR_NAME_WITH_OPENED_SHM = 23,
  UC_ADM_AS_SHM_OPEN = 24,
  UC_ADM_GET_AS_NUM_WITH_OPENED_SHM = 25,
  UC_ADM_GET_AS_REQS_RECEIVED_WITH_OPENED_SHM = 26,
  UC_ADM_GET_AS_TRAN_PROCESSED_WITH_OPENED_SHM = 27,
  UC_ADM_SHM_DETACH = 28,
#if 0				/* ACTIVITY PROFILE */
  ,
  UC_ADM_GET_BR_DIAGCONFIG_WITH_OPENED_SHM = 29,
  UC_ADM_SET_BR_DIAGCONFIG_WITH_OPENED_SHM = 30
#endif
#endif
  UC_ADM_DEL_CAS_LOG = 29
} T_UC_ADMIN_CODE;

typedef struct
{
  void *func_p;
  char *func_name;
} T_UC_ADMIN_F;

T_UC_ADMIN_F uc_admin_f[] = {
  {NULL, "uc_start"},		/* UC_ADM_START */
  {NULL, "uc_stop"},		/* UC_ADM_STOP */
  {NULL, "uc_add"},		/* UC_ADM_ADD */
  {NULL, "uc_restart"},		/* UC_ADM_RESTART */
  {NULL, "uc_drop"},		/* UC_ADM_DROP */
  {NULL, "uc_on"},		/* UC_ADM_ON */
  {NULL, "uc_off"},		/* UC_ADM_OFF */
  {NULL, "uc_suspend"},		/* UC_ADM_SUSPEND */
  {NULL, "uc_resume"},		/* UC_ADM_RESUME */
  {NULL, "uc_job_first"},	/* UC_ADM_JOB_FIRST */
  {NULL, "uc_job_queue"},	/* UC_ADM_JOB_Q_SIZE */
  {NULL, "uc_as_info"},		/* UC_ADM_AS_INFO */
  {NULL, "uc_info_free"},	/* UC_ADM_INFO_FREE */
  {NULL, "uc_br_info"},		/* UC_ADM_BR_INFO */
  {NULL, "uc_unicas_conf"},	/* UC_ADM_UNICAS_CONF */
  {NULL, "uc_unicas_conf_free"},	/* UC_ADM_UNICAS_CONF_FREE */
  {NULL, "uc_conf_broker_add"},	/* UC_ADM_CONF_BROKER_ADD */
  {NULL, "uc_change_config"},	/* UC_ADM_CHANGE_CONFIG */
  {NULL, "uc_changer"},		/* UC_ADM_CHANGER */
  {NULL, "uc_version"},		/* UC_ADM_VERSION */
#ifdef DIAG_DEVEL
  {NULL, "uc_get_active_session_with_opened_shm"},
  {NULL, "uc_broker_shm_open"},
  {NULL, "uc_get_br_num_with_opened_shm"},
  {NULL, "uc_get_br_name_with_opened_shm"},
  {NULL, "uc_as_shm_open"},
  {NULL, "uc_get_as_num_with_opened_shm"},
  {NULL, "uc_get_as_reqs_received_with_opened_shm"},
  {NULL, "uc_get_as_tran_processed_with_opened_shm"},
  {NULL, "uc_shm_detach"},
#if 0				/* ACTIVITY PROFILE */
  {NULL, "uc_get_br_diagconfig_with_opened_shm"},
  {NULL, "uc_set_br_diagconfig_with_opened_shm"},
#endif
#endif
  {NULL, "uc_del_cas_log"},	/* UC_ADM_DEL_CAS_LOG */
  {NULL, NULL}
};

#define NUM_UNICAS_FILE 4
static T_DBMT_FILE_INFO unicas_file[NUM_UNICAS_FILE] = {
  {UC_FID_ADMIN_LOG, BROKER_LOG_DIR, "cubrid_broker.log"},
  {UC_FID_UNICAS_CONF, UNICAS_CONF_DIR, "unicas.conf"},
  {UC_FID_CUBRID_CAS_CONF, UNICAS_CONF_DIR, "cubrid_cas.conf"},
  {UC_FID_CUBRID_BROKER_CONF, UNICAS_CONF_DIR, "cubrid_broker.conf"}
};

static void as_info_copy (T_DM_UC_AS_INFO * dest_info, T_AS_INFO * src_info);
static void job_info_copy (T_DM_UC_JOB_INFO * dest_info,
			   T_JOB_INFO * src_info);
static void br_info_copy (T_DM_UC_BR_INFO * dest_info, T_BR_INFO * src_info);
static char *to_upper_str (char *str, char *buf);

static char uca_err_msg[1024];
static char uca_init_flag = 0;

int
uca_init (char *err_msg)
{
  int i;
  void *ucadm;
  char path[512];

  memset (uca_err_msg, 0, sizeof (uca_err_msg));

#if defined(WINDOWS)
  sprintf (path, "%s/bin/%s", sco.szCubrid, UC_ADMIN_SO);
#else
  sprintf (path, "%s/lib/%s", sco.szCubrid, UC_ADMIN_SO);
#endif
  ucadm = SH_LIB_LOAD (path);
  if (ucadm == NULL)
    {
      CP_INIT_ERR_MSG ();
      if (err_msg)
	strcpy (err_msg, uca_err_msg);
      return -1;
    }
  for (i = 0; uc_admin_f[i].func_name != NULL; i++)
    {
      SH_LIB_GET_ADDRESS (uc_admin_f[i].func_p, ucadm,
			  uc_admin_f[i].func_name);
      if (uc_admin_f[i].func_p == NULL)
	{
	  CP_INIT_ERR_MSG ();
	  if (err_msg)
	    strcpy (err_msg, uca_err_msg);
	  return -1;
	}
    }

  uca_init_flag = 1;
  return 0;
}

char *
uca_version ()
{
  if (!uca_init_flag)
    return "";

  return (((T_UC_VERSION_F) uc_admin_f[UC_ADM_VERSION].func_p) ());
}

int
uca_start (char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_START_F) uc_admin_f[UC_ADM_START].func_p) (err_msg));
}

int
uca_stop (char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_STOP_F) uc_admin_f[UC_ADM_STOP].func_p) (err_msg));
}

int
uca_add (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_ADD_F) uc_admin_f[UC_ADM_ADD].func_p) (br_name, err_msg));
}

int
uca_restart (char *br_name, int as_index, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_RESTART_F) uc_admin_f[UC_ADM_RESTART].func_p) (br_name,
								as_index,
								err_msg));
}

int
uca_drop (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_DROP_F) uc_admin_f[UC_ADM_DROP].func_p) (br_name, err_msg));
}

int
uca_on (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_ON_F) uc_admin_f[UC_ADM_ON].func_p) (br_name, err_msg));
}

int
uca_off (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_OFF_F) uc_admin_f[UC_ADM_OFF].func_p) (br_name, err_msg));
}

int
uca_suspend (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_SUSPEND_F) uc_admin_f[UC_ADM_SUSPEND].func_p) (br_name,
								err_msg));
}

int
uca_resume (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);

  return (((T_UC_RESUME_F) uc_admin_f[UC_ADM_RESUME].func_p) (br_name,
							      err_msg));
}

int
uca_job_first (char *br_name, int job_id, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);
#if 0
  return (((T_UC_JOB_FIRST_F) uc_admin_f[UC_ADM_JOB_FIRST].func_p) (br_name,
								    job_id,
								    err_msg));
#endif
  return 0;
}

int
uca_job_q_size (char *br_name, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);
  return (((T_UC_JOB_QUEUE_F) uc_admin_f[UC_ADM_JOB_Q_SIZE].func_p) (br_name,
								     err_msg));
}

int
uca_as_info (char *br_name, T_DM_UC_INFO * br_info,
	     T_DM_UC_INFO * br_job_info, char *err_msg)
{
  T_AS_INFO *as_info = NULL;
  T_JOB_INFO *job_info = NULL;
  int res, i, num_as_info, num_job = 0;
  T_UC_AS_INFO_F as_info_f;

  CHECK_UCA_INIT (err_msg);
  as_info_f = (T_UC_AS_INFO_F) uc_admin_f[UC_ADM_AS_INFO].func_p;

  memset (br_info, 0, sizeof (T_DM_UC_INFO));
  if (br_job_info == NULL)
    num_as_info = as_info_f (br_name, &as_info, NULL, NULL, err_msg);
  else
    {
      memset (br_job_info, 0, sizeof (T_DM_UC_INFO));
      num_as_info =
	as_info_f (br_name, &as_info, &job_info, &num_job, err_msg);
    }
  if (num_as_info < 0)
    return -1;

  br_info->info.as_info =
    (T_DM_UC_AS_INFO *) malloc (sizeof (T_DM_UC_AS_INFO) * num_as_info);
  if (br_info->info.as_info == NULL)
    {
      strcpy (err_msg, strerror (errno));
      res = -1;
      goto as_info_finale;
    }
  memset (br_info->info.as_info, 0, sizeof (T_DM_UC_AS_INFO) * num_as_info);
  if (br_job_info)
    {
      br_job_info->info.job_info =
	(T_DM_UC_JOB_INFO *) malloc (sizeof (T_DM_UC_JOB_INFO) * num_job);
      if (br_job_info->info.job_info == NULL)
	num_job = 0;
      else
	memset (br_job_info->info.job_info, 0,
		sizeof (T_DM_UC_JOB_INFO) * num_job);
      for (i = 0; i < num_job; i++)
	job_info_copy (&(br_job_info->info.job_info[i]), &(job_info[i]));
      br_job_info->num_info = num_job;
    }
  for (i = 0; i < num_as_info; i++)
    {
      br_info->info.as_info[i].id = i + 1;
      as_info_copy (&(br_info->info.as_info[i]), &(as_info[i]));
    }
  br_info->num_info = num_as_info;
  res = num_as_info;

as_info_finale:
  ((T_UC_INFO_FREE_F) uc_admin_f[UC_ADM_INFO_FREE].func_p) (as_info);
  ((T_UC_INFO_FREE_F) uc_admin_f[UC_ADM_INFO_FREE].func_p) (job_info);
  return res;
}

void
uca_as_info_free (T_DM_UC_INFO * br_info, T_DM_UC_INFO * br_job_info)
{
  int i;
  T_DM_UC_AS_INFO *as_info;

  if (br_info)
    {
      as_info = br_info->info.as_info;
      for (i = 0; i < br_info->num_info; i++)
	{
	  FREE_MEM (as_info[i].clt_ip_addr);
	  FREE_MEM (as_info[i].clt_appl_name);
	  FREE_MEM (as_info[i].request_file);
	  FREE_MEM (as_info[i].log_msg);
	}
      FREE_MEM (br_info->info.as_info);
      br_info->num_info = 0;
    }
  if (br_job_info)
    {
      FREE_MEM (br_job_info->info.job_info);
      br_info->num_info = 0;
    }
}

int
uca_br_info (T_DM_UC_INFO * uc_info, char *err_msg)
{
  T_BR_INFO *br_info;
  int num_br, i, res;

  CHECK_UCA_INIT (err_msg);
  memset (uc_info, 0, sizeof (T_DM_UC_INFO));

  num_br =
    ((T_UC_BR_INFO_F) uc_admin_f[UC_ADM_BR_INFO].func_p) (&br_info, err_msg);
  if (num_br < 0)
    return -1;

  uc_info->info.br_info =
    (T_DM_UC_BR_INFO *) malloc (sizeof (T_DM_UC_BR_INFO) * num_br);
  if (uc_info->info.br_info == NULL)
    {
      strcpy (err_msg, strerror (errno));
      res = -1;
      goto br_info_finale;
    }
  memset (uc_info->info.br_info, 0, sizeof (T_DM_UC_BR_INFO) * num_br);
  for (i = 0; i < num_br; i++)
    {
      br_info_copy (&(uc_info->info.br_info[i]), &(br_info[i]));
    }
  uc_info->num_info = num_br;
  res = num_br;

br_info_finale:
  ((T_UC_INFO_FREE_F) uc_admin_f[UC_ADM_INFO_FREE].func_p) (br_info);
  return res;
}

void
uca_br_info_free (T_DM_UC_INFO * uc_info)
{
  int i;
  T_DM_UC_BR_INFO *br_info;
  if (uc_info)
    {
      br_info = uc_info->info.br_info;
      for (i = 0; i < uc_info->num_info; i++)
	{
	  FREE_MEM (br_info[i].name);
	  FREE_MEM (br_info[i].session_timeout);
	  FREE_MEM (br_info[i].log_dir);
	}
      FREE_MEM (uc_info->info.br_info);
      uc_info->num_info = 0;
    }
}

int
uca_unicas_conf (T_DM_UC_CONF * dm_uc_conf, int *ret_mst_shmid, char *err_msg)
{
  T_UC_CONF uc_conf;
  int master_shm_id;

  CHECK_UCA_INIT (err_msg);
  if (((T_UC_UNICAS_CONF_F) uc_admin_f[UC_ADM_UNICAS_CONF].func_p) (&uc_conf,
								    &master_shm_id,
								    err_msg) <
      0)
    {
      return -1;
    }
  dm_uc_conf->num_header = uc_conf.num_header;
  dm_uc_conf->header_conf = (T_DM_UC_CONF_ITEM *) uc_conf.header_conf;
  dm_uc_conf->num_broker = uc_conf.num_broker;
  dm_uc_conf->br_conf = (T_DM_UC_BR_CONF *) uc_conf.br_conf;

  if (ret_mst_shmid)
    *ret_mst_shmid = master_shm_id;

  return 0;
}

void
uca_unicas_conf_free (T_DM_UC_CONF * dm_uc_conf)
{
  T_UC_CONF uc_conf;
  uc_conf.num_header = dm_uc_conf->num_header;
  uc_conf.header_conf = (T_UC_CONF_ITEM *) dm_uc_conf->header_conf;
  uc_conf.num_broker = dm_uc_conf->num_broker;
  uc_conf.br_conf = (T_BR_CONF *) dm_uc_conf->br_conf;
  ((T_UC_UNICAS_CONF_FREE_F) uc_admin_f[UC_ADM_UNICAS_CONF_FREE].
   func_p) (&uc_conf);
  memset (dm_uc_conf, 0, sizeof (T_DM_UC_CONF));
}

int
uca_conf_broker_add (T_DM_UC_CONF * dm_uc_conf, char *br_name, char *err_msg)
{
  T_UC_CONF uc_conf;
  int res;

  CHECK_UCA_INIT (err_msg);
  uc_conf.num_header = dm_uc_conf->num_header;
  uc_conf.header_conf = (T_UC_CONF_ITEM *) dm_uc_conf->header_conf;
  uc_conf.num_broker = dm_uc_conf->num_broker;
  uc_conf.br_conf = (T_BR_CONF *) dm_uc_conf->br_conf;
  res =
    ((T_UC_CONF_BROKER_ADD_F) uc_admin_f[UC_ADM_CONF_BROKER_ADD].
     func_p) (&uc_conf, br_name, err_msg);
  dm_uc_conf->num_broker = uc_conf.num_broker;
  dm_uc_conf->br_conf = (T_DM_UC_BR_CONF *) uc_conf.br_conf;
  return res;
}

int
uca_change_config (T_DM_UC_CONF * dm_uc_conf, char *br_name, char *name,
		   char *value)
{
  T_UC_CONF uc_conf;

  CHECK_UCA_INIT (NULL);
  uc_conf.num_header = dm_uc_conf->num_header;
  uc_conf.header_conf = (T_UC_CONF_ITEM *) dm_uc_conf->header_conf;
  uc_conf.num_broker = dm_uc_conf->num_broker;
  uc_conf.br_conf = (T_BR_CONF *) dm_uc_conf->br_conf;
  ((T_UC_CHANGE_CONFIG_F) uc_admin_f[UC_ADM_CHANGE_CONFIG].func_p) (&uc_conf,
								    br_name,
								    name,
								    value);
  return 0;
}

int
uca_changer (char *br_name, char *name, char *value, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);
  return (((T_UC_CHANGER_F) uc_admin_f[UC_ADM_CHANGER].func_p) (br_name, name,
								value,
								err_msg));
}

int
uca_del_cas_log (char *br_name, int as_id, char *err_msg)
{
  CHECK_UCA_INIT (err_msg);
  return (((T_UC_DEL_CAS_LOG_F) uc_admin_f[UC_ADM_DEL_CAS_LOG].
	   func_p) (br_name, as_id, err_msg));
}

char *
uca_get_file (T_UNICAS_FILE_ID uc_fid, char *buf)
{
  int i;

  buf[0] = '\0';
  for (i = 0; i < NUM_UNICAS_FILE; i++)
    {
      if (uc_fid == unicas_file[i].fid)
	{
	  sprintf (buf, "%s/%s/%s", sco.szCubrid,
		   unicas_file[i].dir_name, unicas_file[i].file_name);
	  break;
	}
    }
  return buf;
}

char *
uca_get_conf_path (char *filename, char *buf)
{
  strcpy (buf, filename);

  if (buf[0] == '/')
    return buf;
#ifdef WIN32
  if (buf[2] == '/' || buf[2] == '\\')
    return buf;
#endif
  sprintf (buf, "%s/%s", sco.szCubrid, filename);
  return buf;
}

char *
uca_cpu_time_str (int t, char *buf)
{
  int min, sec;

  min = t / 60;
  sec = t % 60;
  sprintf (buf, "%d:%02d", min, sec);
  return buf;
}

#ifdef DIAG_DEVEL
int
uca_get_active_session_with_opened_shm (void *shm_br, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UC_GET_ACTIVE_SESSION_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_ACTIVE_SESSION_WITH_OPENED_SHM].
	   func_p) (shm_br, err_msg));
}

void *
uca_broker_shm_open (char *err_msg)
{
  if (uca_init_flag == 0)
    return NULL;
  return (((T_UCA_BROKER_SHM_OPEN) uc_admin_f[UC_ADM_BROKER_SHM_OPEN].
	   func_p) (err_msg));
}

#if 0				/* ACTIVITY PROFILE */
int
uca_get_br_diagconfig_with_opened_shm (void *shm_br, void *c_config,
				       char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_BR_DIAGCONFIG_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_BR_DIAGCONFIG_WITH_OPENED_SHM].
	   func_p) (shm_br, c_config, err_msg));
}

int
uca_set_br_diagconfig_with_opened_shm (void *shm_br, void *c_config,
				       char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_SET_BR_DIAGCONFIG_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_SET_BR_DIAGCONFIG_WITH_OPENED_SHM].
	   func_p) (shm_br, c_config, err_msg));
}
#endif

int
uca_get_br_num_with_opened_shm (void *shm_br, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_BR_NUM_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_BR_NUM_WITH_OPENED_SHM].func_p) (shm_br,
								  err_msg));
}

int
uca_get_br_name_with_opened_shm (void *shm_br, int br_index, char *name,
				 int buffer_size, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_BR_NAME_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_BR_NAME_WITH_OPENED_SHM].func_p) (shm_br,
								   br_index,
								   name,
								   buffer_size,
								   err_msg));
}

void *
uca_as_shm_open (void *shm_br, int br_index, char *err_msg)
{
  if (uca_init_flag == 0)
    return NULL;
  return (((T_UCA_AS_SHM_OPEN) uc_admin_f[UC_ADM_AS_SHM_OPEN].func_p) (shm_br,
								       br_index,
								       err_msg));
}

int
uca_get_as_num_with_opened_shm (void *shm_br, int br_index, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_AS_NUM_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_AS_NUM_WITH_OPENED_SHM].func_p) (shm_br,
								  br_index,
								  err_msg));
}

int
uca_get_as_reqs_received_with_opened_shm (void *shm_as, long long array[],
					  int array_size, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_AS_REQS_RECEIVED_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_AS_REQS_RECEIVED_WITH_OPENED_SHM].
	   func_p) (shm_as, array, array_size, err_msg));
}

int
uca_get_as_tran_processed_with_opened_shm (void *shm_as, long long array[],
					   int array_size, char *err_msg)
{
  CHECK_UCA_INIT (NULL);
  return (((T_UCA_GET_AS_TRAN_PROCESSED_WITH_OPENED_SHM)
	   uc_admin_f[UC_ADM_GET_AS_TRAN_PROCESSED_WITH_OPENED_SHM].
	   func_p) (shm_as, array, array_size, err_msg));
}

int
uca_shm_detach (void *p)
{
  CHECK_UCA_INIT (NULL);
  (((T_UCA_SHM_DETACH) uc_admin_f[UC_ADM_SHM_DETACH].func_p) (p));
  return 1;
}
#endif

T_DM_UC_BR_CONF *
uca_conf_find_broker (T_DM_UC_CONF * uc_conf, char *br_name)
{
  int i;
  char *p;

  if (uc_conf == NULL || br_name == NULL)
    return NULL;
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      p = uca_conf_find (&(uc_conf->br_conf[i]), "%");
      if ((p != NULL) && (strcasecmp (p, br_name) == 0))
	{
	  return (&(uc_conf->br_conf[i]));
	}
    }
  return NULL;
}

char *
uca_conf_find (T_DM_UC_BR_CONF * br_conf, char *name)
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

int
uca_conf_write (T_DM_UC_CONF * uc_conf, char *del_broker, char *_dbmt_error)
{
  char buf[512];
  FILE *fp;
  int i, j;
  struct stat statbuf;

  for (i = 0; i < uc_conf->num_header; i++)
    {
      if (uc_conf->header_conf[i].name == NULL ||
	  uc_conf->header_conf[i].value == NULL)
	{
	  return ERR_MEM_ALLOC;
	}
    }
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      for (j = 0; j < uc_conf->br_conf[i].num; j++)
	{
	  if (uc_conf->br_conf[i].item[j].name == NULL ||
	      uc_conf->br_conf[i].item[j].value == NULL)
	    {
	      return ERR_MEM_ALLOC;
	    }
	}
    }

  uca_get_file (UC_FID_CUBRID_BROKER_CONF, buf);

  if (stat (buf, &statbuf) < 0)
    {
      uca_get_file (UC_FID_CUBRID_CAS_CONF, buf);
      if (stat (buf, &statbuf) < 0)
	{
	  uca_get_file (UC_FID_UNICAS_CONF, buf);
	  if (stat (buf, &statbuf) < 0)
	    {
	      uca_get_file (UC_FID_CUBRID_BROKER_CONF, buf);
	    }
	}
    }

  if ((fp = fopen (buf, "w")) == NULL)
    {
      strcpy (_dbmt_error, buf);
      return ERR_FILE_OPEN_FAIL;
    }
  fprintf (fp, "[broker]\n");
  for (i = 0; i < uc_conf->num_header; i++)
    {
      fprintf (fp, "%-25s =%s\n",
	       uc_conf->header_conf[i].name, uc_conf->header_conf[i].value);
    }
  fprintf (fp, "\n");
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      if ((del_broker != NULL) &&
	  (strcmp (uc_conf->br_conf[i].item[0].value, del_broker) == 0))
	{
	  continue;
	}
      fprintf (fp, "[%s%s]\n", uc_conf->br_conf[i].item[0].name,
	       to_upper_str (uc_conf->br_conf[i].item[0].value, buf));
      for (j = 1; j < uc_conf->br_conf[i].num; j++)
	{
	  if (uc_conf->br_conf[i].item[j].value[0] == '\0')
	    continue;
	  fprintf (fp, "%-25s =%s\n", uc_conf->br_conf[i].item[j].name,
		   uc_conf->br_conf[i].item[j].value);
	}
      fprintf (fp, "\n");
    }
  fclose (fp);
  return ERR_NO_ERROR;
}

static void
as_info_copy (T_DM_UC_AS_INFO * dest_info, T_AS_INFO * src_info)
{
  dest_info->pid = src_info->pid;
  dest_info->num_request = src_info->num_request;
  dest_info->as_port = src_info->as_port;
  dest_info->psize = src_info->psize;
  dest_info->num_thr = src_info->num_thr;
  dest_info->cpu_time = src_info->cpu_time;
  dest_info->pcpu = src_info->pcpu;
  if (src_info->status == AS_STATUS_RESTART)
    dest_info->status = "RESTART";
  else if (src_info->status == AS_STATUS_BUSY)
    dest_info->status = "BUSY";
  else if (src_info->status == AS_STATUS_CLIENT_WAIT)
    dest_info->status = "CLIENT WAIT";
  else if (src_info->status == AS_STATUS_CLOSE_WAIT)
    dest_info->status = "CLOSE WAIT";
  else
    dest_info->status = "IDLE";
  dest_info->last_access_time = src_info->last_access_time;
  dest_info->clt_ip_addr = strdup (src_info->clt_ip_addr);
  dest_info->clt_appl_name = strdup (src_info->clt_appl_name);
  dest_info->request_file = strdup (src_info->request_file);
  dest_info->log_msg = strdup (src_info->log_msg);
}

static void
job_info_copy (T_DM_UC_JOB_INFO * dest_info, T_JOB_INFO * src_info)
{
  dest_info->id = src_info->id;
  dest_info->priority = src_info->priority;
  dest_info->recv_time = src_info->recv_time;
  memcpy (dest_info->ip, src_info->ip, 4);
  strncpy (dest_info->script, src_info->script,
	   sizeof (dest_info->script) - 1);
  strncpy (dest_info->prgname, src_info->prgname,
	   sizeof (dest_info->prgname) - 1);
}

static void
br_info_copy (T_DM_UC_BR_INFO * dest_info, T_BR_INFO * src_info)
{
  char strbuf[64];
  dest_info->name = strdup (src_info->name);
  strcpy (dest_info->as_type, src_info->as_type);
  dest_info->pid = src_info->pid;
  dest_info->port = src_info->port;
  dest_info->num_as = src_info->num_as;
  dest_info->max_as = src_info->max_as;
  dest_info->min_as = src_info->min_as;
  dest_info->num_job_q = src_info->num_job_q;
  dest_info->num_thr = src_info->num_thr;
  dest_info->pcpu = src_info->pcpu;
  dest_info->cpu_time = src_info->cpu_time;
  dest_info->num_req = src_info->num_req;
  sprintf (strbuf, "%d", src_info->session_timeout);
  dest_info->session_timeout = strdup (strbuf);
  dest_info->sql_log_on_off = src_info->sql_log_on_off;
  dest_info->shm_id = src_info->shm_id;
  dest_info->sql_log_bind_value = src_info->sql_log_bind_value;
  dest_info->sql_log_append_mode = src_info->sql_log_append_mode;
  dest_info->sql_log_time = src_info->sql_log_time;
  if (src_info->status == FLAG_SUSPEND)
    strcpy (dest_info->status, "SUSPENDED");
  else if (src_info->status == FLAG_ON)
    strcpy (dest_info->status, "ON");
  else
    strcpy (dest_info->status, "OFF");
  if (src_info->auto_add_flag == FLAG_ON)
    strcpy (dest_info->auto_add, "ON");
  else
    strcpy (dest_info->auto_add, "OFF");
  dest_info->log_dir = strdup (src_info->log_dir);
  dest_info->as_max_size = src_info->as_max_size;
  dest_info->log_backup = src_info->log_backup_flag;
  dest_info->time_to_kill = src_info->time_to_kill;
  dest_info->access_list_flag = src_info->access_list_flag;
  dest_info->source_env_flag = src_info->source_env_flag;
}

static char *
to_upper_str (char *str, char *buf)
{
  char *p;

  strcpy (buf, str);
  for (p = buf; *p; p++)
    {
      if (*p >= 'a' && *p <= 'z')
	{
	  *p = *p - 'a' + 'A';
	}
    }
  return buf;
}
