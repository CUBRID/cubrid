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
 * cm_mem_cpu_stat.c -
 */
#include "config.h"
#include "cm_stat.h"
#include "cm_dep.h"
#include "cm_portable.h"
#include "cm_errmsg.h"
#include "cm_defines.h"
#include "utility.h"
#include "environment_variable.h"
#include "cm_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>

#if defined(WINDOWS)
#include <process.h>
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#endif

#if defined(_AIX)
#include <libperfstat.h>
#include <fcntl.h>
#include <sys/procfs.h>
#endif

typedef void *(*EXTRACT_FUNC) (FILE * fp, const char *arg1, T_CM_ERROR * arg2);
static void *extract_db_stat (FILE * fp, const char *tdbname, T_CM_ERROR * err_buf);
static void *extract_host_partition_stat (FILE * fp, const char *arg1, T_CM_ERROR * err_buf);
static void assign_db_stat (T_CM_DB_PROC_STAT * db_stat, char *db_name, T_CM_PROC_STAT * stat);
static void *extract_db_exec_stat (FILE * fp, const char *dbname, T_CM_ERROR * err_buf);
static void *cm_get_command_result (const char *argv[], EXTRACT_FUNC func, const char *func_arg1,
				    T_CM_ERROR * func_arg2);
static T_CM_BROKER_PROC_STAT *broker_stat_alloc_init (const char *bname, int num_as, int br_pid, int *as_pids,
						      T_CM_ERROR * err_buf);

#if !defined(WINDOWS)
static long get_pagesize (void);
#endif

/* strncpy safety version, set dest[buf_len - 1] to '\0' */
static char *strcpy_limit (char *dest, const char *src, int buf_len);

#define NELEMS(x) ((sizeof (x))/(sizeof ((x)[0])))

static void cm_db_proc_stat_free (T_CM_DB_PROC_STAT * stat);
int
cm_get_db_proc_stat (const char *db_name, T_CM_DB_PROC_STAT * stat, T_CM_ERROR * err_buf)
{
  T_CM_DB_PROC_STAT *p = NULL;
  char cub_path[PATH_MAX];

  /* cubrid server status */
  const char *argv[] = {
    cub_path,
    PRINT_CMD_SERVER,
    PRINT_CMD_STATUS,
    NULL,
  };

  (void) envvar_bindir_file (cub_path, PATH_MAX, UTIL_CUBRID);

  cm_err_buf_reset (err_buf);
  if (db_name == NULL)
    {
      cm_set_error (err_buf, CM_ERR_NULL_POINTER);
      return -1;
    }

  p = (T_CM_DB_PROC_STAT *) cm_get_command_result (argv, extract_db_stat, db_name, err_buf);
  if (p != NULL)
    {
      *stat = *p;
      cm_db_proc_stat_free (p);
      return 0;
    }
  return -1;
}

static void
cm_db_proc_stat_free (T_CM_DB_PROC_STAT * stat)
{
  FREE_MEM (stat);
}

T_CM_DB_PROC_STAT_ALL *
cm_get_db_proc_stat_all (T_CM_ERROR * err_buf)
{
  char cub_path[PATH_MAX];

  /* cubrid server status */
  const char *argv[] = {
    cub_path,
    PRINT_CMD_SERVER,
    PRINT_CMD_STATUS,
    NULL,
  };

  (void) envvar_bindir_file (cub_path, PATH_MAX, UTIL_CUBRID);

  cm_err_buf_reset (err_buf);
  return (T_CM_DB_PROC_STAT_ALL *) cm_get_command_result (argv, extract_db_stat, NULL, err_buf);
}

void
cm_db_proc_stat_all_free (T_CM_DB_PROC_STAT_ALL * stat)
{
  if (stat != NULL)
    {
      FREE_MEM (stat->db_stats);
      FREE_MEM (stat);
    }
}

static int *
get_as_pids (int num_as, T_CM_CAS_INFO_ALL * cas_info_all)
{
  int i = 0, nitem = 0;
  int *as_pids = (int *) calloc (num_as, sizeof (int));
  if (as_pids == NULL)
    return NULL;
  for (i = 0; i < cas_info_all->num_info && nitem < num_as; i++)
    {
      if (strcmp (cas_info_all->as_info[i].service_flag, "ON") != 0)
	continue;
      as_pids[nitem++] = cas_info_all->as_info[i].pid;
    }
  return as_pids;
}

static T_CM_BROKER_PROC_STAT *
broker_stat_alloc_init (const char *bname, int num_as, int br_pid, int *as_pids, T_CM_ERROR * err_buf)
{
  int i, nitem = 0;
  T_CM_BROKER_PROC_STAT *bp;

  bp = (T_CM_BROKER_PROC_STAT *) malloc (sizeof (T_CM_BROKER_PROC_STAT) + (num_as - 1) * sizeof (T_CM_PROC_STAT));
  if (bp == NULL)
    {
      cm_set_error (err_buf, CM_OUT_OF_MEMORY);
      return NULL;
    }

  if (cm_get_proc_stat (&bp->br_stat, br_pid) != 0)
    {
      err_buf->err_code = CM_BROKER_STAT_NOT_FOUND;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), bname);
      FREE_MEM (bp);
      return NULL;
    }

  strncpy (bp->br_name, bname, sizeof (bp->br_name) - 1);
  bp->br_name[sizeof (bp->br_name) - 1] = '\0';

  for (i = 0; i < num_as; i++)
    {
      if (cm_get_proc_stat (&bp->cas_stats[nitem], as_pids[i]) == 0)
	{
	  nitem++;
	}
    }
  bp->ncas = nitem;
  return bp;
}

T_CM_BROKER_PROC_STAT *
cm_get_broker_proc_stat (const char *bname, T_CM_ERROR * err_buf)
{
  int i;
  int num_as;
  int br_pid = 0;
  int *as_pids = NULL;
  T_CM_BROKER_PROC_STAT *bp = NULL;
  T_CM_BROKER_INFO_ALL broker_info_all;
  T_CM_CAS_INFO_ALL cas_info_all;
  T_CM_JOB_INFO_ALL job_info_all;

  cm_err_buf_reset (err_buf);
  if (bname == NULL)
    {
      cm_set_error (err_buf, CM_ERR_NULL_POINTER);
      return NULL;
    }

  if (cm_get_broker_info (&broker_info_all, err_buf) < 0)
    {
      return NULL;
    }
  if (cm_get_cas_info (bname, &cas_info_all, &job_info_all, err_buf) < 0)
    {
      cm_broker_info_free (&broker_info_all);
      return NULL;
    }

  for (i = 0; i < broker_info_all.num_info; i++)
    {
      if (strcmp (broker_info_all.br_info[i].status, "OFF") == 0)
	continue;
      if (strcasecmp (broker_info_all.br_info[i].name, bname) == 0)
	{
	  br_pid = broker_info_all.br_info[i].pid;
	  num_as = broker_info_all.br_info[i].num_as;
	  break;
	}
    }

  if (br_pid == 0)
    {
      err_buf->err_code = CM_BROKER_STAT_NOT_FOUND;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), bname);
      goto finale;
    }

  if ((as_pids = get_as_pids (num_as, &cas_info_all)) == NULL)
    {
      cm_set_error (err_buf, CM_OUT_OF_MEMORY);
      goto finale;
    }

  bp = broker_stat_alloc_init (bname, num_as, br_pid, as_pids, err_buf);
  if (bp == NULL)
    {
      goto finale;
    }

  cm_broker_info_free (&broker_info_all);
  cm_cas_info_free (&cas_info_all, &job_info_all);
  FREE_MEM (as_pids);
  return bp;

