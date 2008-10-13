/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * server_stat.h - 
 */

#ifndef _SERVER_STAT_H_
#define _SERVER_STAT_H_

#ident "$Id$"

#include <time.h>

#if !defined(HOST_MONITOR_PROC) && defined(HOST_MONITOR_IO)
#undef HOST_MONITOR_IO
#endif

#include "nameval.h"
#include "dbmt_config.h"

#define CPUSTATES     5
#define CPUSTATE_IOWAIT 3
#define CPUSTATE_SWAP   4

typedef struct
{
#ifdef HOST_MONITOR_PROC
  int load_avg[3];
  int memory_stats[5];
  int cpu_states[CPUSTATES];
#else
  int dummy;
#endif
} sys_stat;

typedef struct
{
#ifdef HOST_MONITOR_PROC
  char db_name[64];
  int db_pid;
  unsigned long db_size;
  char proc_stat[2];
  time_t db_start_time;
  double db_cpu_usage;
  double db_mem_usage;
#else
  int dummy;
#endif
} db_stat;

typedef struct
{
#ifdef HOST_MONITOR_PROC
  char cas_name[64];
  int cas_pid;
  unsigned long cas_size;
  char proc_stat[2];
  time_t cas_start_time;
  double cas_cpu_usage;
  double cas_mem_usage;
#else
  int dummy;
#endif
} cas_stat;

typedef struct
{
  char dbsrv_refresh_flag;
  time_t last_request_time;
  sys_stat ssbuf;
  int dbvect[MAX_INSTALLED_DB];	/* 0:inactive, 1:active */
  db_stat dbbuf[MAX_INSTALLED_DB];
  int casvect[MAX_UNICAS_PROC];
  cas_stat casbuf[MAX_UNICAS_PROC];
} userdata;

void record_system_info (sys_stat * sstat);
void record_cubrid_proc_info (userdata * ud);
void record_unicas_proc_info (int casv[], cas_stat casb[]);

#endif /* __SERVER_STAT_H_ */
