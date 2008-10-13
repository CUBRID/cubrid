/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * autojob.c -
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef WIN32
#include <io.h>
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

#include "dbmt_porting.h"
#include "server_util.h"
#include "autojob.h"
#include "dstring.h"
#include "server_stat.h"
#include "dbmt_config.h"
#include "cmd_exec.h"
#include "unicas_admin.h"
#include "dbmt_user.h"
#include "tea.h"
#include "utility.h"

#ifdef	_DEBUG_
#include "deb.h"
#include <assert.h>
#endif

#define MIN_AUTOBACKUPDB_DELAY		600
#define MIN_AUTO_ADDVOL_PAGE_SIZE	1000
#define MAX_AUTOADD_FREE_SPACE_RATE	0.5

typedef struct autobackupdb_node_t
{
  char *dbname;
  char *backup_id;
  char *path;
  int period_type;
  int period_date;
  int time;
  int level;
  int archivedel;
  int updatestatus;
  int storeold;
  int onoff;
  int zip;
  int check;
  int mt;
  time_t lbt;
  struct autobackupdb_node_t *next;
} autobackupdb_node;

typedef struct unicasm_node_t
{
  char *bname;
  short cpumonitor;
  short busymonitor;
  short logcpu;
  short logbusy;
  short cpurestart;
  short busyrestart;
  short cpulimit;
  int busylimit;
  time_t lrt;
  struct unicasm_node_t *next;
} unicasm_node;

/* This structure is written by bctak */
/* This struct is for auto addvolume */
typedef struct autoaddvoldb_t
{
  char dbname[64];
  int data_vol;
  double data_warn_outofspace;
  int data_ext_page;
  int index_vol;
  double index_warn_outofspace;
  int index_ext_page;
  struct autoaddvoldb_t *next;
} autoaddvoldb_node;

typedef enum
{
  AEQT_ONE,
  AEQT_DAY,
  AEQT_WEEK,
  AEQT_MONTH
} T_EXECQUERY_PERIOD_TYPE;

typedef struct autoexecquery_t
{
  char dbname[64];
  char dbmt_uid[64];
  int query_id;
  T_EXECQUERY_PERIOD_TYPE period;
  char detail1[32];
  char detail2[16];
  char query_string[512];
  int db_mode;
  struct autoexecquery_t *next;
} autoexecquery_node;

#ifdef HOST_MONITOR_PROC
/* This struct is for auto history logging */
typedef struct autohistory_t
{
  time_t start_time;
  time_t end_time;
  float memory_limit;
  float cpu_limit;
  char **dbname;
  int dbcount;
  FILE *hfile;
  void *mondata;
} autohistory_node;
#endif

typedef enum
{
  ABPT_MONTHLY,
  ABPT_WEEKLY,
  ABPT_DAILY,
  ABPT_HOURLY,
  ABPT_SPECIAL
} T_AUTOBACKUP_PERIOD_TYPE;

static void aj_load_execquery_conf (ajob * p_aj);
static void aj_execquery_handler (void *hd, time_t prev_check_time,
				  time_t cur_time);
static void aj_execquery_get_exec_time (autoexecquery_node * node,
					struct tm *exec_tm);

static void aj_execquery (autoexecquery_node * c);
static void _aj_autoexecquery_error_log (autoexecquery_node * node,
					 char *errmsg);
static void aj_load_autobackupdb_conf (ajob * ajp);
static void aj_load_autoaddvoldb_config (ajob * ajp);
static void aj_load_unicasm_conf (ajob * ajp);
static void aj_load_autohistory_conf (ajob * ajp);

static void aj_autobackupdb_handler (void *ajp, time_t prev_check_time,
				     time_t cur_time);
static void aj_autoaddvoldb_handler (void *hd, time_t prev_check_time,
				     time_t cur_time);
static void aj_unicasm_handler (void *ajp, time_t prev_check_time,
				time_t cur_time);
static void aj_autohistory_handler (void *ajp, time_t prev_check_time,
				    time_t cur_time);

static void aj_backupdb (autobackupdb_node * n);
static void _aj_autobackupdb_error_log (autobackupdb_node * n, char *errmsg);
static int aj_restart_broker_as (char *bname, int id);
static double ajFreeSpace (T_SPACEDB_RESULT * cmd_res, char *type);
static void aj_add_volume (char *dbname, char *type, int increase);
static void auto_unicas_log_write (char *br_name, T_DM_UC_AS_INFO * as_info,
				   int restart_flag, char cpu_flag,
				   time_t busy_time);

void
aj_initialize (ajob * ajlist, void *ud)
{
  struct stat statbuf;

  sprintf (ajlist[0].name, "autoaddvoldb");
  conf_get_dbmt_file (FID_AUTO_ADDVOLDB_CONF, ajlist[0].config_file);
  stat (ajlist[0].config_file, &statbuf);
  ajlist[0].last_modi = statbuf.st_mtime;
  ajlist[0].is_on = 0;		/* initially off */
  ajlist[0].ajob_handler = aj_autoaddvoldb_handler;
  ajlist[0].ajob_loader = aj_load_autoaddvoldb_config;
  ajlist[0].hd = NULL;
  ajlist[0].mondata = ud;

  sprintf (ajlist[1].name, "autohistory");
  conf_get_dbmt_file (FID_AUTO_HISTORY_CONF, ajlist[1].config_file);
  stat (ajlist[1].config_file, &statbuf);
  ajlist[1].last_modi = statbuf.st_mtime;
  ajlist[1].is_on = 0;
  ajlist[1].ajob_handler = aj_autohistory_handler;
  ajlist[1].ajob_loader = aj_load_autohistory_conf;
  ajlist[1].hd = NULL;
  ajlist[1].mondata = ud;

  sprintf (ajlist[2].name, "autobackupdb");
  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, ajlist[2].config_file);
  stat (ajlist[2].config_file, &statbuf);
  ajlist[2].last_modi = statbuf.st_mtime;
  ajlist[2].is_on = 0;
  ajlist[2].ajob_handler = aj_autobackupdb_handler;
  ajlist[2].ajob_loader = aj_load_autobackupdb_conf;
  ajlist[2].hd = NULL;
  ajlist[2].mondata = ud;

  sprintf (ajlist[3].name, "autounicasm");
  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, ajlist[3].config_file);
  stat (ajlist[3].config_file, &statbuf);
  ajlist[3].last_modi = statbuf.st_mtime;
  ajlist[3].is_on = 0;
  ajlist[3].ajob_handler = aj_unicasm_handler;
  ajlist[3].ajob_loader = aj_load_unicasm_conf;
  ajlist[3].hd = NULL;
  ajlist[3].mondata = ud;

  sprintf (ajlist[4].name, "autoexecquery");
  conf_get_dbmt_file (FID_AUTO_EXECQUERY_CONF, ajlist[4].config_file);
  stat (ajlist[4].config_file, &statbuf);
  ajlist[4].last_modi = statbuf.st_mtime;
  ajlist[4].is_on = 0;
  ajlist[4].ajob_handler = aj_execquery_handler;
  ajlist[4].ajob_loader = aj_load_execquery_conf;
  ajlist[4].hd = NULL;
  ajlist[4].mondata = ud;
}