finale:
  cm_broker_info_free (&broker_info_all);
  cm_cas_info_free (&cas_info_all, &job_info_all);
  FREE_MEM (as_pids);
  FREE_MEM (bp);

  return NULL;
}

void
cm_broker_proc_stat_free (T_CM_BROKER_PROC_STAT * stat)
{
  FREE_MEM (stat);
}

T_CM_BROKER_PROC_STAT_ALL *
cm_get_broker_proc_stat_all (T_CM_ERROR * err_buf)
{
  T_CM_BROKER_INFO_ALL broker_info_all;
  T_CM_CAS_INFO_ALL cas_info_all;
  T_CM_JOB_INFO_ALL job_info_all;
  int *as_pids = NULL;
  T_CM_BROKER_PROC_STAT **p = NULL;
  int i, nitem = 0;

  T_CM_BROKER_PROC_STAT_ALL *all_stat = NULL;

  if (cm_get_broker_info (&broker_info_all, err_buf) < 0)
    {
      return NULL;
    }

  all_stat = (T_CM_BROKER_PROC_STAT_ALL *) calloc (1, sizeof (T_CM_BROKER_PROC_STAT_ALL));
  p = (T_CM_BROKER_PROC_STAT **) calloc (broker_info_all.num_info, sizeof (T_CM_BROKER_PROC_STAT *));

  if (all_stat == NULL || p == NULL)
    {
      cm_set_error (err_buf, CM_OUT_OF_MEMORY);

      cm_broker_info_free (&broker_info_all);
      FREE_MEM (all_stat);
      FREE_MEM (p);
      return NULL;
    }

  all_stat->br_stats = p;

  for (i = 0; i < broker_info_all.num_info; i++)
    {
      int br_pid = broker_info_all.br_info[i].pid;
      int num_as = broker_info_all.br_info[i].num_as;

      if (strcmp (broker_info_all.br_info[i].status, "OFF") == 0)
	continue;

      if (cm_get_cas_info (broker_info_all.br_info[i].name, &cas_info_all, &job_info_all, err_buf) < 0)
	{
	  goto finale;
	}

      if ((as_pids = get_as_pids (num_as, &cas_info_all)) == NULL)
	{
	  cm_set_error (err_buf, CM_OUT_OF_MEMORY);
	  goto finale;
	}

      if ((all_stat->br_stats[nitem] =
	   broker_stat_alloc_init (broker_info_all.br_info[i].name, num_as, br_pid, as_pids, err_buf)))
	{
	  nitem++;
	}

      cm_cas_info_free (&cas_info_all, &job_info_all);
      FREE_MEM (as_pids);

    }
  all_stat->num_stat = nitem;

  cm_broker_info_free (&broker_info_all);

  return all_stat;

finale:
  cm_broker_info_free (&broker_info_all);
  cm_cas_info_free (&cas_info_all, &job_info_all);
  cm_broker_proc_stat_all_free (all_stat);
  FREE_MEM (as_pids);
  return NULL;
}

void
cm_broker_proc_stat_all_free (T_CM_BROKER_PROC_STAT_ALL * stat)
{
  int i;
  if (stat == NULL)
    return;

  if (stat->br_stats != NULL)
    {
      for (i = 0; i < stat->num_stat; i++)
	{
	  cm_broker_proc_stat_free (stat->br_stats[i]);
	}
      FREE_MEM (stat->br_stats);
    }
  FREE_MEM (stat);
}

#if defined(WINDOWS)

