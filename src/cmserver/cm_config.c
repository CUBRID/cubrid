/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * config.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef WIN32
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "cm_porting.h"
#include "cm_config.h"
#include "cm_dstring.h"
#include "cm_server_util.h"

#define DEFAULT_MONITOR_INTERVAL 5
#define DEFAULT_PSERVER_PORT	8001
#define DEFAULT_FSERVER_PORT	8002
#define DEFAULT_ALLOW_MULTI_CON  0	/* no */
#define DEFAULT_AUTOSTART_UNICAS 0	/* no */
/* Reject multi connection with "ALL USER" */

#ifdef DIAG_DEVEL
#define NUM_DBMT_FILE	28
#else
#define NUM_DBMT_FILE	24
#endif

char *autobackup_conf_entry[AUTOBACKUP_CONF_ENTRY_NUM] = {
  "dbname", "backupid", "path", "period_type", "period_date", "time",
  "level", "archivedel", "updatestatus", "storeold", "onoff",
  "zip", "check", "mt"
};
char *autoaddvol_conf_entry[AUTOADDVOL_CONF_ENTRY_NUM] = {
  "dbname", "data", "data_warn_outofspace", "data_ext_page",
  "index", "index_warn_outofspace", "index_ext_page"
};
char *autohistory_conf_entry[AUTOHISTORY_CONF_ENTRY_NUM] = {
  "onoff",
  "startyear", "startmonth", "startday",
  "starthour", "startminute", "startsecond",
  "endyear", "endmonth", "endday",
  "endhour", "endminute", "endsecond",
  "memory", "cpu"
};
char *autounicas_conf_entry[AUTOUNICAS_CONF_ENTRY_NUM] = {
  "bname", "cpumonitor", "busymonitor", "logcpu", "logbusy",
  "cpurestart", "busyrestart", "cpulimit", "busytimelimit"
};

static T_DBMT_FILE_INFO dbmt_file[NUM_DBMT_FILE] = {
  {FID_DBMT_CONF, DBMT_CONF_DIR, "cm.conf"},
  {FID_FSERVER_PID, DBMT_PID_DIR, DBMT_CUB_JS_PID},
  {FID_PSERVER_PID, DBMT_PID_DIR, DBMT_CUB_AUTO_PID},
  {FID_FSERVER_ACCESS_LOG, DBMT_LOG_DIR, "cub_js.access.log"},
  {FID_PSERVER_ACCESS_LOG, DBMT_LOG_DIR, "cub_auto.access.log"},
  {FID_FSERVER_ERROR_LOG, DBMT_LOG_DIR, "cub_js.error.log"},
  {FID_PSERVER_ERROR_LOG, DBMT_LOG_DIR, "cub_auto.error.log"},
  {FID_DBMT_PASS, DBMT_CONF_DIR, "cm.pass"},
  {FID_DBMT_CUBRID_PASS, DBMT_CONF_DIR, "cmdb.pass"},
  {FID_CONN_LIST, DBMT_LOG_DIR, "conlist"},
  {FID_AUTO_ADDVOLDB_CONF, DBMT_CONF_DIR, "autoaddvoldb.conf"},
  {FID_AUTO_ADDVOLDB_LOG, DBMT_LOG_DIR, "autoaddvoldb.log"},
  {FID_AUTO_BACKUPDB_CONF, DBMT_CONF_DIR, "autobackupdb.conf"},
  {FID_AUTO_HISTORY_CONF, DBMT_CONF_DIR, "autohistory.conf"},
  {FID_AUTO_UNICASM_CONF, DBMT_CONF_DIR, "autounicasm.conf"},
  {FID_AUTO_EXECQUERY_CONF, DBMT_CONF_DIR, "autoexecquery.conf"},
  {FID_PSVR_DBINFO_TEMP, DBMT_LOG_DIR, "cmdbinfo.temp"},
  {FID_LOCK_CONN_LIST, DBMT_TMP_DIR, "conlist.lock"},
  {FID_LOCK_PSVR_DBINFO, DBMT_TMP_DIR, "cmdbinfo.lock"},
  {FID_LOCK_SVR_LOG, DBMT_TMP_DIR, "cmlog.lock"},
  {FID_LOCK_DBMT_PASS, DBMT_TMP_DIR, "cmpass.lock"},
  {FID_UC_AUTO_RESTART_LOG, DBMT_LOG_DIR, "casautorestart.log"},
  {FID_PSERVER_PID_LOCK, DBMT_TMP_DIR, "cmauto.pid.lock"}
#ifdef DIAG_DEVEL
  ,
  {FID_DIAG_ACTIVITY_LOG, DBMT_CONF_DIR, "diagactivitylog.conf"},
  {FID_DIAG_STATUS_TEMPLATE, DBMT_CONF_DIR, "diagstatustemplate.conf"},
  {FID_DIAG_ACTIVITY_TEMPLATE, DBMT_CONF_DIR, "diagactivitytemplate.conf"},
  {FID_DIAG_SERVER_PID, DBMT_LOG_DIR, "diag.pid"}
#endif
};

