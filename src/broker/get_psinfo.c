/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * get_psinfo.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#endif

#if defined(SOLARIS)
#include <fcntl.h>
#include <sys/procfs.h>
#elif defined(HPUX)
#include <sys/pstat.h>
#elif defined(AIX)
#include <procinfo.h>
#elif defined(UNIXWARE7)
#include <sys/procfs.h>
#include <fcntl.h>
#elif defined(LINUX) || defined(ALPHA_LINUX)
#include <fcntl.h>
#include <string.h>
#include <sys/procfs.h>
#endif

#include "cas_common.h"
#include "get_psinfo.h"

#if defined(HPUX)
#include "hp_pstat.h"
#endif

#if defined(SOLARIS)
int
get_psinfo (int pid, T_PSINFO * ps)
{
  int procfd;
  struct prstatus pstatus;
  struct prpsinfo pinfo;
  const char *procdir = "/proc";
  char pname[128];

  memset (ps, 0, sizeof (T_PSINFO));

  sprintf (pname, "%s/%05d", procdir, pid);

  if ((procfd = open (pname, O_RDONLY)) == -1)
    {
      return -1;
    }

  if (ioctl (procfd, PIOCSTATUS, (char *) &pstatus) == -1)
    {
      close (procfd);
      return -1;
    }
  if (ioctl (procfd, PIOCPSINFO, (char *) &pinfo) == -1)
    {
      close (procfd);
      return -1;
    }

  close (procfd);

  ps->num_thr = pstatus.pr_nlwp;
  ps->pcpu = (double) pinfo.pr_pctcpu / 0x8000 * 100;
  ps->cpu_time = pinfo.pr_time.tv_sec;

  return 0;
}
#elif defined(HPUX10_2)
int
get_psinfo (int pid, T_PSINFO * ps)
{
  return -1;
}
#elif defined(HPUX)
int
get_psinfo (int pid, T_PSINFO * ps)
{
  struct pst_status info;

  memset (ps, 0, sizeof (T_PSINFO));

  if (HP_PSTAT_GETPROC (&info, pid) < 0)
    {
      return -1;
    }

  ps->num_thr = info.pst_nlwps;
  ps->pcpu = info.pst_pctcpu * 100;
  ps->cpu_time = info.pst_utime + info.pst_stime;

  return (0);
}
#else
int
get_psinfo (int pid, T_PSINFO * ps)
{
  return -1;
}
#endif