typedef BOOL (WINAPI * GET_SYSTEM_TIMES) (LPFILETIME, LPFILETIME, LPFILETIME);
typedef NTSTATUS (WINAPI * NT_QUERY_SYSTEM_INFORMATION) (SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

/*
 * 0 - init_state.
 * 1 - load GetSystemTime.
 * 2 - load NtQuerySystemInformation.
 * 3 - error state.
 */
static volatile int s_symbol_loaded = 0;
static volatile GET_SYSTEM_TIMES s_pfnGetSystemTimes = NULL;
static volatile NT_QUERY_SYSTEM_INFORMATION s_pfnNtQuerySystemInformation = NULL;

static int
get_cpu_time (__int64 * kernel, __int64 * user, __int64 * idle)
{
  /* this logic allow multi thread init multiple times */
  if (s_symbol_loaded == 0)
    {
      /*
       * kernel32.dll and ntdll.dll is essential DLL about user process.
       * when a process started, that means kernel32.dll and ntdll.dll
       * already load in process memory space.
       * so call LoadLibrary() and FreeLibrary() function once, only
       * increase and decrease dll reference counter. this behavior does
       * not cause kernel32.dll or ntdll.dll unload from current process.
       */

      /*
       * first try find function GetSystemTimes(). Windows OS suport this
       * function since Windows XP SP1, Vista, Server 2003 or Server 2008.
       */
      HMODULE module = LoadLibraryA ("kernel32.dll");
      s_pfnGetSystemTimes = (GET_SYSTEM_TIMES) GetProcAddress (module, "GetSystemTimes");
      FreeLibrary (module);

      if (s_pfnGetSystemTimes != NULL)
	{
	  s_symbol_loaded = 1;
	}
      else
	{
	  /*
	   * OS may be is Windows 2000 or Windows XP. (does not support Windows 9x/NT)
	   * try find function NtQuerySystemInformation()
	   */
	  module = LoadLibraryA ("ntdll.dll");
	  s_pfnNtQuerySystemInformation =
	    (NT_QUERY_SYSTEM_INFORMATION) GetProcAddress (module, "NtQuerySystemInformation");
	  FreeLibrary (module);

	  if (s_pfnNtQuerySystemInformation == NULL)
	    {
	      s_symbol_loaded = 3;
	    }
	  else
	    {
	      s_symbol_loaded = 2;
	    }
	}
    }

  if (s_symbol_loaded == 1)
    {
      FILETIME kernel_time, user_time, idle_time;
      ULARGE_INTEGER lk, lu, li;

      s_pfnGetSystemTimes (&idle_time, &kernel_time, &user_time);

      lk.HighPart = kernel_time.dwHighDateTime;
      lk.LowPart = kernel_time.dwLowDateTime;
      lu.HighPart = user_time.dwHighDateTime;
      lu.LowPart = user_time.dwLowDateTime;
      li.HighPart = idle_time.dwHighDateTime;
      li.LowPart = idle_time.dwLowDateTime;

      /* In win32 system, lk includes "System Idle Process" time, so we should exclude it */
      *kernel = lk.QuadPart - li.QuadPart;
      *user = lu.QuadPart;
      *idle = li.QuadPart;

      return 0;
    }
  else if (s_symbol_loaded == 2)
    {
      SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION sppi;
      ULONG len;

      s_pfnNtQuerySystemInformation (SystemProcessorPerformanceInformation, &sppi, sizeof (sppi), &len);

      /* In win32 system, sppi.KernelTime includes "System Idle Process" time, so we should exclude it */
      *kernel = sppi.KernelTime.QuadPart - sppi.IdleTime.QuadPart;
      *user = sppi.UserTime.QuadPart;
      *idle = sppi.IdleTime.QuadPart;

      return 0;
    }

  return -1;
}

BOOL
SetPrivilege (HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnablePrivilege)
{
  TOKEN_PRIVILEGES tp;
  LUID luid;

  /* receives LUID of privilege */
  if (!LookupPrivilegeValue (NULL, lpszPrivilege, &luid))
    {
      return FALSE;
    }

  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  if (bEnablePrivilege)
    {
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    }
  else
    {
      tp.Privileges[0].Attributes = 0;
    }

  if (!AdjustTokenPrivileges (hToken, FALSE, &tp, sizeof (TOKEN_PRIVILEGES), NULL, NULL))
    {
      return FALSE;
    }

  if (GetLastError () == ERROR_NOT_ALL_ASSIGNED)
    {
      return FALSE;
    }

  return TRUE;
}

int
cm_get_proc_stat (T_CM_PROC_STAT * stat, int pid)
{
  ULARGE_INTEGER lk, lu;
  FILETIME dummy1, dummy2, kt, ut;
  PROCESS_MEMORY_COUNTERS pmc;
  MEMORYSTATUSEX ms;
  HANDLE hProcess = NULL, hToken = NULL;
  int ret = 0;

  stat->pid = pid;
  if (!OpenThreadToken (GetCurrentThread (), (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY), FALSE, &hToken))
    {
      if (GetLastError () == ERROR_NO_TOKEN)
	{
	  if (!ImpersonateSelf (SecurityImpersonation))
	    {
	      return -1;
	    }

	  if (!OpenThreadToken (GetCurrentThread (), (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY), FALSE, &hToken))
	    {
	      return -1;
	    }
	}
      else
	{
	  return -1;
	}
    }

  /* enable SeDebugPrivilege */
  if (!SetPrivilege (hToken, SE_DEBUG_NAME, TRUE))
    {
      ret = -1;
      goto error_exit;
    }

  hProcess = OpenProcess (PROCESS_ALL_ACCESS, FALSE, pid);
  if (hProcess == NULL)
    {
      ret = -1;
      goto error_exit;
    }

  if (!GetProcessTimes (hProcess, &dummy1, &dummy2, &kt, &ut))
    {
      ret = -1;
      goto error_exit;
    }

  lk.HighPart = kt.dwHighDateTime;
  lk.LowPart = kt.dwLowDateTime;
  lu.HighPart = ut.dwHighDateTime;
  lu.LowPart = ut.dwLowDateTime;

  stat->cpu_kernel = lk.QuadPart;
  stat->cpu_user = lu.QuadPart;

  memset (&pmc, 0, sizeof (pmc));
  pmc.cb = sizeof (pmc);

  if (!GetProcessMemoryInfo (hProcess, &pmc, sizeof (pmc)))
    {
      ret = -1;
      goto error_exit;
    }

  stat->mem_physical = pmc.WorkingSetSize;

  memset (&ms, 0, sizeof (ms));
  ms.dwLength = sizeof (ms);

  if (!GlobalMemoryStatusEx (&ms))
    {
      ret = -1;
      goto error_exit;
    }

  stat->mem_virtual = ms.ullTotalVirtual - ms.ullAvailVirtual;

error_exit:
  if (hProcess != NULL)
    {
      CloseHandle (hProcess);
    }
  if (hToken != NULL)
    {
      CloseHandle (hToken);
    }
  return ret;
}

int
cm_get_host_stat (T_CM_HOST_STAT * stat, T_CM_ERROR * err_buf)
{
  __int64 kernel, user, idle;
  PERFORMANCE_INFORMATION pi;

  if (get_cpu_time (&kernel, &user, &idle) != 0)
    {
      return -1;
    }

  stat->cpu_kernel = kernel;
  stat->cpu_user = user;
  stat->cpu_idle = idle;
  stat->cpu_iowait = 0;

  memset (&pi, 0, sizeof (pi));
  pi.cb = sizeof (pi);
  if (!GetPerformanceInfo (&pi, sizeof (pi)))
    {
      return -1;
    }

  stat->mem_physical_total = ((__int64) pi.PhysicalTotal) * pi.PageSize;
  stat->mem_physical_free = ((__int64) pi.PhysicalAvailable) * pi.PageSize;
  stat->mem_swap_total = ((__int64) pi.CommitLimit) * pi.PageSize;
  stat->mem_swap_free = ((__int64) (pi.CommitLimit - pi.CommitTotal)) * pi.PageSize;

  return 0;
}

T_CM_DISK_PARTITION_STAT_ALL *
cm_get_host_disk_partition_stat (T_CM_ERROR * err_buf)
{
  int i, len;
  char buf[160] = { 0 };
  ULONGLONG total_size[32], free_size[32];
  char names[32][4] = { 0 };
  char *token;
  T_CM_DISK_PARTITION_STAT_ALL *res;

  len = GetLogicalDriveStringsA (sizeof (buf), buf);

  for (i = 0; i < len; i++)
    {
      if (buf[i] == 0)
	{
	  buf[i] = ';';
	}
    }

  buf[len - 1] = 0;
  i = 0;

  for (token = strtok (buf, ";"); token != NULL && i < 32; token = strtok (NULL, ";"))
    {
      if (GetDriveTypeA (token) == DRIVE_FIXED)
	{
	  ULARGE_INTEGER ul_total, ul_free;
	  GetDiskFreeSpaceExA (token, &ul_free, &ul_total, NULL);
	  total_size[i] = ul_total.QuadPart;
	  free_size[i] = ul_free.QuadPart;
	  strcpy_limit (names[i], token, sizeof (names[i]));
	  i++;
	}
    }


  res = (T_CM_DISK_PARTITION_STAT_ALL *) malloc (sizeof (T_CM_DISK_PARTITION_STAT_ALL));

  if (res == NULL)
    {
      return NULL;
    }

  res->num_stat = i;
  res->partitions = (T_CM_DISK_PARTITION_STAT *) malloc (sizeof (T_CM_DISK_PARTITION_STAT) * i);

  if (res->partitions == NULL)
    {
      FREE_MEM (res);
      return NULL;
    }

  for (i = 0; i < res->num_stat; i++)
    {
      res->partitions[i].avail = free_size[i];
      res->partitions[i].size = total_size[i];
      res->partitions[i].used = total_size[i] - free_size[i];
      strcpy_limit (res->partitions[i].name, names[i], sizeof (res->partitions[i].name));
    }

  return res;
}

#elif defined(_AIX)

int
cm_get_proc_stat (T_CM_PROC_STAT * stat, int pid)
{
  struct pstatus proc_stat;
  struct psinfo proc_psinfo;
  char file_name[PATH_MAX];
  int fd;
  long int ticks_per_sec;

  if (stat == NULL)
    {
      return -1;
    }

  ticks_per_sec = sysconf (_SC_CLK_TCK);
  snprintf (file_name, PATH_MAX, "/proc/%d/status", pid);

  fd = open (file_name, O_RDONLY);
  if (fd == -1)
    {
      return -1;
    }
  if (read (fd, (void *) &proc_stat, sizeof (struct pstatus)) == -1)
    {
      close (fd);
      return -1;
    }
  close (fd);

  stat->cpu_user = (uint64_t) ((proc_stat.pr_utime.tv_sec + proc_stat.pr_utime.tv_nsec * 10e-9) * ticks_per_sec);
  stat->cpu_kernel = (uint64_t) ((proc_stat.pr_stime.tv_sec + proc_stat.pr_stime.tv_nsec * 10e-9) * ticks_per_sec);

  snprintf (file_name, PATH_MAX, "/proc/%d/psinfo", pid);

  fd = open (file_name, O_RDONLY);
  if (fd == -1)
    {
      return -1;
    }
  if (read (fd, (void *) &proc_psinfo, sizeof (struct psinfo)) == -1)
    {
      close (fd);
      return -1;
    }
  close (fd);

  stat->mem_virtual = proc_psinfo.pr_size * 1024;
  stat->mem_physical = proc_psinfo.pr_rssize * 1024;

  return 0;
}

int
cm_get_host_stat (T_CM_HOST_STAT * stat, T_CM_ERROR * err_buf)
{
  perfstat_cpu_total_t cpu_stat;
  perfstat_memory_total_t mem_info;

  cm_err_buf_reset (err_buf);

  if (stat == NULL)
    {
      cm_set_error (err_buf, CM_ERR_NULL_POINTER);
      return -1;
    }
  if (perfstat_cpu_total (NULL, &cpu_stat, sizeof (perfstat_cpu_total_t), 1) == -1)
    {
      cm_set_error (err_buf, CM_GENERAL_ERROR);
      return -1;
    }
  stat->cpu_user = cpu_stat.user;
  stat->cpu_kernel = cpu_stat.sys;
  stat->cpu_idle = cpu_stat.idle;
  stat->cpu_iowait = cpu_stat.wait;

  if (perfstat_memory_total (NULL, &mem_info, sizeof (perfstat_memory_total_t), 1) == -1)
    {
      cm_set_error (err_buf, CM_GENERAL_ERROR);
      return -1;
    }
  stat->mem_physical_total = mem_info.real_total * 4 * 1024;
  stat->mem_physical_free = mem_info.real_free * 4 * 1024;
  stat->mem_swap_total = mem_info.pgsp_total * 4 * 1024;
  stat->mem_swap_free = mem_info.pgsp_free * 4 * 1024;

  return 0;
}

#else

int
cm_get_proc_stat (T_CM_PROC_STAT * stat, int pid)
{
  long vmem_pages;
  long rmem_pages;
  char fname[PATH_MAX];
  FILE *cpufp, *memfp;

  if (stat == NULL || pid == 0)
    {
      return -1;
    }

  stat->pid = pid;

  snprintf (fname, PATH_MAX - 1, "/proc/%d/stat", (int) pid);
  cpufp = fopen (fname, "r");
  if (!cpufp)
    {
      return -1;
    }

  snprintf (fname, PATH_MAX - 1, "/proc/%d/statm", (int) pid);
  memfp = fopen (fname, "r");
  if (memfp == NULL)
    {
      fclose (cpufp);
      return -1;
    }

  fscanf (cpufp, "%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%lu%lu", &stat->cpu_user, &stat->cpu_kernel);
  fscanf (memfp, "%lu%lu", &vmem_pages, &rmem_pages);	/* 'size' and 'resident' in stat file */
  stat->mem_virtual = vmem_pages * get_pagesize ();
  stat->mem_physical = rmem_pages * get_pagesize ();

  fclose (cpufp);
  fclose (memfp);

  return 0;
}

int
cm_get_host_stat (T_CM_HOST_STAT * stat, T_CM_ERROR * err_buf)
{
  char linebuf[LINE_MAX];
  char prefix[50];
  uint64_t nice;
  uint64_t buffers;
  uint64_t cached;
  FILE *cpufp = NULL;
  FILE *memfp = NULL;
  int n_cpuitem = 0;
  int n_memitem = 0;
  const char *stat_file = "/proc/stat";
  const char *meminfo_file = "/proc/meminfo";

  cm_err_buf_reset (err_buf);

  if (stat == NULL)
    {
      cm_set_error (err_buf, CM_ERR_NULL_POINTER);
      return -1;
    }
  cpufp = fopen (stat_file, "r");
  if (cpufp == NULL)
    {
      err_buf->err_code = CM_FILE_OPEN_FAILED;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), stat_file, strerror (errno));
      return -1;
    }
  memfp = fopen (meminfo_file, "r");
  if (memfp == NULL)
    {
      err_buf->err_code = CM_FILE_OPEN_FAILED;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), meminfo_file,
		strerror (errno));
      fclose (cpufp);
      return -1;
    }

  while (fgets (linebuf, sizeof (linebuf), cpufp))
    {
      sscanf (linebuf, "%49s", prefix);
      if (!strcmp (prefix, "cpu"))
	{
	  sscanf (linebuf, "%*s%lu%lu%lu%lu%lu", &stat->cpu_user, &nice, &stat->cpu_kernel, &stat->cpu_idle,
		  &stat->cpu_iowait);
	  stat->cpu_user += nice;
	  n_cpuitem++;
	  break;
	}
    }
  if (n_cpuitem != 1)
    {
      goto error_handle;
    }

  while (fgets (linebuf, sizeof (linebuf), memfp))
    {
      sscanf (linebuf, "%49s", prefix);
      if (!strcmp (prefix, "MemTotal:"))
	{
	  sscanf (linebuf, "%*s%lu", &stat->mem_physical_total);
	  n_memitem++;
	}
      if (!strcmp (prefix, "MemFree:"))
	{
	  sscanf (linebuf, "%*s%lu", &stat->mem_physical_free);
	  n_memitem++;
	}
      if (!strcmp (prefix, "Buffers:"))
	{
	  sscanf (linebuf, "%*s%lu", &buffers);
	}
      if (!strcmp (prefix, "Cached:"))
	{
	  sscanf (linebuf, "%*s%lu", &cached);
	}
      if (!strcmp (prefix, "SwapTotal:"))
	{
	  sscanf (linebuf, "%*s%lu", &stat->mem_swap_total);
	  n_memitem++;
	}
      if (!strcmp (prefix, "SwapFree:"))
	{
	  sscanf (linebuf, "%*s%lu", &stat->mem_swap_free);
	  n_memitem++;
	}
    }
  if (n_memitem != 4)
    {
      goto error_handle;
    }

  stat->mem_physical_free += (buffers + cached);

  stat->mem_physical_total *= 1024;
  stat->mem_physical_free *= 1024;
  stat->mem_swap_total *= 1024;
  stat->mem_swap_free *= 1024;

  fclose (cpufp);
  fclose (memfp);
  return 0;