/* This function is written by bctak */
/* This function calculates the free space fraction of given type */
static double
ajFreeSpace (T_SPACEDB_RESULT * cmd_res, char *type)
{
  double total_page, free_page;
  int i;

  total_page = free_page = 0.0;

  for (i = 0; i < cmd_res->num_vol; i++)
    {
      if (strcmp (cmd_res->vol_info[i].purpose, type) == 0 ||
	  strcmp (cmd_res->vol_info[i].purpose, "GENERIC") == 0)
	{
	  total_page += cmd_res->vol_info[i].total_page;
	  free_page += cmd_res->vol_info[i].free_page;
	}
    }

  if (total_page > 0.0)
    {
      return (free_page / total_page);
    }

  return 1.0;
}

/* This function is written by bctak */
/* This function adds volume and write to file for fserver */
static void
aj_add_volume (char *dbname, char *type, int increase)
{
  char dbloca[512];
  char strbuf[1024];
  char volname[512] = { '\0' };
  FILE *outfile;
  time_t mytime;
  int retval;
  struct stat statbuf;
  char log_file_name[512];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char inc_str[32];
  char *argv[10];
  T_SPACEDB_RESULT *all_volumes;
  T_SPACEDB_INFO *vol_info;
  int i;

  if (uRetrieveDBDirectory (dbname, dbloca) != ERR_NO_ERROR)
    return;

  if (access (dbloca, W_OK | X_OK | R_OK) < 0)
    return;

#ifdef WIN32
  nt_style_path (dbloca, dbloca);
#endif
  cubrid_cmd_name (cmd_name);
  argv[0] = cmd_name;
  argv[1] = UTIL_OPTION_ADDVOLDB;
  argv[2] = "--" ADDVOL_FILE_PATH_L;
  argv[3] = dbloca;
  argv[4] = "--" ADDVOL_PURPOSE_L;
  argv[5] = type;
  argv[6] = dbname;
  sprintf (inc_str, "%d", increase);
  argv[7] = inc_str;
  argv[8] = NULL;
  retval = run_child (argv, 1, NULL, NULL, NULL, NULL);	/* addvoldb  */

  mytime = 0;

  all_volumes = cmd_spacedb (dbname, CUBRID_MODE_CS);
  if (all_volumes == NULL)
    return;
  else
    {
      vol_info = all_volumes->vol_info;

      for (i = 0; i < all_volumes->num_vol; i++)
	{
	  if (uStringEqual (vol_info[i].purpose, "DATA")
	      || uStringEqual (vol_info[i].purpose, "INDEX"))
	    {
	      strcpy (volname, vol_info[i].vol_name);
	      sprintf (strbuf, "%s/%s", dbloca, volname);
	      if (!stat (strbuf, &statbuf))
		mytime = statbuf.st_mtime;
	    }
	}
    }
  cmd_spacedb_result_free (all_volumes);

  mytime = time (&mytime);
  conf_get_dbmt_file (FID_AUTO_ADDVOLDB_LOG, log_file_name);
  if ((outfile = fopen (log_file_name, "a")) != NULL)
    {
      fprintf (outfile, "%s ", dbname);
      if (retval == 0)
	fprintf (outfile, "%s ", volname);
      else
	fprintf (outfile, "none ");
      fprintf (outfile, "%s ", type);
      fprintf (outfile, "%d ", increase);
      time_to_str (mytime, "%d-%d-%d,%d:%d:%d", strbuf,
		   TIME_STR_FMT_DATE_TIME);
      fprintf (outfile, "%s ", strbuf);
      if (retval == 0)
	fprintf (outfile, "success\n");
      else
	fprintf (outfile, "failure\n");
      fclose (outfile);
    }
}

