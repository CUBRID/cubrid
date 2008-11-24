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
 * server_stat.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <sys/sysinfo.h>	/* CPU_WAIT */
#if !defined(AIX)
#include <sys/swap.h>
#endif
#include <stdlib.h>
#include <string.h>		/* strncmp() */
#include <unistd.h>		/* sysconf() */
#if !defined(HPUX) && !defined(AIX)
#include <sys/procfs.h>		/* prpsinfo_t */
#endif
#include <fcntl.h>		/* open() */
#include <dirent.h>
#ifdef HOST_MONITOR_PROC
#include <kstat.h>
#endif

#include "cm_porting.h"
#include "cm_server_stat.h"
#include "cm_nameval.h"
#include "cm_server_util.h"
#include "cm_config.h"

#define MAX_CPU	16

#define CHECK_KCID(nk,ok)	\
	do {			\
	  if (nk == -1) {	\
	    return -1;		\
	  }			\
	  if (nk != ok) {	\
	    goto kcid_changed;	\
	  }			\
	} while (0)

#define PAGETOM(NUMPAGE, PAGESIZE) ((((NUMPAGE) >> 10) * (PAGESIZE)) >> 10) * 10

#ifdef HOST_MONITOR_PROC
static long percentages (int cnt, int *out1, register long *new1,
			 register long *old, long *diffs);
static void get_swapinfo (int *total, int *fr);
static int get_system_info (kstat_ctl_t * kc, sys_stat * sst);
#ifdef HOST_MONITOR_IO
static void record_iostat (nvplist * res);
#endif /* ifdef HOST_MONITOR_IO */
#endif /* ifdef HOST_MONITOR_PROC */

void
record_system_info (sys_stat * sstat)
{
#ifdef HOST_MONITOR_PROC
  static kstat_ctl_t *kc = NULL;

  while (kc == NULL)
    {
      kc = kstat_open ();
      SLEEP_MILISEC (0, 100);
    }

  while (get_system_info (kc, sstat) < 0)
    SLEEP_MILISEC (0, 100);
#endif
}

void
record_cubrid_proc_info (userdata * ud)
{
#ifdef HOST_MONITOR_PROC
  int i, fd;
  prpsinfo_t psbuff;
  char procbuf[50];
  int *vect = ud->dbvect;
  db_stat *buff = ud->dbbuf;

  /* for each db, update 7 values of process */
  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    {
      if (vect[i])
	{
	  sprintf (procbuf, "/proc/%d", buff[i].db_pid);
	  if ((fd = open (procbuf, O_RDONLY)) == -1)
	    {
	      ud->dbsrv_refresh_flag = 1;
	      continue;
	    }
	  if (ioctl (fd, PIOCPSINFO, &psbuff) == -1)
	    {
	      ud->dbsrv_refresh_flag = 1;
	      close (fd);
	      continue;
	    }
	  close (fd);

	  /* fill in the structure */
	  buff[i].db_size = (unsigned long) psbuff.pr_bysize >> 10;
	  buff[i].proc_stat[0] = psbuff.pr_sname;
	  buff[i].db_start_time = psbuff.pr_start.tv_sec;
	  buff[i].db_cpu_usage = (((double) psbuff.pr_pctcpu) / 0x8000 * 100);
	  buff[i].db_mem_usage = (((double) psbuff.pr_pctmem) / 0x8000 * 100);
	}
    }
#endif
}

void
record_unicas_proc_info (int vect[], cas_stat buff[])
{
#ifdef HOST_MONITOR_PROC
  int i, fd;
  prpsinfo_t psbuff;
  char procbuf[50];

  for (i = 0; i < MAX_UNICAS_PROC; ++i)
    {
      if (vect[i])
	{
	  sprintf (procbuf, "/proc/%d", buff[i].cas_pid);

	  if ((fd = open (procbuf, O_RDONLY)) == -1)
	    {
	      vect[i] = 0;
	      continue;
	    }
	  if (ioctl (fd, PIOCPSINFO, &psbuff) == -1)
	    continue;
	  close (fd);

	  /* fill in the structure */
	  buff[i].cas_size = (unsigned long) psbuff.pr_bysize >> 10;
	  buff[i].proc_stat[0] = psbuff.pr_sname;
	  buff[i].cas_start_time = psbuff.pr_start.tv_sec;
	  buff[i].cas_cpu_usage =
	    (((double) psbuff.pr_pctcpu) / 0x8000 * 100);
	  buff[i].cas_mem_usage =
	    (((double) psbuff.pr_pctmem) / 0x8000 * 100);
	}
    }
#endif
}