error_handle:
  err_buf->err_code = CM_GENERAL_ERROR;
  snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, "%s", "read host info error");
  fclose (cpufp);
  fclose (memfp);
  return -1;
}

T_CM_DISK_PARTITION_STAT_ALL *
cm_get_host_disk_partition_stat (T_CM_ERROR * err_buf)
{
  /* df -TB 1M */
  const char *argv[] = {
    "/bin/df",
    "-TB",
    "1M",
    NULL,
  };

  cm_err_buf_reset (err_buf);
  return (T_CM_DISK_PARTITION_STAT_ALL *) cm_get_command_result (argv, extract_host_partition_stat, NULL, err_buf);
}



#endif // WINDOWS

/* TODO: Find out what this is used for. */
int
cm_get_db_exec_stat (const char *db_name, T_CM_DB_EXEC_STAT * exec_stat, T_CM_ERROR * err_buf)
{
  T_CM_DB_EXEC_STAT *p = NULL;
  char *root_env = NULL;
  char exec_path[PATH_MAX];
  /* cubrid statdump dbname */
  const char *argv[] = {
    exec_path,
    "statdump",
    db_name,
    NULL,
  };

  cm_err_buf_reset (err_buf);
  if (db_name == NULL)
    {
      cm_set_error (err_buf, CM_ERR_NULL_POINTER);
      return -1;
    }

  (void) envvar_bindir_file (exec_path, PATH_MAX, UTIL_CUBRID);
  p = (T_CM_DB_EXEC_STAT *) cm_get_command_result (argv, extract_db_exec_stat, db_name, err_buf);
  if (p != NULL)
    {
      *exec_stat = *p;
      FREE_MEM (p);
      return 0;
    }
  return -1;
}