static void
aj_autohistory_handler (void *ajp, time_t prev_check_time, time_t cur_time)
{
#ifdef HOST_MONITOR_PROC
  time_t mytime, current_time;
  float current_cpu, current_mem;
  char strbuf[1024];
  autohistory_node *hsp;
  userdata *mondata;
  char timestr[64];

  hsp = (autohistory_node *) ajp;
  mondata = (userdata *) (hsp->mondata);

  current_time = time (&current_time);
  if ((current_time < hsp->start_time) || (current_time > hsp->end_time))
    {
      if (hsp->hfile != NULL)
	fclose (hsp->hfile);
      hsp->hfile = NULL;
      return;
    }

  /* auto histoy feature */
  current_cpu = (float) (1000 - mondata->ssbuf.cpu_states[0]);
  current_mem =
    (float) (mondata->ssbuf.memory_stats[1]) /
    (float) (mondata->ssbuf.memory_stats[0]) * 100.0;

  if ((current_cpu > hsp->cpu_limit) || (current_mem > hsp->memory_limit))
    {
      mytime = time (&mytime);

      if (hsp->hfile == NULL)
	{
	  time_to_str (mytime, "%04d%02d%02d.%02d%02d%02d", timestr,
		       TIME_STR_FMT_DATE_TIME);
	  sprintf (strbuf, "%s/logs/_dbmt_history.%s", sco.szCubrid, timestr);
	  hsp->hfile = fopen (strbuf, "w");
	}
      /* recrod system information */
      if (hsp->hfile != NULL)
	{
	  time_to_str (mytime, "[%04d/%02d/%02d-%02d:%02d:%02d]", timestr,
		       TIME_STR_FMT_DATE_TIME);
	  fprintf (hsp->hfile, "%s", timestr);
	  fprintf (hsp->hfile, "load average 1min:%d 5min:%d 15min:%d\n",
		   mondata->ssbuf.load_avg[0],
		   mondata->ssbuf.load_avg[1], mondata->ssbuf.load_avg[2]);
	  fprintf (hsp->hfile,
		   "cpu time idle:%d user:%d kernel%d iowait:%d swap:%d\n",
		   mondata->ssbuf.cpu_states[0],
		   mondata->ssbuf.cpu_states[1],
		   mondata->ssbuf.cpu_states[2],
		   mondata->ssbuf.cpu_states[3],
		   mondata->ssbuf.cpu_states[4]);
	  fprintf (hsp->hfile,
		   "memory real:%dK active:%dK free:%dK swap:%dK swapfree:%dK\n",
		   mondata->ssbuf.memory_stats[0],
		   mondata->ssbuf.memory_stats[1],
		   mondata->ssbuf.memory_stats[2],
		   mondata->ssbuf.memory_stats[3],
		   mondata->ssbuf.memory_stats[4]);
	  fflush (hsp->hfile);
	}
      /* record db information */

      if (hsp->hfile != NULL)
	{
	  FILE *infile;
	  int i;
	  infile =
	    fopen (conf_get_dbmt_file (FID_AUTO_HISTORY_CONF, strbuf), "r");
	  if (infile != NULL)
	    {
	      while (fgets (strbuf, sizeof (strbuf), infile))
		{
		  ut_trim (strbuf);
		  for (i = 0; i < MAX_INSTALLED_DB; ++i)
		    {
		      if ((mondata->dbvect[i] == 1) &&
			  (uStringEqual (strbuf, mondata->dbbuf[i].db_name)))
			{
			  fprintf (hsp->hfile, "database name:%s ",
				   mondata->dbbuf[i].db_name);
			  fprintf (hsp->hfile, "pid:%d ",
				   mondata->dbbuf[i].db_pid);
			  fprintf (hsp->hfile, "size:%ld ",
				   mondata->dbbuf[i].db_size);
			  fprintf (hsp->hfile, "status:%c ",
				   mondata->dbbuf[i].proc_stat[0]);
			  mytime = mondata->dbbuf[i].db_start_time;
			  time_to_str (mytime,
				       "%04d/%02d/%02d-%02d:%02d:%02d",
				       timestr, TIME_STR_FMT_DATE_TIME);
			  fprintf (hsp->hfile, "start_time:%s ", timestr);
			  fprintf (hsp->hfile, "cpu_usage:%f%% ",
				   mondata->dbbuf[i].db_cpu_usage);
			  fprintf (hsp->hfile, "mem_usage:%f%%\n\n",
				   mondata->dbbuf[i].db_mem_usage);
			  fflush (hsp->hfile);
			}
		    }
		}
	      fclose (infile);
	    }
	}
    }
#endif
}

/* This function is written by bctak */
static void
aj_autoaddvoldb_handler (void *hd, time_t prev_check_time, time_t cur_time)
{
  autoaddvoldb_node *curr;
  double frate;
  int page_add;
  T_SPACEDB_RESULT *spacedb_res;
  T_COMMDB_RESULT *commdb_res;
  int max_page;

  commdb_res = cmd_commdb ();
  if (commdb_res == NULL)
    return;

  for (curr = (autoaddvoldb_node *) hd; curr != NULL; curr = curr->next)
    {
      if (curr->dbname == NULL)
	continue;

      if (!uIsDatabaseActive2 (commdb_res, curr->dbname))
	{
	  continue;
	}

      spacedb_res = cmd_spacedb (curr->dbname, CUBRID_MODE_CS);
      if (spacedb_res == NULL)
	continue;

      if (spacedb_res->page_size > 0)
	max_page = (1 << 30) / spacedb_res->page_size;
      else
	max_page = 200000;

      page_add = curr->data_ext_page;
      if ((curr->data_vol) && (page_add > 0))
	{
	  frate = ajFreeSpace (spacedb_res, "DATA");
	  if (page_add > max_page)
	    page_add = max_page;
	  if (page_add < MIN_AUTO_ADDVOL_PAGE_SIZE)
	    page_add = MIN_AUTO_ADDVOL_PAGE_SIZE;
	  if (curr->data_warn_outofspace >= frate)
	    aj_add_volume (curr->dbname, "data", page_add);
	}

      page_add = curr->index_ext_page;
      if ((curr->index_vol) && (page_add > 0))
	{
	  frate = ajFreeSpace (spacedb_res, "INDEX");
	  if (page_add > max_page)
	    page_add = max_page;
	  if (page_add < MIN_AUTO_ADDVOL_PAGE_SIZE)
	    page_add = MIN_AUTO_ADDVOL_PAGE_SIZE;
	  if (curr->index_warn_outofspace >= frate)
	    aj_add_volume (curr->dbname, "index", page_add);
	}

      cmd_spacedb_result_free (spacedb_res);
    }
  cmd_commdb_result_free (commdb_res);
}