sys_config sco;

static int check_file (char *fname, FILE * log_fp);
static int check_path (char *path, FILE * log_fp);

void
sys_config_init ()
{
  memset (&sco, 0, sizeof (sco));
}

int
uReadEnvVariables (char *progname, FILE * log_fp)
{
  sco.szCubrid = getenv ("CUBRID");

  sco.szCubrid_databases = getenv ("CUBRID_DATABASES");

  sco.szProgname = strdup (progname);	/* not an env variable */
  if (sco.szCubrid == NULL)
    {
      fprintf (log_fp, "ERROR : Environment variable CUBRID not set.\n");
      return -1;
    }
  if (sco.szCubrid_databases == NULL)
    {
      fprintf (log_fp,
	       "ERROR : Environment variable CUBRID_DATABASES not set.\n");
      return -1;
    }

  sco.dbmt_tmp_dir =
    (char *) malloc (strlen (sco.szCubrid) + strlen (DBMT_TMP_DIR) + 2);
  if (sco.dbmt_tmp_dir == NULL)
    {
      perror ("malloc");
      return -1;
    }
  sprintf (sco.dbmt_tmp_dir, "%s/%s", sco.szCubrid, DBMT_TMP_DIR);

  return 1;
}

/* Read cm.conf and fill system configuration structure */
/* It fills the global variable 'sco' */
int
uReadSystemConfig (void)
{
  FILE *conf_file;
  char cbuf[1024];
  char ent_name[128], ent_val[128];
  int cm_port = 0;

  conf_file = fopen (conf_get_dbmt_file (FID_DBMT_CONF, cbuf), "rt");
  if (conf_file == NULL)
    return -1;

  sco.iFsvr_port = DEFAULT_FSERVER_PORT;
  sco.iPsvr_port = DEFAULT_PSERVER_PORT;
  sco.iMonitorInterval = DEFAULT_MONITOR_INTERVAL;
  sco.iAllow_AdminMultiCon = DEFAULT_ALLOW_MULTI_CON;
  sco.iAutoStart_UniCAS = DEFAULT_AUTOSTART_UNICAS;

  while (fgets (cbuf, 1024, conf_file))
    {
      ut_trim (cbuf);
      if (cbuf[0] == '\0' || cbuf[0] == '#')
	continue;

      if (sscanf (cbuf, "%s %s", ent_name, ent_val) < 2)
	continue;

      if (strcasecmp (ent_name, "cm_port") == 0)
	{
	  cm_port = atoi (ent_val);
	  sco.iPsvr_port = cm_port;
	  sco.iFsvr_port = cm_port + 1;
	}
      else if (strcasecmp (ent_name, "PortFsvr") == 0 ||
	       strcasecmp (ent_name, "port_job_server") == 0)
	{
	  if (cm_port <= 0)
	    sco.iFsvr_port = atoi (ent_val);
	}
      else if (strcasecmp (ent_name, "PortPsvr") == 0 ||
	       strcasecmp (ent_name, "port_auto_server") == 0)
	{
	  if (cm_port <= 0)
	    sco.iPsvr_port = atoi (ent_val);
	}
      else if (strcasecmp (ent_name, "MonitorInterval") == 0 ||
	       strcasecmp (ent_name, "monitor_interval") == 0)
	{
	  sco.iMonitorInterval = atoi (ent_val);
	}
      else if (strcasecmp (ent_name, "Allow_UserMultiCon") == 0 ||
	       strcasecmp (ent_name, "allow_user_multi_connection") == 0)
	{
	  if (strcasecmp (ent_val, "yes") == 0)
	    sco.iAllow_AdminMultiCon = 1;
	  else
	    sco.iAllow_AdminMultiCon = 0;
	}
      else if (strcasecmp (ent_name, "AutoStart_UniCAS") == 0 ||
	       strcasecmp (ent_name, "auto_start_broker") == 0)
	{
	  if (strcasecmp (ent_val, "yes") == 0)
	    sco.iAutoStart_UniCAS = 1;
	  else
	    sco.iAutoStart_UniCAS = 0;
	}
    }
  fclose (conf_file);

#ifdef HOST_MONITOR_PROC
  sco.hmtab1 = 1;
  sco.hmtab2 = 1;
  sco.hmtab3 = 1;
#ifdef HOST_MONITOR_IO
  sco.hmtab4 = 1;
#else
  sco.hmtab4 = 0;
#endif /* HOST_MONITOR_IO */
#else
  sco.hmtab1 = 0;
  sco.hmtab2 = 0;
  sco.hmtab3 = 0;
  sco.hmtab4 = 0;
#endif /* HOST_MONITOR_PROC */

  /* check value range of system parameters */
  if (sco.iMonitorInterval < DEFAULT_MONITOR_INTERVAL)
    sco.iMonitorInterval = DEFAULT_MONITOR_INTERVAL;

  return 1;
}