static void *
extract_host_partition_stat (FILE * fp, const char *arg1, T_CM_ERROR * err_buf)
{
  char linebuf[LINE_MAX];
  char type[512];
  int nitem = 0;
  int nalloc = 10;
  T_CM_DISK_PARTITION_STAT *p = NULL;
  T_CM_DISK_PARTITION_STAT_ALL *stat = NULL;

  stat = (T_CM_DISK_PARTITION_STAT_ALL *) malloc (sizeof (T_CM_DISK_PARTITION_STAT_ALL));
  p = (T_CM_DISK_PARTITION_STAT *) malloc (nalloc * sizeof (T_CM_DISK_PARTITION_STAT));
  if (stat == NULL || p == NULL)
    {
      cm_set_error (err_buf, CM_OUT_OF_MEMORY);
      FREE_MEM (stat);
      FREE_MEM (p);
      return NULL;
    }
  stat->partitions = p;

  while (fgets (linebuf, sizeof (linebuf), fp))
    {
      sscanf (linebuf, "%*s%511s", type);
      if (strstr (type, "ext") == NULL)
	continue;

      if (nitem >= nalloc)
	{
	  nalloc *= 2;
	  stat->partitions =
	    (T_CM_DISK_PARTITION_STAT *) realloc (stat->partitions, nalloc * sizeof (T_CM_DISK_PARTITION_STAT));
	}
      if (stat->partitions)
	{
	  p = stat->partitions + nitem;
	  sscanf (linebuf, "%255s%*s%lu%lu%lu", p->name, &p->size, &p->used, &p->avail);
	  nitem++;
	}
      else
	{
	  cm_host_disk_partition_stat_free (stat);
	  cm_set_error (err_buf, CM_OUT_OF_MEMORY);
	  return NULL;
	}
    }
  stat->num_stat = nitem;

  return stat;
}

typedef struct
{
  const char *prop_name;
  int prop_offset;
} STATDUMP_PROP;