#ifdef HOST_MONITOR_PROC
static long
percentages (int cnt, int *out1, register long *new1, register long *old,
	     long *diffs)
{
  register int i;
  register long change;
  register long total_change;
  register long *dp;
  long half_total;

  /* initialization */
  total_change = 0;
  dp = diffs;

  /* calculate changes for each state and the overall change */
  for (i = 0; i < cnt; i++)
    {
      if ((change = *new1 - *old) < 0)
	{
	  /* this only happens when the counter wraps */
	  change = (int) ((unsigned long) *new1 - (unsigned long) *old);
	}
      total_change += (*dp++ = change);
      *old++ = *new1++;
    }

  /* avoid divide by zero potential */
  if (total_change == 0)
    {
      total_change = 1;
    }

  /* calculate percentages based on overall change, rounding up */
  half_total = total_change / 2l;
  for (i = 0; i < cnt; i++)
    {
      *out1++ = (int) ((*diffs++ * 1000 + half_total) / total_change);
    }
  /* return the total in case the caller wants to use it */
  return (total_change);
}

static void
get_swapinfo (int *total, int *fr)
{
#ifdef SC_AINFO
  struct anoninfo anon;

  if (swapctl (SC_AINFO, &anon) == -1)
    {
      *total = *fr = 0;
      return;
    }
  *total = anon.ani_max;
  *fr = anon.ani_max - anon.ani_resv;
#else
  register int cnt, i;
  register int t, f;
  struct swaptable *swt;
  struct swapent *ste;
  static char path[256];

  /* get total number of swap entries */
  cnt = swapctl (SC_GETNSWP, 0);

  /* allocate enough space to hold count + n swapents */
  swt =
    (struct swaptable *) malloc (sizeof (int) +
				 cnt * sizeof (struct swapent));
  if (swt == NULL)
    {
      *total = 0;
      *fr = 0;
      return;
    }
  swt->swt_n = cnt;

  /* fill in ste_path pointers: we don't care about the paths, so we point
     them all to the same buffer */
  ste = &(swt->swt_ent[0]);
  i = cnt;
  while (--i >= 0)
    {
      ste++->ste_path = path;
    }

  /* grab all swap info */
  swapctl (SC_LIST, swt);

  /* walk thru the structs and sum up the fields */
  t = f = 0;
  ste = &(swt->swt_ent[0]);
  i = cnt;
  while (--i >= 0)
    {
      /* don't count slots being deleted */
      if (!(ste->ste_flags & ST_INDEL) && !(ste->ste_flags & ST_DOINGDEL))
	{
	  t += ste->ste_pages;
	  f += ste->ste_free;
	}
      ste++;
    }

  /* fill in the results */
  *total = t;
  *fr = f;
  free (swt);
#endif
}

