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
 * boot_perf_trace.c - Startup phase TSC instrumentation (debug-only).
 */

#ident "$Id$"

#include "config.h"

#if !defined(NDEBUG)

#include <stdio.h>
#include <string.h>

#include "boot_perf_trace.h"
#include "tsc_timer.h"
#include "system_parameter.h"
#include "error_manager.h"

#include "memory_wrapper.hpp"	// XXX: SHOULD BE THE LAST INCLUDE HEADER

#define BOOT_PERF_TRACE_MAX_PHASES 64

typedef struct boot_perf_phase BOOT_PERF_PHASE;
struct boot_perf_phase
{
  const char *name;		/* pointer equality used for lookup: callers must pass string literals */
  TSC_TICKS start_ticks;
  TSC_TICKS end_ticks;
  bool has_begin;
  bool has_end;
};

static BOOT_PERF_PHASE boot_Perf_phases[BOOT_PERF_TRACE_MAX_PHASES];
static int boot_Perf_phase_count = 0;

static BOOT_PERF_PHASE *boot_perf_find_or_append (const char *name);

static BOOT_PERF_PHASE *
boot_perf_find_or_append (const char *name)
{
  int i;

  for (i = 0; i < boot_Perf_phase_count; i++)
    {
      if (boot_Perf_phases[i].name == name || strcmp (boot_Perf_phases[i].name, name) == 0)
	{
	  return &boot_Perf_phases[i];
	}
    }
  if (boot_Perf_phase_count >= BOOT_PERF_TRACE_MAX_PHASES)
    {
      return NULL;
    }
  boot_Perf_phases[boot_Perf_phase_count].name = name;
  boot_Perf_phases[boot_Perf_phase_count].has_begin = false;
  boot_Perf_phases[boot_Perf_phase_count].has_end = false;
  return &boot_Perf_phases[boot_Perf_phase_count++];
}

void
boot_phase_begin_ (const char *name)
{
  BOOT_PERF_PHASE *p;

  if (!prm_get_bool_value (PRM_ID_BOOT_PERF_TRACE))
    {
      return;
    }
  p = boot_perf_find_or_append (name);
  if (p == NULL)
    {
      return;
    }
  tsc_getticks (&p->start_ticks);
  p->has_begin = true;
}

void
boot_phase_end_ (const char *name)
{
  BOOT_PERF_PHASE *p;

  if (!prm_get_bool_value (PRM_ID_BOOT_PERF_TRACE))
    {
      return;
    }
  p = boot_perf_find_or_append (name);
  if (p == NULL)
    {
      return;
    }
  tsc_getticks (&p->end_ticks);
  p->has_end = true;
}

void
boot_phase_dump (void)
{
  int i, j;
  int order[BOOT_PERF_TRACE_MAX_PHASES];
  UINT64 elapsed_us[BOOT_PERF_TRACE_MAX_PHASES];
  TSCTIMEVAL tv;

  if (!prm_get_bool_value (PRM_ID_BOOT_PERF_TRACE))
    {
      return;
    }
  if (boot_Perf_phase_count == 0)
    {
      return;
    }

  for (i = 0; i < boot_Perf_phase_count; i++)
    {
      order[i] = i;
      if (boot_Perf_phases[i].has_begin && boot_Perf_phases[i].has_end)
	{
	  tsc_elapsed_time_usec (&tv, boot_Perf_phases[i].end_ticks, boot_Perf_phases[i].start_ticks);
	  elapsed_us[i] = (UINT64) tv.tv_sec * 1000000ULL + (UINT64) tv.tv_usec;
	}
      else
	{
	  elapsed_us[i] = 0;
	}
    }

  /* insertion sort, desc by elapsed_us (phase count is <= 64) */
  for (i = 1; i < boot_Perf_phase_count; i++)
    {
      int key = order[i];
      UINT64 key_val = elapsed_us[key];
      j = i - 1;
      while (j >= 0 && elapsed_us[order[j]] < key_val)
	{
	  order[j + 1] = order[j];
	  j--;
	}
      order[j + 1] = key;
    }

  er_log_debug (ARG_FILE_LINE, "=== BOOT PERF TRACE (phases=%d) ===", boot_Perf_phase_count);
  for (i = 0; i < boot_Perf_phase_count; i++)
    {
      int idx = order[i];
      const char *flag = (boot_Perf_phases[idx].has_begin && boot_Perf_phases[idx].has_end) ? "" : " (incomplete)";
      er_log_debug (ARG_FILE_LINE, "  %-40s %12llu us%s",
		    boot_Perf_phases[idx].name, (unsigned long long) elapsed_us[idx], flag);
    }
}

#endif /* !NDEBUG */