/* Check system configuration */
/* It is to be called after uReadSystemConfig() */
int
uCheckSystemConfig (FILE * log_fp)
{
  int retval;
#ifndef WIN32
  struct stat statbuf;
  uid_t userid, ownerid;
#endif
  char filepath[1024];

#ifndef WIN32
  /* CUBRID user id check */
  userid = getuid ();
  stat (sco.szCubrid, &statbuf);
  ownerid = statbuf.st_uid;
#if 0
  fprintf (log_fp, "Current User id : %d\n", (int) userid);
  fprintf (log_fp, "CUBRID  User id : %d\n", (int) ownerid);
#endif
  if (userid != ownerid)
    {
      fprintf (log_fp, "Warning");
      fprintf (log_fp,
	       " -> Current user(%d) does not match CUBRID user(%d).\n",
	       (int) userid, (int) ownerid);
      fprintf (log_fp,
	       "    Server may have unpredictable behavior. Continuing...\n");
    }
#endif

  /* CUBRID databases.txt file check */
  sprintf (filepath, "%s/%s", sco.szCubrid_databases, CUBRID_DATABASE_TXT);
  if (access (filepath, F_OK) < 0)
    {
      FILE *fp;
      fp = fopen (filepath, "w");
      if (fp)
	fclose (fp);
    }
  retval = check_file (filepath, log_fp);
  if (retval < 0)
    return -1;

  if (check_file (conf_get_dbmt_file (FID_DBMT_PASS, filepath), log_fp) < 0)
    return -1;
  if (check_file (conf_get_dbmt_file (FID_DBMT_CUBRID_PASS, filepath), log_fp)
      < 0)
    return -1;

  sprintf (filepath, "%s/%s", sco.szCubrid, DBMT_LOG_DIR);
  if (check_path (filepath, log_fp) < 0)
    return -1;
  sprintf (filepath, "%s/%s", sco.szCubrid, DBMT_CONF_DIR);
  if (check_path (filepath, log_fp) < 0)
    return -1;
  sprintf (filepath, "%s/%s", sco.szCubrid, DBMT_TMP_DIR);
  if (check_path (filepath, log_fp) < 0)
    return -1;

  return 1;
}