static int
get_system_info (kstat_ctl_t * kc, sys_stat * sst)
{
  kstat_t *ksp;
  kstat_named_t *kn;
  kid_t kcid, nkcid;
  int totalswap, freeswap;
  int i, j;
  cpu_stat_t cpu_stat[MAX_CPU];
  static int pagesize = 0, maxmem = 0;
  static long cp_time[CPUSTATES];
  static long cp_old[CPUSTATES];
  static long cp_diff[CPUSTATES];
  static kstat_t *cpu_ks[MAX_CPU];
  static int ncpu = 0;
  static int freemem_check_time = 0;
  int changed = 0;

  kcid = kc->kc_chain_id;

kcid_changed:

  nkcid = kstat_chain_update (kc);
  if (nkcid)
    {
      kcid = nkcid;
      changed = 1;
    }
  CHECK_KCID (nkcid, 0);

  ksp = kstat_lookup (kc, "unix", -1, "system_misc");
  nkcid = kstat_read (kc, ksp, NULL);
  CHECK_KCID (nkcid, kcid);

  /*
   *  collect load average information
   */
  kn = kstat_data_lookup (ksp, "avenrun_1min");
  sst->load_avg[0] = kn->value.ui32;
  kn = kstat_data_lookup (ksp, "avenrun_5min");
  sst->load_avg[1] = kn->value.ui32;
  kn = kstat_data_lookup (ksp, "avenrun_15min");
  sst->load_avg[2] = kn->value.ui32;

  /*
   *  collect cpu information
   */
  if (changed == 1 || ncpu == 0)
    {
      ncpu = 0;
      for (ksp = kc->kc_chain; ksp && ncpu < MAX_CPU; ksp = ksp->ks_next)
	{
	  if (strncmp (ksp->ks_name, "cpu_stat", 8) == 0)
	    {
	      nkcid = kstat_read (kc, ksp, NULL);
	      CHECK_KCID (nkcid, kcid);
	      cpu_ks[ncpu] = ksp;
	      ncpu++;
	    }
	}
    }

  for (i = 0; i < ncpu; i++)
    {
      nkcid = kstat_read (kc, cpu_ks[i], &(cpu_stat[i]));
      CHECK_KCID (nkcid, kcid);
    }

  for (j = 0; j < CPUSTATES; j++)
    cp_time[j] = 0L;

  for (i = 0; i < ncpu; i++)
    {
      for (j = 0; j < CPU_WAIT; j++)
	cp_time[j] += (long) cpu_stat[i].cpu_sysinfo.cpu[j];

      cp_time[CPUSTATE_IOWAIT] += (long) cpu_stat[i].cpu_sysinfo.wait[W_IO] +
	(long) cpu_stat[i].cpu_sysinfo.wait[W_PIO];
      cp_time[CPUSTATE_SWAP] = (long) cpu_stat[i].cpu_sysinfo.wait[W_SWAP];
    }

  percentages (CPUSTATES, sst->cpu_states, cp_time, cp_old, cp_diff);

  /*
   *  collect memory information
   */
  if (pagesize == 0)
    pagesize = sysconf (_SC_PAGESIZE);
  if (maxmem == 0)
    {
      maxmem = sysconf (_SC_PHYS_PAGES);
      sst->memory_stats[0] = PAGETOM (maxmem, pagesize);
    }

  if (time (NULL) - freemem_check_time > 30)
    {
      ksp = kstat_lookup (kc, "unix", 0, "system_pages");
      nkcid = kstat_read (kc, ksp, NULL);
      CHECK_KCID (nkcid, kcid);

      kn = kstat_data_lookup (ksp, "freemem");
      sst->memory_stats[2] = PAGETOM (kn->value.ui32, pagesize);

      if (sst->memory_stats[0] - sst->memory_stats[2] > 0)
	sst->memory_stats[1] = sst->memory_stats[0] - sst->memory_stats[2];
      freemem_check_time = time (NULL);
    }

#if 0
  kn = kstat_data_lookup (ksp, "availrmem");
#endif

  get_swapinfo (&totalswap, &freeswap);
  sst->memory_stats[3] = PAGETOM ((totalswap - freeswap), pagesize);
  sst->memory_stats[4] = PAGETOM (freeswap, pagesize);

  return 1;
}

#ifdef HOST_MONITOR_IO
static void
record_iostat (nvplist * res)
{
  FILE *infile;
  char buf[1024], d[256], rs[16], ws[16], krs[16], kws[16], wait[16],
    actv[16], svc_t[16], w[16], b[16], tmpfile[256];
  sprintf (tmpfile, "%s/DBMT_rec_iostat.%d", sco.dbmt_tmp_dir,
	   (int) getpid ());
  sprintf (buf, "/usr/bin/iostat -x > %s", tmpfile);

  if (system (buf) != 0)	/* iostat */
    return;
  infile = fopen (tmpfile, "r");
  if (infile == NULL)
    return;
  fgets (buf, 1024, infile);
  fgets (buf, 1024, infile);
  while (fgets (buf, 1024, infile))
    {
      if (sscanf (buf, "%s %s %s %s %s %s %s %s %s %s",
		  d, rs, ws, krs, kws, wait, actv, svc_t, w, b) != 10)
	continue;
      /* name of the disk */
      nv_add_nvp (res, "device", d);
      /* reads per second */
      nv_add_nvp (res, "rs", rs);
      /* writes per second */
      nv_add_nvp (res, "ws", ws);
      /* KBs read per second */
      nv_add_nvp (res, "krs", krs);
      /* KBs written per second */
      nv_add_nvp (res, "kws", kws);
      /* avg # of transactions waiting for service */
      nv_add_nvp (res, "wait", wait);
      /* avg # of transactions being serviced */
      nv_add_nvp (res, "actv", actv);
      /* avg service time in milliseconds */
      nv_add_nvp (res, "svc_t", svc_t);
      /* % of time there are transactions waiting for service */
      nv_add_nvp (res, "w", w);
      /* % of time the disk is busy */
      nv_add_nvp (res, "b", b);
    }
  fclose (infile);
}
#endif /* ifdef HOST_MONITOR_IO */
#endif /* ifdef HOST_MONITOR_PROC */