/* This function is written by bctak */
static void
aj_load_autoaddvoldb_config (ajob * ajp)
{
  FILE *infile = NULL;
  char strbuf[1024];
  char *conf_item[AUTOADDVOL_CONF_ENTRY_NUM];
  autoaddvoldb_node *next, *curr;

  /* turn off autoaddvoldb feature */
  ajp->is_on = 0;

  /* free existing structure */
  curr = (autoaddvoldb_node *) (ajp->hd);
  while (curr)
    {				/* clear the linked list */
      next = curr->next;
      FREE_MEM (curr);
      curr = next;
    }
  ajp->hd = curr = NULL;

  infile = fopen (ajp->config_file, "r");
  if (infile == NULL)
    return;

  while (fgets (strbuf, 1024, infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#' || strbuf[0] == '\0')
	continue;

      if (string_tokenize (strbuf, conf_item, AUTOADDVOL_CONF_ENTRY_NUM) < 0)
	continue;

      if (curr == NULL)
	{
	  curr = (autoaddvoldb_node *) malloc (sizeof (autoaddvoldb_node));
	  ajp->hd = curr;
	}
      else
	{
	  curr->next =
	    (autoaddvoldb_node *) malloc (sizeof (autoaddvoldb_node));
	  curr = curr->next;
	}
      if (curr == NULL)
	break;

      memset (curr, 0, sizeof (autoaddvoldb_node));
      strcpy (curr->dbname, conf_item[0]);

      if (strcmp (conf_item[1], "ON") == 0)
	{
	  curr->data_vol = 1;
	  ajp->is_on = 1;
	}

      curr->data_warn_outofspace = atof (conf_item[2]);
      if (curr->data_warn_outofspace > MAX_AUTOADD_FREE_SPACE_RATE)
	curr->data_warn_outofspace = MAX_AUTOADD_FREE_SPACE_RATE;

      curr->data_ext_page = atoi (conf_item[3]);

      if (strcmp (conf_item[4], "ON") == 0)
	{
	  curr->index_vol = 1;
	  ajp->is_on = 1;
	}

      curr->index_warn_outofspace = atof (conf_item[5]);
      if (curr->index_warn_outofspace > MAX_AUTOADD_FREE_SPACE_RATE)
	curr->index_warn_outofspace = MAX_AUTOADD_FREE_SPACE_RATE;

      curr->index_ext_page = atoi (conf_item[6]);
      curr->next = NULL;
    }
  fclose (infile);
}

/* parameter ud : autojob structure for autohistory */
/* parameter ud2 : data from collect_start() */
static void
aj_load_autohistory_conf (ajob * ajp)
{
#ifdef HOST_MONITOR_PROC
  int i;
  FILE *infile;
  struct tm timeptr;
  autohistory_node *ahist;
  char strbuf[1024];
  char *conf_item[AUTOHISTORY_CONF_ENTRY_NUM];

  ajp->is_on = 0;

  ahist = (autohistory_node *) (ajp->hd);
  /* free existing structure */
  if (ahist)
    {
      for (i = 0; i < ahist->dbcount; ++i)
	FREE_MEM (ahist->dbname[i]);
      FREE_MEM (ahist->dbname);
      FREE_MEM (ahist);
    }

  /* create new struct and initialize */
  ajp->hd = (void *) malloc (sizeof (autohistory_node));
  ahist = (autohistory_node *) (ajp->hd);
  ahist->memory_limit = 100.0;
  ahist->cpu_limit = 100.0;
  ahist->dbname = NULL;
  ahist->dbcount = 0;
  ahist->hfile = NULL;
  ahist->mondata = ajp->mondata;

  if ((infile = fopen (ajp->config_file, "r")) == NULL)
    return;

  memset (strbuf, 0, sizeof (strbuf));
  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#' || strbuf[0] == '\0')
	{
	  memset (strbuf, 0, sizeof (strbuf));
	  continue;
	}
      break;
    }
  if (string_tokenize (strbuf, conf_item, AUTOHISTORY_CONF_ENTRY_NUM) < 0)
    {
      fclose (infile);
      return;
    }

  if (strcmp (conf_item[0], "ON") == 0)
    ajp->is_on = 1;
  else
    ajp->is_on = 0;

  timeptr.tm_year = atoi (conf_item[1]) - 1900;
  timeptr.tm_mon = atoi (conf_item[2]) - 1;
  timeptr.tm_mday = atoi (conf_item[3]);
  timeptr.tm_hour = atoi (conf_item[4]);
  timeptr.tm_min = atoi (conf_item[5]);
  timeptr.tm_sec = atoi (conf_item[6]);
  ahist->start_time = mktime (&timeptr);

  timeptr.tm_year = atoi (conf_item[7]) - 1900;
  timeptr.tm_mon = atoi (conf_item[8]) - 1;
  timeptr.tm_mday = atoi (conf_item[9]);
  timeptr.tm_hour = atoi (conf_item[10]);
  timeptr.tm_min = atoi (conf_item[11]);
  timeptr.tm_sec = atoi (conf_item[12]);
  ahist->end_time = mktime (&timeptr);

  ahist->memory_limit = atof (conf_item[13]);
  ahist->cpu_limit = atof (conf_item[14]);

  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      ut_trim (strbuf);
      ahist->dbcount++;
      ahist->dbname =
	REALLOC (ahist->dbname, sizeof (char *) * (ahist->dbcount));
      if (ahist->dbname == NULL)
	break;
      ahist->dbname[ahist->dbcount - 1] = strdup (strbuf);
    }

  fclose (infile);
#endif
}

static void
aj_load_autobackupdb_conf (ajob * p_aj)
{
  char buf[1024];
  FILE *infile = NULL;
  autobackupdb_node *c;
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int is_old_version_entry;

  p_aj->is_on = 0;

  c = (autobackupdb_node *) (p_aj->hd);
  while (c != NULL)
    {
      autobackupdb_node *t;

      t = c;
      FREE_MEM (t->dbname);
      FREE_MEM (t->backup_id);
      FREE_MEM (t->path);
      c = c->next;
      FREE_MEM (t);
    }
  p_aj->hd = c = NULL;

  if ((infile = fopen (p_aj->config_file, "r")) == NULL)
    return;

  while (fgets (buf, 1024, infile))
    {
      is_old_version_entry = 0;
      ut_trim (buf);
      if (buf[0] == '#' || buf[0] == '\0')
	continue;

      if (string_tokenize (buf, conf_item, AUTOBACKUP_CONF_ENTRY_NUM) < 0)
	{
	  if (string_tokenize (buf, conf_item, AUTOBACKUP_CONF_ENTRY_NUM - 3)
	      < 0)
	    continue;
	  else
	    is_old_version_entry = 1;
	}

      if (c == NULL)
	{
	  c = (autobackupdb_node *) malloc (sizeof (autobackupdb_node));
	  p_aj->hd = c;
	}
      else
	{
	  c->next = (autobackupdb_node *) malloc (sizeof (autobackupdb_node));
	  c = c->next;
	}
      if (c == NULL)
	break;

      c->lbt = -1;
      c->dbname = strdup (conf_item[0]);
      c->backup_id = strdup (conf_item[1]);
      c->path = strdup (conf_item[2]);

      if (!strcmp (conf_item[3], "Monthly"))
	{
	  c->period_type = ABPT_MONTHLY;
	  c->period_date = atoi (conf_item[4]);
	}
      else if (!strcmp (conf_item[3], "Weekly"))
	{
	  c->period_type = ABPT_WEEKLY;
	  if (!strcmp (conf_item[4], "Sunday"))
	    c->period_date = 0;
	  else if (!strcmp (conf_item[4], "Monday"))
	    c->period_date = 1;
	  else if (!strcmp (conf_item[4], "Tuesday"))
	    c->period_date = 2;
	  else if (!strcmp (conf_item[4], "Wednesday"))
	    c->period_date = 3;
	  else if (!strcmp (conf_item[4], "Thursday"))
	    c->period_date = 4;
	  else if (!strcmp (conf_item[4], "Friday"))
	    c->period_date = 5;
	  else if (!strcmp (conf_item[4], "Saturday"))
	    c->period_date = 6;
	  else
	    {
	      FREE_MEM (c);
	      continue;
	    }
	}
      else if (!strcmp (conf_item[3], "Daily"))
	{
	  c->period_type = ABPT_DAILY;
	  c->period_date = -1;
	}
      else if (!strcmp (conf_item[3], "Hourly"))
	{
	  c->period_type = ABPT_HOURLY;
	  c->period_date = atoi (conf_item[4]);
	}
      else if (!strcmp (conf_item[3], "Special"))
	{
	  c->period_type = ABPT_SPECIAL;
	  c->period_date = atoi (conf_item[4]) * 10000;
	  c->period_date += atoi (conf_item[4] + 5) * 100;
	  c->period_date += atoi (conf_item[4] + 8);
	}
      else
	{
	  FREE_MEM (c);
	  continue;
	}
      c->time = atoi (conf_item[5]);
      c->level = atoi (conf_item[6]);
      c->archivedel = !strcmp (conf_item[7], "ON") ? 1 : 0;
      c->updatestatus = !strcmp (conf_item[8], "ON") ? 1 : 0;
      c->storeold = !strcmp (conf_item[9], "ON") ? 1 : 0;
      c->onoff = !strcmp (conf_item[10], "ON") ? 1 : 0;

      if (is_old_version_entry)
	{
	  c->zip = 0;
	  c->check = 0;
	  c->mt = 0;
	}
      else
	{
	  c->zip = !strcmp (conf_item[11], "y") ? 1 : 0;
	  c->check = !strcmp (conf_item[12], "y") ? 1 : 0;
	  c->mt = atoi (conf_item[13]);
	}
      c->next = NULL;
    }				/* end of while */
  fclose (infile);
  p_aj->is_on = 1;
}

