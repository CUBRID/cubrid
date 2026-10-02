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


// Explicit POSIX background output ownership. Direct and synchronous execution
// keep their existing contracts.
#ifndef _BACKGROUND_PROCESS_HPP_
#define _BACKGROUND_PROCESS_HPP_
#if !defined(WINDOWS)
// Call at executable entry, before logging can reuse a closed standard FD.
int background_process_prepare_stdio ();
// Null stdin, explicit stdout/stderr, and no other inherited descriptors.
// Preserves SIGCHLD; returns child PID or -1 with errno. Caller owns reaping.
int background_process_spawn_stdio (const char *path, const char *const args[], int output, int error);
struct background_process
{
  // The caller owns child reaping; this interface does not change SIGCHLD.
  int pid = 0;
  int relay_pid = 0;
  int control = -1;
  int acknowledgement = -1;
  int output_error = 0;
  int output[2] = {-1, -1};
};
int background_process_start (const char *path, const char *const args[], const char *relay_path,
			      const char *log_path, background_process &process);
// Optional bounded framing for multiple producers sharing the caller streams.
// Short diagnostic lines stay intact; longer lines flush at the fixed bound.
struct background_process_output
{
  char bytes[2][8192];
  int used[2] = {0, 0};
};
// Drain bounded startup channels during existing readiness waits.
void background_process_wait (background_process &process, int milliseconds,
			      background_process_output *output = nullptr);
int background_process_finish_start (background_process &process);
#endif
#endif
