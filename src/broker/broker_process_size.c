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
 * broker_process_size.c - Get process size
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

#if !defined(WINDOWS)
#include <unistd.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <io.h>
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
#include "broker_process_size.h"

#if defined(HPUX)
#include "hp_pstat.h"
#endif

#define GETSIZE_PATH            "getsize"

#if defined(LINUX) || defined(ALPHA_LINUX)
static char *skip_token (char *p);
#endif

#if defined(WINDOWS)
#define ALLOC_COUNTER_VALUE()                                           \
        do {                                                            \
            int _mem_size = sizeof(PDH_FMT_COUNTERVALUE_ITEM) *         \
                                   num_counter_value;                   \
            cntvalue_pid = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_pid, _mem_size);               \
            cntvalue_workset = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_workset, _mem_size);       \
            cntvalue_pct_cpu = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_pct_cpu, _mem_size);       \
            cntvalue_num_thr = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_num_thr, _mem_size);       \
        } while (0)

#define IS_COUNTER_VALUE_PTR_NULL()                                     \
        (cntvalue_pid == NULL || cntvalue_workset == NULL ||            \
         cntvalue_pct_cpu == NULL || cntvalue_num_thr == NULL)

typedef PDH_STATUS (__stdcall * PDHOpenQuery) (LPCSTR, DWORD_PTR,
					       PDH_HQUERY *);
typedef PDH_STATUS (__stdcall * PDHCloseQuery) (PDH_HQUERY);
typedef PDH_STATUS (__stdcall * PDHAddCounter) (PDH_HQUERY, LPCSTR, DWORD_PTR,
						PDH_HCOUNTER *);
typedef PDH_STATUS (__stdcall * PDHCollectQueryData) (PDH_HQUERY);
typedef PDH_STATUS (__stdcall * PDHGetFormattedCounterArray) (PDH_HCOUNTER,
							      DWORD, LPDWORD,
							      LPDWORD,
							      PPDH_FMT_COUNTERVALUE_ITEM_A);
PDHOpenQuery fp_PdhOpenQuery;
PDHCloseQuery fp_PdhCloseQuery;
PDHAddCounter fp_PdhAddCounter;
PDHCollectQueryData fp_PdhCollectQueryData;
PDHGetFormattedCounterArray fp_PdhGetFormattedCounterArray;

HCOUNTER counter_pid;
HCOUNTER counter_pct_cpu;
HCOUNTER counter_workset;
HCOUNTER counter_num_thr;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_pid = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_pct_cpu = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_workset = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_num_thr = NULL;
HQUERY pdh_h_query;
int num_counter_value;
unsigned long pdh_num_proc;
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
    {
      return -1;
    }

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
	{
	  goto retry;
	}
      if (saverr != ENOENT)
	{
	  ;
	}
      return 1;
    }

  close (procfd);

  return (int) (info.pr_bysize / 1024);
}
#elif defined(HPUX)
int
getsize (int pid)
{
  int page_size = sysconf (_SC_PAGESIZE);
  INT64 psize;
  struct pst_status info;

  if (pid <= 0)
    {
      return -1;
    }

  if (HP_PSTAT_GETPROC (&info, pid) < 0)
    {
      if (errno == ESRCH)
	{
	  return -1;
	}
      return 1;
    }

  psize = info.pst_vdsize + info.pst_vtsize + info.pst_vssize;
  psize *= (page_size / 1024);	/* psize by kilobyte */
  return (int) (psize);
}
#elif defined(AIX)
int
getsize (int pid)
{
  struct procsinfo pinfo_buf;
  int page_size = sysconf (_SC_PAGESIZE);
  pid_t tmp_pid = pid;

  if (pid <= 0)
    {
      return -1;
    }

  if (getprocs (&pinfo_buf, sizeof (pinfo_buf), NULL, 0, &tmp_pid, 1) < 0)
    {
      if (kill (pid, 0) < 0)
	{
	  if (errno == ESRCH)
	    {
	      return -1;
	    }
	}
      return 1;
    }
  if (pinfo_buf.pi_pid != pid)
    {
      return -1;
    }

  return (int) (((INT64) pinfo_buf.pi_size) * ((INT64) page_size) / 1024);
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
    {
      return -1;
    }

  sprintf (proc_file, "/proc/%d/psinfo", pid);
  fd = open (proc_file, O_RDONLY);
  if (fd < 0)
    {
      return -1;
    }

  read_len = read (fd, &pinfo, sizeof (pinfo));
  close (fd);
  if (read_len < sizeof (pinfo))
    {
      return 8;
    }
  if (pinfo.pr_size <= 0)
    {
      pinfo.pr_size = 2;
    }

  return (int) (((INT64) pinfo.pr_size) * ((INT64) page_size) / 1024);
}
#elif defined(LINUX) || defined(ALPHA_LINUX)
int
getsize (int pid)
{
  char buf[4096];
  char *p;
  int fd;
  int read_len, i;
  INT64 psize;

  if (pid <= 0)
    {
      return -1;
    }

  sprintf (buf, "/proc/%d/stat", pid);
  fd = open (buf, O_RDONLY);
  if (fd < 0)
    {
      return -1;
    }

  read_len = read (fd, buf, sizeof (buf) - 1);
  close (fd);

  if (read_len < 0 || read_len >= (int) sizeof (buf))
    {
      return 1;
    }
  buf[read_len] = '\0';

  p = strchr (buf, ')');
  p++;
  for (i = 0; i < 20; i++)
    {
      p = skip_token (p);
    }

  psize = atoll (p);
  return (int) (psize / 1024);
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
    {
      return -1;
    }

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
	{
	  goto retry;
	}
      if (saverr != ENOENT)
	{
	  ;
	}
      return 1;
    }

  close (procfd);

  return (int) (((INT64) info.pr_size) * ((INT64) page_size) / 1024);
}
#elif defined(WINDOWS)
int
getsize (int pid)
{
  return 1;
}