static void
aj_load_execquery_conf (ajob * p_aj)
{
  FILE *infile = NULL;
  char *conf_item[AUTOEXECQUERY_CONF_ENTRY_NUM];
  char buf[1024];
  autoexecquery_node *c;

  p_aj->is_on = 0;

  /* NODE reset */
  c = (autoexecquery_node *) (p_aj->hd);
  while (c != NULL)
    {
      autoexecquery_node *t;
      t = c;
      c = c->next;
      FREE_MEM (t);
    }

  p_aj->hd = c = NULL;

  /* read NODE information from file */
  if ((infile = fopen (p_aj->config_file, "r")) == NULL)
    return;

  while (fgets (buf, 1024, infile))
    {
      ut_trim (buf);
      if (buf[0] == '#' || buf[0] == '\0')
	continue;

      if (string_tokenize_accept_laststring_space
	  (buf, conf_item, AUTOEXECQUERY_CONF_ENTRY_NUM) < 0)
	continue;

      if (c == NULL)
	{
	  c = (autoexecquery_node *) malloc (sizeof (autoexecquery_node));
	  p_aj->hd = c;
	}
      else
	{
	  c->next =
	    (autoexecquery_node *) malloc (sizeof (autoexecquery_node));
	  c = c->next;
	}
      if (c == NULL)
	break;

      strcpy (c->dbname, conf_item[0]);
      c->query_id = atoi (conf_item[1]);
      strcpy (c->dbmt_uid, conf_item[2]);

      if (strcmp (conf_item[3], "ONE") == 0)
	c->period = AEQT_ONE;
      else if (strcmp (conf_item[3], "DAY") == 0)
	c->period = AEQT_DAY;
      else if (strcmp (conf_item[3], "WEEK") == 0)
	c->period = AEQT_WEEK;
      else if (strcmp (conf_item[3], "MONTH") == 0)
	c->period = AEQT_MONTH;

      strcpy (c->detail1, conf_item[4]);
      strcpy (c->detail2, conf_item[5]);
      strcpy (c->query_string, conf_item[6]);
      c->db_mode = 2;
      c->next = NULL;
    }				/* end of while */
  fclose (infile);
  p_aj->is_on = 1;

}

static void
aj_execquery_handler (void *hd, time_t prev_check_time, time_t cur_time)
{
  time_t execquery_time;
  struct tm exec_tm, cur_tm;
  autoexecquery_node *c;

  cur_tm = *localtime (&cur_time);

  for (c = (autoexecquery_node *) (hd); c != NULL; c = c->next)
    {
      exec_tm = cur_tm;

      aj_execquery_get_exec_time (c, &exec_tm);

      execquery_time = mktime (&exec_tm);

      if (execquery_time <= prev_check_time || execquery_time > cur_time)
	continue;

      if ((c->period == AEQT_ONE) ||
	  (c->period == AEQT_DAY) ||
	  ((c->period == AEQT_WEEK) && cur_tm.tm_wday == exec_tm.tm_wday) ||
	  ((c->period == AEQT_MONTH) && cur_tm.tm_mday == exec_tm.tm_mday))
	{
	  aj_execquery (c);
	}
    }

  return;
}

static void
aj_execquery_get_exec_time (autoexecquery_node * c, struct tm *exec_tm)
{
  switch (c->period)
    {
    case AEQT_ONE:
      sscanf (c->detail1, "%d/%d/%d", &(exec_tm->tm_year), &(exec_tm->tm_mon),
	      &(exec_tm->tm_mday));
      exec_tm->tm_year -= 1900;	/* year : since 1900 */
      exec_tm->tm_mon -= 1;	/* month : zero based month */

      break;

    case AEQT_DAY:
      /* sscanf(c->detail2, "%d:%d", &(exec_tm->tm_hour), &(exec_tm->tm_min)); */
      break;

    case AEQT_WEEK:
      if (strcmp (c->detail1, "SUN") == 0)
	exec_tm->tm_wday = 0;
      else if (strcmp (c->detail1, "MON") == 0)
	exec_tm->tm_wday = 1;
      else if (strcmp (c->detail1, "TUE") == 0)
	exec_tm->tm_wday = 2;
      else if (strcmp (c->detail1, "WED") == 0)
	exec_tm->tm_wday = 3;
      else if (strcmp (c->detail1, "THU") == 0)
	exec_tm->tm_wday = 4;
      else if (strcmp (c->detail1, "FRI") == 0)
	exec_tm->tm_wday = 5;
      else if (strcmp (c->detail1, "SAT") == 0)
	exec_tm->tm_wday = 6;
      break;
    case AEQT_MONTH:
      sscanf (c->detail1, "%d", &(exec_tm->tm_mday));
    }

  sscanf (c->detail2, "%d:%d", &(exec_tm->tm_hour), &(exec_tm->tm_min));
  exec_tm->tm_sec = 0;
}


