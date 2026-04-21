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
 * boot_perf_trace.h - Startup phase TSC instrumentation.
 *
 * The bodies gate on PRM_ID_BOOT_PERF_TRACE at runtime, so the overhead
 * is zero unless the operator explicitly opts in.
 */

#ifndef _BOOT_PERF_TRACE_H_
#define _BOOT_PERF_TRACE_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

  extern void boot_phase_begin_ (const char *name);
  extern void boot_phase_end_ (const char *name);
  extern void boot_phase_dump (void);

#define BOOT_PHASE_BEGIN(name)  boot_phase_begin_ (name)
#define BOOT_PHASE_END(name)    boot_phase_end_ (name)
#define BOOT_PHASE_DUMP()       boot_phase_dump ()

#ifdef __cplusplus
}
#endif

#endif /* _BOOT_PERF_TRACE_H_ */