char *
conf_get_dbmt_file (T_DBMT_FILE_ID dbmt_fid, char *buf)
{
  int i;

  buf[0] = '\0';
  for (i = 0; i < NUM_DBMT_FILE; i++)
    {
      if (dbmt_fid == dbmt_file[i].fid)
	{
	  sprintf (buf, "%s/%s/%s",
		   sco.szCubrid, dbmt_file[i].dir_name,
		   dbmt_file[i].file_name);
	  break;
	}
    }
  return buf;
}

char *
conf_get_dbmt_file2 (T_DBMT_FILE_ID dbmt_fid, char *buf)
{
  int i;

  buf[0] = '\0';
  for (i = 0; i < NUM_DBMT_FILE; i++)
    {
      if (dbmt_fid == dbmt_file[i].fid)
	{
	  strcpy (buf, dbmt_file[i].file_name);
	  break;
	}
    }
  return buf;

}

int
auto_conf_delete (T_DBMT_FILE_ID fid, char *dbname)
{
  char conf_file[512], tmpfile[512];
  char conf_dbname[128];
  char strbuf[1024];
  FILE *infp, *outfp;

  conf_get_dbmt_file (fid, conf_file);
  if ((infp = fopen (conf_file, "r")) == NULL)
    return -1;
  sprintf (tmpfile, "%s/DBMT_task_ac_del.%d", sco.dbmt_tmp_dir,
	   (int) getpid ());
  if ((outfp = fopen (tmpfile, "w")) == NULL)
    {
      fclose (infp);
      return -1;
    }

  while (fgets (strbuf, sizeof (strbuf), infp))
    {
      if (sscanf (strbuf, "%s", conf_dbname) < 1)
	continue;
      if (strcmp (dbname, conf_dbname) != 0)
	fputs (strbuf, outfp);
    }
  fclose (infp);
  fclose (outfp);

  if (move_file (tmpfile, conf_file) < 0)
    return -1;
  return 0;
}

int
auto_conf_rename (T_DBMT_FILE_ID fid, char *src_dbname, char *dest_dbname)
{
  char conf_file[512], tmpfile[512];
  char conf_dbname[128];
  char strbuf[1024], *p;
  FILE *infp, *outfp;

  conf_get_dbmt_file (fid, conf_file);
  if ((infp = fopen (conf_file, "r")) == NULL)
    return -1;
  sprintf (tmpfile, "%s/DBMT_task_ac_ren.%d", sco.dbmt_tmp_dir,
	   (int) getpid ());
  if ((outfp = fopen (tmpfile, "w")) == NULL)
    {
      fclose (infp);
      return -1;
    }

  while (fgets (strbuf, sizeof (strbuf), infp))
    {
      if (sscanf (strbuf, "%s", conf_dbname) < 1)
	continue;
      if (strcmp (conf_dbname, src_dbname) == 0)
	{
	  p = strstr (strbuf, src_dbname);
	  p += strlen (src_dbname);
	  fprintf (outfp, "%s%s", dest_dbname, p);
	}
      else
	fputs (strbuf, outfp);
    }
  fclose (infp);
  fclose (outfp);

  if (move_file (tmpfile, conf_file) < 0)
    return -1;
  return 0;
}

static int
check_file (char *fname, FILE * log_fp)
{
#if 0
  fprintf (log_fp, "Checking %s ... ", fname);
#endif

  if (access (fname, F_OK | R_OK | W_OK) < 0)
    {
      fprintf (log_fp, "\n   Error : %s : %s\n", fname, strerror (errno));
      return -1;
    }
#if 0
  fprintf (log_fp, "found. OK.\n");
#endif
  return 1;
}

static int
check_path (char *dirname, FILE * log_fp)
{
#if 0
  fprintf (log_fp, "Checking %s ... ", dirname);
#endif

  /* check if directory exists */
  if (access (dirname, F_OK | W_OK | R_OK | X_OK) < 0)
    {
      fprintf (log_fp, "\n   Error : %s : %s\n", dirname, strerror (errno));
      return -1;
    }
#if 0
  fprintf (log_fp, "OK\n");
#endif
  return 1;
}
