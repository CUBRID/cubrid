/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * getsize.c - Get process size
 *		  Solaris, HPUX - by system call
 *		  others - by 'ps' cmd (popen)
 *	return values
 *		> 1 	success
 *		1 	cannot get process information
 *		<= 0	no such process
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <sys/procfs.h>
#elif defined(OSF1)
#include <fcntl.h>
#include <sys/procfs.h>
#endif

#include "cas_common.h"
#include "getsize.h"

#if defined(HPUX)
#include "hp_pstat.h"
#endif

#define GETSIZE_PATH            "getsize"

#if defined(LINUX) || defined(ALPHA_LINUX)
static char *skip_token (char *p);
#endif

#if defined(SOLARIS)
int
getsize (int pid)
{
  int procfd;
  struct prpsinfo info;
  const char *procdir = "/proc";
  char pname[128];

  if (pid <= 0)
    return -1;

  sprintf (pname, "%s/%05d", procdir, pid);

retry:
  if ((procfd = open (pname, O_RDONLY)) == -1)
    {
      return -1;
    }

  if (ioctl (procfd, PIOCPSINFO, (char *) &info) == -1)
    {
      int saverr = errno;

      close (procfd);
      if (saverr == EAGAIN)
	goto retry;
      if (saverr != ENOENT)
	;
      return 1;
    }

  close (procfd);

  return ((info.pr_bysize) / 1024);
}
#elif defined(HPUX)
int
getsize (int pid)
{
  int page_size = sysconf (_SC_PAGESIZE);
  int psize;
  struct pst_status info;

  if (pid <= 0)
    return -1;

  if (HP_PSTAT_GETPROC (&info, pid) < 0)
    {
      if (errno == ESRCH)
	return -1;
      return 1;
    }

  psize = info.pst_vdsize + info.pst_vtsize + info.pst_vssize;
  psize *= (page_size / 1024);	/* psize by kilobyte */
  return (psize);
}
#elif defined(AIX)
int
getsize (int pid)
{
  struct procsinfo pinfo_buf;
  int page_size = sysconf (_SC_PAGESIZE);
  pid_t tmp_pid = pid;

  if (pid <= 0)
    return -1;

  if (getprocs (&pinfo_buf, sizeof (pinfo_buf), NULL, 0, &tmp_pid, 1) < 0)
    {
      if (kill (pid, 0) < 0)
	{
	  if (errno == ESRCH)
	    return -1;
	}
      return 1;
    }
  if (pinfo_buf.pi_pid != pid)
    return -1;

  return (pinfo_buf.pi_size * page_size / 1024);
}
#elif defined(UNIXWARE7)
int
getsize (int pid)
{
  char proc_file[128];
  psinfo_t pinfo;
  int read_len;
  int fd;
  int page_size = sysconf (_SC_PAGESIZE);

  if (pid <= 0)
    return -1;

  sprintf (proc_file, "/proc/%d/psinfo", pid);
  fd = open (proc_file, O_RDONLY);
  if (fd < 0)
    return -1;

  read_len = read (fd, &pinfo, sizeof (pinfo));
  close (fd);
  if (read_len < sizeof (pinfo))
    {
      return 8;
    }
  if (pinfo.pr_size <= 0)
    pinfo.pr_size = 2;

  return (pinfo.pr_size * page_size / 1024);
}
#elif defined(LINUX) || defined(ALPHA_LINUX)
int
getsize (int pid)
{
  char buf[4096];
  char *p;
  int fd;
  int read_len, psize, i;

  if (pid <= 0)
    return -1;

  sprintf (buf, "/proc/%d/stat", pid);
  fd = open (buf, O_RDONLY);
  if (fd < 0)
    {
      return -1;
    }

  read_len = read (fd, buf, sizeof (buf) - 1);
  close (fd);

  if (read_len < 0)
    {
      return 1;
    }
  buf[read_len] = '\0';

  p = strchr (buf, ')');
  p++;
  for (i = 0; i < 20; i++)
    p = skip_token (p);

  sscanf (p, "%d", &psize);
  return (psize / 1024);
}

static char *
skip_token (char *p)
{
  while (isspace (*p))
    p++;
  while (*p && !isspace (*p))
    p++;
  return p;
}
#elif defined(OSF1)
int
getsize (int pid)
{
  int page_size = sysconf (_SC_PAGESIZE);
  int procfd;
  struct prpsinfo info;
  const char *procdir = "/proc";
  char pname[128];

  if (pid <= 0)
    return -1;

  sprintf (pname, "%s/%05d", procdir, pid);

retry:
  if ((procfd = open (pname, O_RDONLY)) == -1)
    {
      return -1;
    }

  if (ioctl (procfd, PIOCPSINFO, (char *) &info) == -1)
    {
      int saverr = errno;

      close (procfd);
      if (saverr == EAGAIN)
	goto retry;
      if (saverr != ENOENT)
	;
      return 1;
    }

  close (procfd);

  return (info.pr_size * page_size / 1024);
}
#elif defined(WIN32)
int
getsize (int pid)
{
  return 1;
}
#else
int
getsize (int pid)
{
  FILE *pp;
  char cmd[256];
  char buf[16];
  int i, c;
  int psize;

  if (pid <= 0)
    return -1;

  sprintf (cmd, "%s %d", GETSIZE_PATH, pid);
  pp = popen (cmd, "r");
  if (pp == NULL)
    return -1;
  i = 0;
  while ((c = getc (pp)) != EOF)
    buf[i++] = c;
  buf[i] = '\0';
  pclose (pp);
  psize = atoi (buf);

  if (psize <= 0)
    {
      if (kill (pid, 0) < 0)
	{
	  if (errno == ESRCH)
	    return -1;
	}
      return 1;
    }

  return psize;
}
#endif