static void
aj_execquery (autoexecquery_node * c)
{
  /* run query */
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char error_buffer[1024];
  char *dbuser, dbpasswd[PASSWD_LENGTH];
  char *argv[11];
  char input_filename[200];
  char *cubrid_err_file;
  int retval, argc, i, j;
  FILE *input_file;

  T_DB_SERVICE_MODE db_mode;
  T_DBMT_USER dbmt_user;

  memset (error_buffer, '\0', sizeof (error_buffer));
  /* must be freed with dbmt_user_free */

  sprintf (input_filename, "%s/dbmt_auto_execquery_%d", sco.dbmt_tmp_dir,
	   (int) getpid ());

  /* dbuser = get user name */
  /* dbpasswd = get password */
  if (dbmt_user_read (&dbmt_user, error_buffer) != ERR_NO_ERROR)
    {
      /* can't get user information */
#ifdef	_DEBUG_
      assert (error_buffer != NULL);
#endif
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (c->dbmt_uid, dbmt_user.user_info[i].user_name) == 0)
	{
	  /* User whom i seeks */
	  for (j = 0; j < dbmt_user.user_info[i].num_dbinfo; j++)
	    {
	      if (strcmp (c->dbname, dbmt_user.user_info[i].dbinfo[j].dbname)
		  == 0)
		{
		  /* Database where i seeks */
		  char *temp;
		  memset (dbpasswd, '\0', sizeof (dbpasswd));

		  dbuser = dbmt_user.user_info[i].dbinfo[j].uid;
		  temp = dbmt_user.user_info[i].dbinfo[j].passwd;
		  uDecrypt (PASSWD_LENGTH, temp, dbpasswd);
		  break;
		}
	    }
	  if (j == dbmt_user.user_info[i].num_dbinfo)
	    {
	      /* Can't find dbname */
	      sprintf (error_buffer,
		       "Database(%s) not found or User(%s) has no outhority for this Database",
		       c->dbname, c->dbmt_uid);
	      _aj_autoexecquery_error_log (c, error_buffer);
	      return;
	    }

	  break;
	}
    }

  if (i == dbmt_user.num_dbmt_user)
    {
      /* Can't find easy-manager user */
      sprintf (error_buffer, "User(%s) not found or has no authority",
	       c->dbmt_uid);
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CSQL_NAME);
  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = c->dbname;
  argv[argc++] = NULL;
  argv[argc++] = "--" CSQL_INPUT_FILE_L;
  argv[argc++] = input_filename;
  if (dbuser)
    {
      argv[argc++] = "--" CSQL_USER_L;
      argv[argc++] = dbuser;
      if (*dbpasswd != '\0')
	{
	  argv[argc++] = "--" CSQL_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }
  argv[argc++] = "--" CSQL_NO_AUTO_COMMIT_L;

  for (; argc < 11; argc++)
    argv[argc] = NULL;

  if (!_isRegisteredDB (c->dbname))
    {
      /* database is not exist */
      sprintf (error_buffer, "Database(%s) not found", c->dbname);
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  db_mode = uDatabaseMode (c->dbname);

  if (db_mode == DB_SERVICE_MODE_SA)
    {
      /* database is running in stand alone mode */
      sprintf (error_buffer, "Database(%s) is running in stand alone mode",
	       c->dbname);
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  if (db_mode == DB_SERVICE_MODE_CS)
    {
      /* run sqlx command with -cs option */
      argv[2] = "--" CSQL_CS_MODE_L;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      /* run sqlx command with -sa option */
      argv[2] = "--" CSQL_SA_MODE_L;
    }

  /* sqlx -sa|-cs -i input_file dbname  */
  input_file = fopen (input_filename, "w+");
  if (input_file)
    {
      fprintf (input_file, "%s\n", c->query_string);
      fprintf (input_file, ";commit");
      fclose (input_file);
    }
  else
    {
      /* file open error */
      sprintf (error_buffer, "Can't create temp file");
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* sqlx auto-execute */
  unlink (input_filename);

  /* free dbmt_user */
  dbmt_user_free (&dbmt_user);

  if (read_error_file (cubrid_err_file, error_buffer, DBMT_ERROR_MSG_SIZE) <
      0)
    {
      /* error occurred when exec query */
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }

  if (retval < 0)
    {
      /* error occerred when run_child */
      sprintf (error_buffer, "Failed to execute Query with");
      _aj_autoexecquery_error_log (c, error_buffer);
      return;
    }
}

static void
_aj_autoexecquery_error_log (autoexecquery_node * node, char *errmsg)
{
  /* open error file and write errmsg */
  time_t tt;
  FILE *outfile;
  char logfile[256];
  char strbuf[128];

  tt = time (&tt);
  sprintf (logfile, "%s/logs/autoexecquery_error.log", sco.szCubrid);

  outfile = fopen (logfile, "a");
  if (outfile == NULL)
    return;

  time_to_str (tt, "DATE:%04d/%02d/%02d TIME:%02d:%02d:%02d", strbuf,
	       TIME_STR_FMT_DATE_TIME);
  fprintf (outfile, "%s\n", strbuf);
  fprintf (outfile, "DBNAME:%s EMGR-USERNAME:%s\n", node->dbname,
	   node->dbmt_uid);
  fprintf (outfile, "=> %s\n", errmsg);
  fclose (outfile);
}

static void
aj_autobackupdb_handler (void *hd, time_t prev_check_time, time_t cur_time)
{
  time_t backup_time;
  struct tm backup_tm, cur_tm;
  autobackupdb_node *c;

  cur_tm = *localtime (&cur_time);

  for (c = (autobackupdb_node *) (hd); c != NULL; c = c->next)
    {
      backup_tm = cur_tm;
      if (c->period_type == ABPT_SPECIAL)
	{
	  backup_tm.tm_year = c->period_date / 10000 - 1900;
	  backup_tm.tm_mon = (c->period_date % 10000) / 100 - 1;
	  backup_tm.tm_mday = c->period_date % 100;
	}
      if (c->period_type != ABPT_HOURLY)
	{
	  backup_tm.tm_hour = c->time / 100;
	}
      backup_tm.tm_min = c->time % 100;
      backup_tm.tm_sec = 0;

      backup_time = mktime (&backup_tm);
      if (backup_time <= prev_check_time || backup_time > cur_time)
	continue;

      if ((c->period_type == ABPT_MONTHLY && cur_tm.tm_mday == c->period_date)
	  || (c->period_type == ABPT_WEEKLY
	      && cur_tm.tm_wday == c->period_date)
	  || (c->period_type == ABPT_DAILY) || (c->period_type == ABPT_HOURLY)
	  || (c->period_type == ABPT_SPECIAL))
	{
	  aj_backupdb (c);
	}
    }
}

static void
_aj_autobackupdb_error_log (autobackupdb_node * n, char *errmsg)
{
  time_t tt;
  FILE *outfile;
  char logfile[256];
  char strbuf[128];

  tt = time (&tt);
  sprintf (logfile, "%s/logs/autobackupdb_error.log", sco.szCubrid);

  outfile = fopen (logfile, "a");
  if (outfile == NULL)
    return;
  time_to_str (tt, "DATE:%04d/%02d/%02d TIME:%02d:%02d:%02d", strbuf,
	       TIME_STR_FMT_DATE_TIME);
  fprintf (outfile, "%s\n", strbuf);
  fprintf (outfile, "DBNAME:%s BACKUPID:%s\n", n->dbname, n->backup_id);
  fprintf (outfile, "=> %s\n", errmsg);
  fclose (outfile);
}

static void
aj_backupdb (autobackupdb_node * n)
{
  char filepath[512];
  char inputfilepath[512];
  char buf[1024], dbdir[512];
  char backup_vol_name[128];
  char *opt_mode;
  char db_start_flag = 0;
  char *cubrid_err_file;
  int retval;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char level_str[32];
  char thread_num_str[8];
  char *argv[16];
  int argc = 0;
  FILE *inputfile;
  T_DB_SERVICE_MODE db_mode;

  n->lbt = time (NULL);
  if (uRetrieveDBDirectory (n->dbname, dbdir) != ERR_NO_ERROR)
    {
      sprintf (buf, "DB directory not found");
      _aj_autobackupdb_error_log (n, buf);
      return;
    }

  sprintf (backup_vol_name, "%s_auto_backup_lv%d", n->dbname, n->level);
  sprintf (filepath, "%s/%s", n->path, backup_vol_name);

  db_mode = uDatabaseMode (n->dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (buf, "Failed to execute backupdb: %s is in standalone mode",
	       n->dbname);
      _aj_autobackupdb_error_log (n, buf);
      return;
    }
  if (n->onoff == 1 && db_mode == DB_SERVICE_MODE_NONE)
    {
      sprintf (buf, "Failed to execute backupdb: %s is offline", n->dbname);
      _aj_autobackupdb_error_log (n, buf);
      return;
    }

  if (access (n->path, F_OK) < 0)
    {
      if (mkdir (n->path, 0755) < 0)
	{
	  sprintf (buf, "Directory creation failed: %s", n->path);
	  _aj_autobackupdb_error_log (n, buf);
	  return;
	}
    }

  if (n->storeold && access (filepath, F_OK) == 0)
    {
      char store_old_dir[512];
      sprintf (store_old_dir, "%s/storeold", n->path);
      if (access (store_old_dir, F_OK) < 0)
	{
	  if (mkdir (store_old_dir, 0755) < 0)
	    {
	      sprintf (buf, "Directory creation failed: %s", store_old_dir);
	      _aj_autobackupdb_error_log (n, buf);
	      return;
	    }
	}
      sprintf (buf, "%s/%s", store_old_dir, backup_vol_name);
      if (move_file (filepath, buf) < 0)
	{
	  strcpy (buf, "Failed to copy old backup file");
	  _aj_autobackupdb_error_log (n, buf);
	  return;
	}
    }

  /* if DB status is on then turn off */
  if (n->onoff == 0 && db_mode == DB_SERVICE_MODE_CS)
    {
      if (cmd_stop_server (n->dbname, NULL, 0) < 0)
	{
	  sprintf (buf, "Failed to turn off DB");
	  _aj_autobackupdb_error_log (n, buf);
	  return;
	}
      db_start_flag = 1;
    }

  opt_mode = (n->onoff == 0) ? "-sa" : "-cs";

  cubrid_cmd_name (cmd_name);
  sprintf (thread_num_str, "%d", n->mt);
  sprintf (level_str, "%d", n->level);
  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_BACKUPDB;
  argv[argc++] = opt_mode;
  argv[argc++] = "--" BACKUP_LEVEL_L;
  argv[argc++] = level_str;
  argv[argc++] = "--" BACKUP_DESTINATION_PATH_L;
  argv[argc++] = filepath;
  if (n->archivedel)
    argv[argc++] = "--" BACKUP_REMOVE_ARCHIVE_L;

  if (n->mt > 0)
    {
      argv[argc++] = "--" BACKUP_THREAD_COUNT_L;
      argv[argc++] = thread_num_str;
    }

  if (n->zip)
    argv[argc++] = "--" BACKUP_COMPRESS_L;

  if (!n->check)
    argv[argc++] = "--" BACKUP_NO_CHECK_L;

  argv[argc++] = n->dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  sprintf (inputfilepath, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_BACKUPDB,
	   (int) getpid ());
  inputfile = fopen (inputfilepath, "w");
  if (inputfile)
    {
      fprintf (inputfile, "y");
      fclose (inputfile);
    }
  else
    {
      sprintf (buf, "Failed to write file: %s", inputfilepath);
      _aj_autobackupdb_error_log (n, buf);
      return;
    }

  retval = run_child (argv, 1, inputfilepath, NULL, cubrid_err_file, NULL);	/* backupdb */
  unlink (inputfilepath);

  if (read_error_file (cubrid_err_file, buf, sizeof (buf)) < 0)
    {
      _aj_autobackupdb_error_log (n, buf);
      return;
    }
  if (retval < 0)
    {				/* backupdb */
      sprintf (buf, "Failed to execute backupdb: %s", argv[0]);
      _aj_autobackupdb_error_log (n, buf);
      return;
    }

  /* update statistics */
  if (n->onoff == 0 && n->updatestatus)
    {
      cubrid_cmd_name (cmd_name);
      argv[0] = cmd_name;
      argv[1] = UTIL_OPTION_OPTIMIZEDB;
      argv[2] = n->dbname;
      argv[3] = NULL;
      if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
	{			/* optimizedb */
	  sprintf (buf, "Failed to update statistics");
	  _aj_autobackupdb_error_log (n, buf);
	  return;
	}
    }

  if (db_start_flag)
    {
      char err_buf[ERR_MSG_SIZE];
      if (cmd_start_server (n->dbname, err_buf, sizeof (err_buf)) < 0)
	{
	  int buf_len;
	  memset (buf, 0, sizeof (buf));
	  sprintf (buf, "Failed to turn on DB : ");
	  buf_len = strlen (buf);
	  strncpy (buf + buf_len, err_buf, sizeof (buf) - buf_len - 1);
	  _aj_autobackupdb_error_log (n, buf);
	}
    }
}

static void
aj_load_unicasm_conf (ajob * p_aj)
{
  char buf[1024];
  FILE *infile;
  unicasm_node *c, *t;
  char *conf_item[AUTOUNICAS_CONF_ENTRY_NUM];

  p_aj->is_on = 0;

  /* established list clear */
  for (c = (unicasm_node *) (p_aj->hd); c != NULL;)
    {
      t = c;
      FREE_MEM (t->bname);
      c = c->next;
      FREE_MEM (t);
    }
  p_aj->hd = c = NULL;

  if ((infile = fopen (p_aj->config_file, "r")) == NULL)
    {
      return;
    }

  p_aj->is_on = 1;

  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (buf[0] == '#' || buf[0] == '\0')
	continue;
      if (string_tokenize (buf, conf_item, AUTOUNICAS_CONF_ENTRY_NUM) < 0)
	continue;

      if (c == NULL)
	{
	  p_aj->hd = (unicasm_node *) malloc (sizeof (unicasm_node));
	  c = (unicasm_node *) (p_aj->hd);
	}
      else
	{
	  c->next = (unicasm_node *) malloc (sizeof (unicasm_node));
	  c = c->next;
	}
      if (c == NULL)
	break;
      c->bname = strdup (conf_item[0]);
      c->cpumonitor = !strcmp (conf_item[1], "ON") ? 1 : 0;
      c->busymonitor = !strcmp (conf_item[2], "ON") ? 1 : 0;
      c->logcpu = !strcmp (conf_item[3], "ON") ? 1 : 0;
      c->logbusy = !strcmp (conf_item[4], "ON") ? 1 : 0;
      c->cpurestart = !strcmp (conf_item[5], "ON") ? 1 : 0;
      c->busyrestart = !strcmp (conf_item[6], "ON") ? 1 : 0;
      c->cpulimit = atoi (conf_item[7]);
      c->busylimit = atoi (conf_item[8]);
      c->lrt = -1;
      c->next = NULL;
    }				/* end of while */
  fclose (infile);
}

static void
aj_unicasm_handler (void *hd, time_t prev_check_time, time_t cur_time)
{
  char strbuf[1024];
  unicasm_node *c;
  int ret;
  float cpulimit;
  T_DM_UC_INFO br_info;
  int i;

  for (c = (unicasm_node *) (hd); c != NULL; c = c->next)
    {
      if (c->bname == NULL)
	return;
      if (uca_as_info (c->bname, &br_info, NULL, strbuf) < 0)
	continue;

      cpulimit = c->cpulimit;
      for (i = 0; i < br_info.num_info; i++)
	{
	  if (c->cpumonitor == 1 && br_info.info.as_info[i].pcpu >= cpulimit)
	    {
	      ret = -1;
	      if (c->cpurestart == 1)
		{
		  c->lrt = time (NULL);
		  ret =
		    aj_restart_broker_as (c->bname,
					  br_info.info.as_info[i].id);
		}
	      if (c->logcpu == 1)
		{
		  auto_unicas_log_write (c->bname, &(br_info.info.as_info[i]),
					 !ret, 1, 0);
		}
	    }
	  if (c->busymonitor == 1 &&
	      strcmp (br_info.info.as_info[i].status, "BUSY") == 0)
	    {
	      time_t busy_time;
	      time_t cur_time = time (NULL);

	      busy_time = cur_time - br_info.info.as_info[i].last_access_time;
	      if ((busy_time >= c->busylimit)
		  && (cur_time - c->lrt >= c->busylimit))
		{
		  ret = -1;
		  if (c->busyrestart == 1)
		    {
		      c->lrt = time (NULL);
		      ret =
			aj_restart_broker_as (c->bname,
					      br_info.info.as_info[i].id);
		    }
		  if (c->logcpu == 1)
		    {
		      auto_unicas_log_write (c->bname,
					     &(br_info.info.as_info[i]), !ret,
					     0, busy_time);
		    }
		}
	    }
	}
      uca_as_info_free (&br_info, NULL);
    }				/* end of for */
}

static int
aj_restart_broker_as (char *bname, int id)
{
  char err_msg[1024];

  if (uca_restart (bname, id, err_msg) < 0)
    return -1;
  return 0;
}

static void
auto_unicas_log_write (char *br_name, T_DM_UC_AS_INFO * as_info,
		       int restart_flag, char cpu_flag, time_t busy_time)
{
  char logfile[512];
  FILE *outfile;
  time_t t;
  char timestr[64];

  t = time (NULL);

  outfile =
    fopen (conf_get_dbmt_file (FID_UC_AUTO_RESTART_LOG, logfile), "a");
  if (outfile == NULL)
    return;

  time_to_str (t, "%04d/%02d/%02d/%02d:%02d:%02d", timestr,
	       TIME_STR_FMT_DATE_TIME);
  fprintf (outfile, "%s %s %d %d ", br_name, timestr, as_info->id,
	   as_info->psize);
  if (cpu_flag)
    fprintf (outfile, "%.2f ", as_info->pcpu);
  else
    fprintf (outfile, "- ");
  if (busy_time > 0)
    fprintf (outfile, "%d ", busy_time);
  else
    fprintf (outfile, "- ");
  if (restart_flag)
    fprintf (outfile, "YES ");
  else
    fprintf (outfile, "NO ");
  fprintf (outfile, "\n");
  fclose (outfile);
}