static STATDUMP_PROP statdump_offset[] = {
  {"Num_file_creates", offsetof (T_CM_DB_EXEC_STAT, file_num_creates)},
  {"Num_file_removes", offsetof (T_CM_DB_EXEC_STAT, file_num_removes)},
  {"Num_file_ioreads", offsetof (T_CM_DB_EXEC_STAT, file_num_ioreads)},
  {"Num_file_iowrites", offsetof (T_CM_DB_EXEC_STAT, file_num_iowrites)},
  {"Num_file_iosynches", offsetof (T_CM_DB_EXEC_STAT, file_num_iosynches)},
  {"Num_file_page_allocs", offsetof (T_CM_DB_EXEC_STAT, file_num_page_allocs)},
  {"Num_file_page_deallocs", offsetof (T_CM_DB_EXEC_STAT, file_num_page_deallocs)},
  {"Num_data_page_fetches", offsetof (T_CM_DB_EXEC_STAT, pb_num_fetches)},
  {"Num_data_page_dirties", offsetof (T_CM_DB_EXEC_STAT, pb_num_dirties)},
  {"Num_data_page_ioreads", offsetof (T_CM_DB_EXEC_STAT, pb_num_ioreads)},
  {"Num_data_page_iowrites", offsetof (T_CM_DB_EXEC_STAT, pb_num_iowrites)},
  {"Num_data_page_hash_anchor_waits", offsetof (T_CM_DB_EXEC_STAT, pb_num_hash_anchor_waits)},
  {"Time_data_page_hash_anchor_wait", offsetof (T_CM_DB_EXEC_STAT, pb_time_hash_anchor_wait)},
  {"Num_data_page_fixed", offsetof (T_CM_DB_EXEC_STAT, pb_fixed_cnt)},
  {"Num_data_page_dirty", offsetof (T_CM_DB_EXEC_STAT, pb_dirty_cnt)},
  {"Num_data_page_lru1", offsetof (T_CM_DB_EXEC_STAT, pb_lru1_cnt)},
  {"Num_data_page_lru2", offsetof (T_CM_DB_EXEC_STAT, pb_lru2_cnt)},
  {"Num_data_page_lru3", offsetof (T_CM_DB_EXEC_STAT, pb_lru3_cnt)},
  {"Num_data_page_avoid_dealloc", offsetof (T_CM_DB_EXEC_STAT, pb_avoid_dealloc_cnt)},
  {"Num_data_page_avoid_victim", offsetof (T_CM_DB_EXEC_STAT, pb_avoid_victim_cnt)},
  {"Num_data_page_victim_candidate", offsetof (T_CM_DB_EXEC_STAT, pb_victim_cand_cnt)},
  {"Num_log_page_fetches", offsetof (T_CM_DB_EXEC_STAT, log_num_fetches)},
  {"Num_log_page_ioreads", offsetof (T_CM_DB_EXEC_STAT, log_num_ioreads)},
  {"Num_log_page_iowrites", offsetof (T_CM_DB_EXEC_STAT, log_num_iowrites)},
  {"Num_log_append_records", offsetof (T_CM_DB_EXEC_STAT, log_num_appendrecs)},
  {"Num_log_archives", offsetof (T_CM_DB_EXEC_STAT, log_num_archives)},
  {"Num_log_start_checkpoints", offsetof (T_CM_DB_EXEC_STAT, log_num_start_checkpoints)},
  {"Num_log_end_checkpoints", offsetof (T_CM_DB_EXEC_STAT, log_num_end_checkpoints)},
  {"Num_log_wals", offsetof (T_CM_DB_EXEC_STAT, log_num_wals)},
  {"Num_log_page_replacement", offsetof (T_CM_DB_EXEC_STAT, log_num_replacements)},
  {"Num_log_page_iowrites_for_replacement", offsetof (T_CM_DB_EXEC_STAT, log_num_iowrites_for_replacement)},
  {"Num_page_locks_acquired", offsetof (T_CM_DB_EXEC_STAT, lk_num_acquired_on_pages)},
  {"Num_object_locks_acquired", offsetof (T_CM_DB_EXEC_STAT, lk_num_acquired_on_objects)},
  {"Num_page_locks_converted", offsetof (T_CM_DB_EXEC_STAT, lk_num_converted_on_pages)},
  {"Num_object_locks_converted", offsetof (T_CM_DB_EXEC_STAT, lk_num_converted_on_objects)},
  {"Num_page_locks_re-requested", offsetof (T_CM_DB_EXEC_STAT, lk_num_re_requested_on_pages)},
  {"Num_object_locks_re-requested", offsetof (T_CM_DB_EXEC_STAT, lk_num_re_requested_on_objects)},
  {"Num_page_locks_waits", offsetof (T_CM_DB_EXEC_STAT, lk_num_waited_on_pages)},
  {"Num_object_locks_waits", offsetof (T_CM_DB_EXEC_STAT, lk_num_waited_on_objects)},
  {"Num_object_locks_time_waited_usec", offsetof (T_CM_DB_EXEC_STAT, lk_num_waited_time_on_objects)},
  {"Num_tran_commits", offsetof (T_CM_DB_EXEC_STAT, tran_num_commits)},
  {"Num_tran_rollbacks", offsetof (T_CM_DB_EXEC_STAT, tran_num_rollbacks)},
  {"Num_tran_savepoints", offsetof (T_CM_DB_EXEC_STAT, tran_num_savepoints)},
  {"Num_tran_start_topops", offsetof (T_CM_DB_EXEC_STAT, tran_num_start_topops)},
  {"Num_tran_end_topops", offsetof (T_CM_DB_EXEC_STAT, tran_num_end_topops)},
  {"Num_tran_interrupts", offsetof (T_CM_DB_EXEC_STAT, tran_num_interrupts)},
  {"Num_btree_inserts", offsetof (T_CM_DB_EXEC_STAT, bt_num_inserts)},
  {"Num_btree_deletes", offsetof (T_CM_DB_EXEC_STAT, bt_num_deletes)},
  {"Num_btree_updates", offsetof (T_CM_DB_EXEC_STAT, bt_num_updates)},
  {"Num_btree_covered", offsetof (T_CM_DB_EXEC_STAT, bt_num_covered)},
  {"Num_btree_noncovered", offsetof (T_CM_DB_EXEC_STAT, bt_num_noncovered)},
  {"Num_btree_resumes", offsetof (T_CM_DB_EXEC_STAT, bt_num_resumes)},
  {"Num_btree_multirange_optimization", offsetof (T_CM_DB_EXEC_STAT, bt_num_multi_range_opt)},
  {"Num_btree_splits", offsetof (T_CM_DB_EXEC_STAT, bt_num_splits)},
  {"Num_btree_merges", offsetof (T_CM_DB_EXEC_STAT, bt_num_merges)},
  {"Num_btree_get_stats", offsetof (T_CM_DB_EXEC_STAT, bt_num_get_stats)},
  {"Num_heap_stats_sync_bestspace", offsetof (T_CM_DB_EXEC_STAT, heap_num_stats_sync_bestspace)},
  {"Num_query_selects", offsetof (T_CM_DB_EXEC_STAT, qm_num_selects)},
  {"Num_query_inserts", offsetof (T_CM_DB_EXEC_STAT, qm_num_inserts)},
  {"Num_query_deletes", offsetof (T_CM_DB_EXEC_STAT, qm_num_deletes)},
  {"Num_query_updates", offsetof (T_CM_DB_EXEC_STAT, qm_num_updates)},
  {"Num_query_sscans", offsetof (T_CM_DB_EXEC_STAT, qm_num_sscans)},
  {"Num_query_iscans", offsetof (T_CM_DB_EXEC_STAT, qm_num_iscans)},
  {"Num_query_lscans", offsetof (T_CM_DB_EXEC_STAT, qm_num_lscans)},
  {"Num_query_setscans", offsetof (T_CM_DB_EXEC_STAT, qm_num_setscans)},
  {"Num_query_methscans", offsetof (T_CM_DB_EXEC_STAT, qm_num_methscans)},
  {"Num_query_nljoins", offsetof (T_CM_DB_EXEC_STAT, qm_num_nljoins)},
  {"Num_query_mjoins", offsetof (T_CM_DB_EXEC_STAT, qm_num_mjoins)},
  {"Num_query_objfetches", offsetof (T_CM_DB_EXEC_STAT, qm_num_objfetches)},
  {"Num_query_holdable_cursors", offsetof (T_CM_DB_EXEC_STAT, qm_num_holdable_cursors)},
  {"Num_sort_io_pages", offsetof (T_CM_DB_EXEC_STAT, sort_num_io_pages)},
  {"Num_sort_data_pages", offsetof (T_CM_DB_EXEC_STAT, sort_num_data_pages)},
  {"Num_network_requests", offsetof (T_CM_DB_EXEC_STAT, net_num_requests)},
  {"Num_adaptive_flush_pages", offsetof (T_CM_DB_EXEC_STAT, fc_num_pages)},
  {"Num_adaptive_flush_log_pages", offsetof (T_CM_DB_EXEC_STAT, fc_num_log_pages)},
  {"Num_adaptive_flush_max_pages", offsetof (T_CM_DB_EXEC_STAT, fc_tokens)},
  {"Num_prior_lsa_list_size", offsetof (T_CM_DB_EXEC_STAT, prior_lsa_list_size)},
  {"Num_prior_lsa_list_maxed", offsetof (T_CM_DB_EXEC_STAT, prior_lsa_list_maxed)},
  {"Num_prior_lsa_list_removed", offsetof (T_CM_DB_EXEC_STAT, prior_lsa_list_removed)},
  {"Num_heap_stats_bestspace_entries", offsetof (T_CM_DB_EXEC_STAT, hf_stats_bestspace_entries)},
  {"Num_heap_stats_bestspace_maxed", offsetof (T_CM_DB_EXEC_STAT, hf_stats_bestspace_maxed)},
  {"Time_ha_replication_delay", offsetof (T_CM_DB_EXEC_STAT, ha_repl_delay)},
  {"Num_plan_cache_add", offsetof (T_CM_DB_EXEC_STAT, pc_num_add)},
  {"Num_plan_cache_lookup", offsetof (T_CM_DB_EXEC_STAT, pc_num_lookup)},
  {"Num_plan_cache_hit", offsetof (T_CM_DB_EXEC_STAT, pc_num_hit)},
  {"Num_plan_cache_miss", offsetof (T_CM_DB_EXEC_STAT, pc_num_miss)},
  {"Num_plan_cache_full", offsetof (T_CM_DB_EXEC_STAT, pc_num_full)},
  {"Num_plan_cache_delete", offsetof (T_CM_DB_EXEC_STAT, pc_num_delete)},
  {"Num_plan_cache_invalid_xasl_id", offsetof (T_CM_DB_EXEC_STAT, pc_num_invalid_xasl_id)},
  {"Num_plan_cache_query_string_hash_entries", offsetof (T_CM_DB_EXEC_STAT, pc_num_query_string_hash_entries)},
  {"Num_plan_cache_xasl_id_hash_entries", offsetof (T_CM_DB_EXEC_STAT, pc_num_xasl_id_hash_entries)},
  {"Num_plan_cache_class_oid_hash_entries", offsetof (T_CM_DB_EXEC_STAT, pc_num_class_oid_hash_entries)},
  {"Num_vacuum_log_pages_vaccumed", offsetof (T_CM_DB_EXEC_STAT, vac_num_vacuumed_log_pages)},
  {"Num_vacuum_log_pages_to_vacuum", offsetof (T_CM_DB_EXEC_STAT, vac_num_to_vacuum_log_pages)},
  {"Num_vacuum_prefetch_requests_log_pages", offsetof (T_CM_DB_EXEC_STAT, vac_num_prefetch_requests_log_pages)},
  {"Num_vacuum_prefetch_hits_log_pages", offsetof (T_CM_DB_EXEC_STAT, vac_num_prefetch_hits_log_pages)},
  {"Num_heap_home_inserts", offsetof (T_CM_DB_EXEC_STAT, heap_home_inserts)},
  {"Num_heap_big_inserts", offsetof (T_CM_DB_EXEC_STAT, heap_big_inserts)},
  {"Num_heap_assign_inserts", offsetof (T_CM_DB_EXEC_STAT, heap_assign_inserts)},
  {"Num_heap_home_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_home_deletes)},
  {"Num_heap_home_mvcc_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_home_mvcc_deletes)},
  {"Num_heap_home_to_rel_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_home_to_rel_deletes)},
  {"Num_heap_home_to_big_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_home_to_big_deletes)},
  {"Num_heap_rel_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_rel_deletes)},
  {"Num_heap_rel_mvcc_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_rel_mvcc_deletes)},
  {"Num_heap_rel_to_home_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_home_deletes)},
  {"Num_heap_rel_to_big_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_big_deletes)},
  {"Num_heap_rel_to_rel_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_rel_deletes)},
  {"Num_heap_big_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_big_deletes)},
  {"Num_heap_big_mvcc_deletes", offsetof (T_CM_DB_EXEC_STAT, heap_big_mvcc_deletes)},
  {"Num_heap_new_ver_inserts", offsetof (T_CM_DB_EXEC_STAT, heap_new_ver_inserts)},
  {"Num_heap_home_updates", offsetof (T_CM_DB_EXEC_STAT, heap_home_updates)},
  {"Num_heap_home_to_rel_updates", offsetof (T_CM_DB_EXEC_STAT, heap_home_to_rel_updates)},
  {"Num_heap_home_to_big_updates", offsetof (T_CM_DB_EXEC_STAT, heap_home_to_big_updates)},
  {"Num_heap_rel_updates", offsetof (T_CM_DB_EXEC_STAT, heap_rel_updates)},
  {"Num_heap_rel_to_home_updates", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_home_updates)},
  {"Num_heap_rel_to_rel_updates", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_rel_updates)},
  {"Num_heap_rel_to_big_updates", offsetof (T_CM_DB_EXEC_STAT, heap_rel_to_big_updates)},
  {"Num_heap_big_updates", offsetof (T_CM_DB_EXEC_STAT, heap_big_updates)},
  {"Num_heap_home_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_home_vacuums)},
  {"Num_heap_big_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_big_vacuums)},
  {"Num_heap_rel_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_rel_vacuums)},
  {"Num_heap_insid_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_insid_vacuums)},
  {"Num_heap_remove_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_remove_vacuums)},
  {"Num_heap_next_ver_vacuums", offsetof (T_CM_DB_EXEC_STAT, heap_next_ver_vacuums)},
  {"Time_heap_insert_prepare", offsetof (T_CM_DB_EXEC_STAT, heap_insert_prepare)},
  {"Time_heap_insert_execute", offsetof (T_CM_DB_EXEC_STAT, heap_insert_execute)},
  {"Time_heap_insert_log", offsetof (T_CM_DB_EXEC_STAT, heap_insert_log)},
  {"Time_heap_delete_prepare", offsetof (T_CM_DB_EXEC_STAT, heap_delete_prepare)},
  {"Time_heap_delete_execute", offsetof (T_CM_DB_EXEC_STAT, heap_delete_execute)},
  {"Time_heap_delete_log", offsetof (T_CM_DB_EXEC_STAT, heap_delete_log)},
  {"Time_heap_update_prepare", offsetof (T_CM_DB_EXEC_STAT, heap_update_prepare)},
  {"Time_heap_update_execute", offsetof (T_CM_DB_EXEC_STAT, heap_update_execute)},
  {"Time_heap_update_log", offsetof (T_CM_DB_EXEC_STAT, heap_update_log)},
  {"Time_heap_vacuum_prepare", offsetof (T_CM_DB_EXEC_STAT, heap_vacuum_prepare)},
  {"Time_heap_vacuum_execute", offsetof (T_CM_DB_EXEC_STAT, heap_vacuum_execute)},
  {"Time_heap_vacuum_log", offsetof (T_CM_DB_EXEC_STAT, heap_vacuum_log)},
  {"Num_bt_find_unique", offsetof (T_CM_DB_EXEC_STAT, bt_find_unique_cnt)},
  {"Num_bt_range_search", offsetof (T_CM_DB_EXEC_STAT, bt_range_search_cnt)},
  {"Num_bt_insert_obj", offsetof (T_CM_DB_EXEC_STAT, bt_insert_cnt)},
  {"Num_bt_delete_obj", offsetof (T_CM_DB_EXEC_STAT, bt_delete_cnt)},
  {"Num_bt_mvcc_delete", offsetof (T_CM_DB_EXEC_STAT, bt_mvcc_delete_cnt)},
  {"Num_bt_mark_delete", offsetof (T_CM_DB_EXEC_STAT, bt_mark_delete_cnt)},
  {"Num_bt_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_update_sk_cnt)},
  {"Num_bt_undo_insert", offsetof (T_CM_DB_EXEC_STAT, bt_undo_insert_cnt)},
  {"Num_bt_undo_delete", offsetof (T_CM_DB_EXEC_STAT, bt_undo_delete_cnt)},
  {"Num_bt_undo_mvcc_delete", offsetof (T_CM_DB_EXEC_STAT, bt_undo_mvcc_delete_cnt)},
  {"Num_bt_undo_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_undo_update_sk_cnt)},
  {"Num_bt_vacuum", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_cnt)},
  {"Num_bt_vacuum_insid", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_insid_cnt)},
  {"Num_bt_vacuum_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_update_sk_cnt)},
  {"Num_bt_fix_ovf_oids", offsetof (T_CM_DB_EXEC_STAT, bt_fix_ovf_oids_cnt)},
  {"Num_bt_unique_rlocks", offsetof (T_CM_DB_EXEC_STAT, bt_unique_rlocks_cnt)},
  {"Num_bt_unique_wlocks", offsetof (T_CM_DB_EXEC_STAT, bt_unique_wlocks_cnt)},
  {"Time_bt_find_unique", offsetof (T_CM_DB_EXEC_STAT, bt_find_unique)},
  {"Time_bt_range_search", offsetof (T_CM_DB_EXEC_STAT, bt_range_search)},
  {"Time_bt_insert", offsetof (T_CM_DB_EXEC_STAT, bt_insert)},
  {"Time_bt_delete", offsetof (T_CM_DB_EXEC_STAT, bt_delete)},
  {"Time_bt_mvcc_delete", offsetof (T_CM_DB_EXEC_STAT, bt_mvcc_delete)},
  {"Time_bt_mark_delete", offsetof (T_CM_DB_EXEC_STAT, bt_mark_delete)},
  {"Time_bt_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_update_sk)},
  {"Time_bt_undo_insert", offsetof (T_CM_DB_EXEC_STAT, bt_undo_insert)},
  {"Time_bt_undo_delete", offsetof (T_CM_DB_EXEC_STAT, bt_undo_delete)},
  {"Time_bt_undo_mvcc_delete", offsetof (T_CM_DB_EXEC_STAT, bt_undo_mvcc_delete)},
  {"Time_bt_undo_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_undo_update_sk)},
  {"Time_bt_vacuum", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum)},
  {"Time_bt_vacuum_insid", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_insid)},
  {"Time_bt_vacuum_update_sk", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_update_sk)},
  {"Time_bt_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_traverse)},
  {"Time_bt_find_unique_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_find_unique_traverse)},
  {"Time_bt_range_search_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_range_search_traverse)},
  {"Time_bt_insert_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_insert_traverse)},
  {"Time_bt_delete_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_delete_traverse)},
  {"Time_bt_mvcc_delete_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_mvcc_delete_traverse)},
  {"Time_bt_mark_delete_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_mark_delete_traverse)},
  {"Time_bt_update_sk_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_update_sk_traverse)},
  {"Time_bt_undo_insert_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_undo_insert_traverse)},
  {"Time_bt_undo_delete_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_undo_delete_traverse)},
  {"Time_bt_undo_mvcc_delete_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_undo_mvcc_delete_traverse)},
  {"Time_bt_undo_update_sk_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_undo_update_sk_traverse)},
  {"Time_bt_vacuum_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_traverse)},
  {"Time_bt_vacuum_insid_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_insid_traverse)},
  {"Time_bt_vacuum_update_sk_traverse", offsetof (T_CM_DB_EXEC_STAT, bt_vacuum_update_sk_traverse)},
  {"Time_bt_fix_ovf_oids", offsetof (T_CM_DB_EXEC_STAT, bt_fix_ovf_oids)},
  {"Time_bt_unique_rlocks", offsetof (T_CM_DB_EXEC_STAT, bt_unique_rlocks)},
  {"Time_bt_unique_wlocks", offsetof (T_CM_DB_EXEC_STAT, bt_unique_wlocks)},
  {"Time_vac_master", offsetof (T_CM_DB_EXEC_STAT, vac_master)},
  {"Time_vac_worker_process_log", offsetof (T_CM_DB_EXEC_STAT, vac_worker_process_log)},
  {"Time_vac_worker_execute", offsetof (T_CM_DB_EXEC_STAT, vac_worker_execute)},
  {"Data_page_buffer_hit_ratio", offsetof (T_CM_DB_EXEC_STAT, pb_hit_ratio)},
  {"Log_page_buffer_hit_ratio", offsetof (T_CM_DB_EXEC_STAT, log_hit_ratio)},
  {"Vacuum_data_page_buffer_hit_ratio", offsetof (T_CM_DB_EXEC_STAT, vacuum_data_hit_ratio)},
  {"Vacuum_page_efficiency_ratio", offsetof (T_CM_DB_EXEC_STAT, pb_vacuum_efficiency)},
  {"Vacuum_page_fetch_ratio", offsetof (T_CM_DB_EXEC_STAT, pb_vacuum_fetch_ratio)},
  {"Time_data_page_lock_acquire_time", offsetof (T_CM_DB_EXEC_STAT, pb_page_lock_acquire_time_msec)},
  {"Time_data_page_hold_acquire_time", offsetof (T_CM_DB_EXEC_STAT, pb_page_hold_acquire_time_msec)},
  {"Time_data_page_fix_acquire_time", offsetof (T_CM_DB_EXEC_STAT, pb_page_fix_acquire_time_msec)}
};

static unsigned int *
get_statdump_member_ptr (T_CM_DB_EXEC_STAT * stat, const char *prop_name)
{
  unsigned int i;
  for (i = 0; i < NELEMS (statdump_offset); i++)
    {
      if (strcmp (statdump_offset[i].prop_name, prop_name) == 0)
	{
	  return (unsigned int *) ((char *) stat + statdump_offset[i].prop_offset);
	}
    }
  return NULL;
}

static void *
extract_db_exec_stat (FILE * fp, const char *dbname, T_CM_ERROR * err_buf)
{
  char linebuf[LINE_MAX];
  char prop_name[100];
  T_CM_DB_EXEC_STAT *stat;
  stat = (T_CM_DB_EXEC_STAT *) calloc (1, sizeof (*stat));
  if (stat == NULL)
    {
      cm_set_error (err_buf, CM_OUT_OF_MEMORY);
      return NULL;
    }
  while (fgets (linebuf, sizeof (linebuf), fp))
    {
      unsigned int *member_ptr;
      uint64_t prop_val;
      memset (prop_name, 0, sizeof (prop_name));
      sscanf (linebuf, "%99s%*s%llu", prop_name, &prop_val);
      member_ptr = get_statdump_member_ptr (stat, prop_name);
      if (!member_ptr)
	continue;
      *member_ptr = prop_val;
    }

  return stat;

}

void
cm_host_disk_partition_stat_free (T_CM_DISK_PARTITION_STAT_ALL * stat)
{
  if (stat != NULL)
    {
      FREE_MEM (stat->partitions);
      FREE_MEM (stat);
    }
}

static void *
cm_get_command_result (const char *argv[], EXTRACT_FUNC func, const char *func_arg1, T_CM_ERROR * err_buf)
{
  void *retval;
  FILE *fp = NULL;
  char outputfile[PATH_MAX];
  char errfile[PATH_MAX];
  char tmpfile[PATH_MAX];

  if (make_temp_filename (tmpfile, "cmd_res_", PATH_MAX) < 0)
    {
      return NULL;
    }
  (void) envvar_tmpdir_file (outputfile, PATH_MAX, tmpfile);

  if (make_temp_filename (tmpfile, "cmd_err_", PATH_MAX) < 0)
    {
      return NULL;
    }
  (void) envvar_tmpdir_file (errfile, PATH_MAX, tmpfile);

  if (run_child (argv, 1, NULL, outputfile, errfile, NULL) < 0)
    {
      err_buf->err_code = CM_ERR_SYSTEM_CALL;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), argv[0]);
      return NULL;
    }

  fp = fopen (outputfile, "r");
  if (fp == NULL)
    {
      err_buf->err_code = CM_FILE_OPEN_FAILED;
      snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), outputfile, strerror (errno));
      unlink (outputfile);
      return NULL;
    }

  retval = func (fp, func_arg1, err_buf);	/* call extract function */

  fclose (fp);
  unlink (outputfile);
  unlink (errfile);
  return retval;
}

static void *
extract_db_stat (FILE * fp, const char *tdbname, T_CM_ERROR * err_buf)
{
  char linebuf[LINE_MAX];
  char db_name[512];
  char cmd_name[512];
  int pid;
  T_CM_PROC_STAT pstat;
  T_CM_DB_PROC_STAT_ALL *all_stat = NULL;
  T_CM_DB_PROC_STAT *db_stat = NULL;
  int nitem = 0;
  int nalloc = 10;

  if (tdbname != NULL)
    {
      db_stat = (T_CM_DB_PROC_STAT *) malloc (sizeof (T_CM_DB_PROC_STAT));
      if (!db_stat)
	{
	  cm_set_error (err_buf, CM_OUT_OF_MEMORY);
	  return NULL;
	}
    }
  else
    {
      T_CM_DB_PROC_STAT *p = NULL;
      all_stat = (T_CM_DB_PROC_STAT_ALL *) malloc (sizeof (T_CM_DB_PROC_STAT_ALL));
      p = (T_CM_DB_PROC_STAT *) malloc (nalloc * sizeof (T_CM_DB_PROC_STAT));

      if (all_stat == NULL || p == NULL)
	{
	  cm_set_error (err_buf, CM_OUT_OF_MEMORY);
	  FREE_MEM (all_stat);
	  FREE_MEM (p);
	  return NULL;
	}
      all_stat->db_stats = p;
    }

  while (fgets (linebuf, sizeof (linebuf), fp))
    {
      int tok_num = 0;
      char pid_t[20];

      ut_trim (linebuf);
      if (linebuf[0] == '@')
	continue;

      tok_num = sscanf (linebuf, "%511s %511s %*s %*s %*s %20s", cmd_name, db_name, pid_t);

      if (tok_num != 3 || (strcmp (cmd_name, "Server") != 0 && strcmp (cmd_name, "HA-Server") != 0))
	continue;

      /* remove the ")" at the end of the pid. */
      pid_t[strlen (pid_t) - 1] = '\0';
      pid = atoi (pid_t);

      if (pid == 0)
	continue;

      if (tdbname != NULL)
	{
	  if (!strcmp ((char *) tdbname, db_name))
	    {
	      if (cm_get_proc_stat (&pstat, pid) == 0)
		{
		  assign_db_stat (db_stat, db_name, &pstat);
		  return db_stat;
		}
	      goto not_found;
	    }
	}
      else
	{
	  if (nitem >= nalloc)
	    {
	      T_CM_DB_PROC_STAT *db_stats_newptr = NULL;
	      int nalloc_new = nalloc * 2;
	      db_stats_newptr =
		(T_CM_DB_PROC_STAT *) realloc (all_stat->db_stats, nalloc_new * sizeof (T_CM_DB_PROC_STAT));
	      if (db_stats_newptr == NULL)
		{
		  cm_set_error (err_buf, CM_OUT_OF_MEMORY);
		  return NULL;
		}
	      all_stat->db_stats = db_stats_newptr;
	      nalloc = nalloc_new;
	    }
	  if (cm_get_proc_stat (&pstat, pid) == 0)
	    {
	      assign_db_stat (&all_stat->db_stats[nitem++], db_name, &pstat);
	    }
	}
    }

  if (tdbname == NULL)
    {
      all_stat->num_stat = nitem;
      return all_stat;
    }
  else
    {
      goto not_found;
    }

not_found:
  err_buf->err_code = CM_DB_STAT_NOT_FOUND;
  snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code), tdbname);
  cm_db_proc_stat_free (db_stat);
  return NULL;
}

static void
assign_db_stat (T_CM_DB_PROC_STAT * db_stat, char *db_name, T_CM_PROC_STAT * stat)
{
  strcpy_limit (db_stat->name, db_name, sizeof (db_stat->name));
  db_stat->stat = *stat;
}

#if !defined(WINDOWS)
static long
get_pagesize (void)
{
  return sysconf (_SC_PAGESIZE);
}
#endif

static char *
strcpy_limit (char *dest, const char *src, int buf_len)
{
  size_t src_len = strnlen (src, buf_len - 1);
  memcpy (dest, src, src_len);
  dest[src_len] = '\0';
  return dest;
}