int
pdh_get_value (int pid, int *workset, float *pct_cpu, int *br_num_thr)
{
  unsigned long i;

  if (pid <= 0)
    {
      *workset = 0;
      *pct_cpu = 0;
      if (br_num_thr)
	{
	  *br_num_thr = 0;
	}
      return 0;
    }

  for (i = 0; i < pdh_num_proc; i++)
    {
      if (cntvalue_pid[i].FmtValue.longValue == pid)
	{
	  *workset = (int) (cntvalue_workset[i].FmtValue.largeValue / 1024);
	  *pct_cpu = (float) (cntvalue_pct_cpu[i].FmtValue.doubleValue);
	  if (br_num_thr)
	    {
	      *br_num_thr = (int) (cntvalue_num_thr[i].FmtValue.largeValue);
	    }
	  return 0;
	}
    }

  return -1;
}

int
pdh_init ()
{
  HMODULE h_module;
  PDH_STATUS pdh_status;
  CHAR path_buffer[128];

  h_module = LoadLibrary ("pdh.dll");
  if (h_module == NULL)
    {
      return -1;
    }

  fp_PdhOpenQuery = (PDHOpenQuery) GetProcAddress (h_module, "PdhOpenQueryA");
  if (fp_PdhOpenQuery == NULL)
    {
      return -1;
    }

  fp_PdhAddCounter =
    (PDHAddCounter) GetProcAddress (h_module, "PdhAddCounterA");
  if (fp_PdhAddCounter == NULL)
    {
      return -1;
    }

  fp_PdhCollectQueryData =
    (PDHCollectQueryData) GetProcAddress (h_module, "PdhCollectQueryData");
  if (fp_PdhCollectQueryData == NULL)
    {
      return -1;
    }

  fp_PdhGetFormattedCounterArray =
    (PDHGetFormattedCounterArray) GetProcAddress (h_module,
						  "PdhGetFormattedCounterArrayA");
  if (fp_PdhGetFormattedCounterArray == NULL)
    {
      return -1;
    }

  fp_PdhCloseQuery =
    (PDHCloseQuery) GetProcAddress (h_module, "PdhCloseQuery");
  if (fp_PdhCloseQuery == NULL)
    {
      return -1;
    }

  pdh_status = (*fp_PdhOpenQuery) (0, 0, &pdh_h_query);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\ID Process");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_pid);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\Working Set");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_workset);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\% Processor Time");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_pct_cpu);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\Thread Count");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_num_thr);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  num_counter_value = 128;

  ALLOC_COUNTER_VALUE ();
  if (IS_COUNTER_VALUE_PTR_NULL ())
    {
      return -1;
    }
  memset (cntvalue_pid, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_workset, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_pct_cpu, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_num_thr, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);

  return 0;
}

int
pdh_collect ()
{
  unsigned long in_size;
  PDH_STATUS pdh_status;
  int i, retry_count = 10;
  char success_flag = FALSE;

  if (IS_COUNTER_VALUE_PTR_NULL ())
    {
      ALLOC_COUNTER_VALUE ();
      if (IS_COUNTER_VALUE_PTR_NULL ())
	goto collect_error;
    }

  for (i = 0; i < retry_count; i++)
    {
      pdh_status = (*fp_PdhCollectQueryData) (pdh_h_query);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      in_size = sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value;

      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_pid, PDH_FMT_LONG,
					   &in_size, &pdh_num_proc,
					   cntvalue_pid);
      if (pdh_status != ERROR_SUCCESS)
	{
	  if (pdh_status == PDH_MORE_DATA)
	    {
	      num_counter_value *= 2;
	      ALLOC_COUNTER_VALUE ();
	      if (IS_COUNTER_VALUE_PTR_NULL ())
		{
		  goto collect_error;
		}
	    }
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_workset, PDH_FMT_LARGE,
					   &in_size, &pdh_num_proc,
					   cntvalue_workset);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_pct_cpu, PDH_FMT_DOUBLE,
					   &in_size, &pdh_num_proc,
					   cntvalue_pct_cpu);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_num_thr, PDH_FMT_LONG,
					   &in_size, &pdh_num_proc,
					   cntvalue_num_thr);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}

      success_flag = TRUE;
      break;
    }

  if (success_flag == TRUE)
    {
      return 0;
    }

collect_error:
  pdh_num_proc = 0;
  return -1;
}
#else
int
getsize (int pid)
{
  FILE *pp;
  char cmd[256];
  char buf[256];
  int i, c;
  INT64 psize;

  if (pid <= 0)
    {
      return -1;
    }

  sprintf (cmd, "%s %d", GETSIZE_PATH, pid);
  pp = popen (cmd, "r");
  if (pp == NULL)
    {
      return -1;
    }
  i = 0;
  while ((c = getc (pp)) != EOF)
    {
      buf[i++] = c;
    }
  buf[i] = '\0';
  pclose (pp);
  psize = atoll (buf);

  if (psize <= 0)
    {
      if (kill (pid, 0) < 0)
	{
	  if (errno == ESRCH)
	    {
	      return -1;
	    }
	}
      return 1;
    }

  return (int) psize;
}
#endif
