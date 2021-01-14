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
 * broker_process_info.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#if !defined(WINDOWS)
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
#include "broker_process_info.h"

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
