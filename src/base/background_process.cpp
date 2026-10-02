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


#include "config.h"
#include "background_process.hpp"
#include "console_log.hpp"
#include <poll.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#if defined(LINUX)
#include <sys/syscall.h>
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// All preparation happens before fork. The private mappings use destinations
// 0..2 (producer) or 0..9 (relay); owned sources are always at least 16.
static int
owned_fd (int fd)
{
  if (fd < 0)
    {
      return -1;
    }
  int result = fcntl (fd, F_DUPFD_CLOEXEC, 16);
  int saved = errno;
  close (fd);
  errno = saved;
  return result;
}

static int
owned_pipe (int ends[2])
{
  int temporary[2];
#if defined(LINUX)
  if (pipe2 (temporary, O_CLOEXEC) != 0)
#else
  if (pipe (temporary) != 0)
#endif
    {
      return -1;
    }
  ends[0] = owned_fd (temporary[0]);
  ends[1] = owned_fd (temporary[1]);
  return ends[0] < 0 || ends[1] < 0 ? -1 : 0;
}

static void
close_interval (unsigned first, unsigned last, unsigned limit)
{
#if defined(SYS_close_range)
  if (syscall (SYS_close_range, first, last, 0) == 0)
    {
      return;
    }
#endif
#if defined(LINUX) && defined(SYS_getdents64)
  // Old kernels and seccomp may reject close_range. Enumerate in the child
  // using raw syscalls and stack storage, not opendir/readdir (libc locks).
  // A parent-side snapshot would miss descriptors opened by other threads.
  int directory = open ("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory >= 0)
    {
      alignas (struct dirent64) char entries[4096];
      long length;
      while ((length = syscall (SYS_getdents64, directory, entries, sizeof (entries))) > 0)
	{
	  for (long offset = 0; offset < length;)
	    {
	      auto *entry = reinterpret_cast<struct dirent64 *> (entries + offset);
	      unsigned fd = 0;
	      const char *digit = entry->d_name;
	      bool numeric = *digit != '\0';
	      for (; *digit; ++digit)
		{
		  if (*digit < '0' || *digit > '9')
		    {
		      numeric = false;
		      break;
		    }
		  fd = fd * 10 + (*digit - '0');
		}
	      if (numeric && fd >= first && fd <= last && fd != static_cast<unsigned> (directory))
		{
		  close (fd);
		}
	      offset += entry->d_reclen;
	    }
	}
      int saved = errno;
      close (directory);
      errno = saved;
      if (length == 0)
	{
	  return;
	}
    }
#endif
  // Portable fallback: the hard limit, never the possibly lowered soft limit.
  for (unsigned fd = first; fd <= last && fd < limit; ++fd)
    {
      close (fd);
    }
}

static int
spawn (const char *path, const char *const args[], const int *sources, int count, bool reset_sigchld)
{
  int errors[2] = {-1, -1};
  if (owned_pipe (errors) != 0)
    {
      if (errors[0] >= 0)
	{
	  close (errors[0]);
	}
      if (errors[1] >= 0)
	{
	  close (errors[1]);
	}
      return -1;
    }
  struct rlimit limit;
  unsigned maximum = INT_MAX;
  if (getrlimit (RLIMIT_NOFILE, &limit) == 0 && limit.rlim_max != RLIM_INFINITY)
    {
      maximum = limit.rlim_max < static_cast<rlim_t> (INT_MAX) ? limit.rlim_max : INT_MAX;
    }
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  sigemptyset (&action.sa_mask);
  pid_t pid = fork ();
  if (pid == 0)
    {
      int error = 0;
      if (reset_sigchld && sigaction (SIGCHLD, &action, nullptr) != 0)
	{
	  error = errno;
	}
      for (int fd = 0; fd < count; ++fd)
	{
	  if (dup2 (sources[fd], fd) < 0)
	    {
	      error = errno;
	      break;
	    }
	}
      close_interval (count, errors[1] - 1, maximum);
      close_interval (errors[1] + 1, UINT_MAX, maximum);
      if (error == 0)
	{
	  execv (path, const_cast<char *const *> (args));
	  error = errno;
	}
      // A failed child must never return to the launcher's control flow.
      while (write (errors[1], &error, sizeof (error)) < 0 && errno == EINTR) {}
      _exit (127);
    }
  int saved = errno;
  close (errors[1]);
  if (pid < 0)
    {
      close (errors[0]);
      errno = saved;
      return -1;
    }
  int error = 0;
  ssize_t length;
  do
    {
      length = read (errors[0], &error, sizeof (error));
    }
  while (length < 0 && errno == EINTR);
  close (errors[0]);
  if (length != 0)
    {
      int status;
      while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
      errno = error != 0 ? error : EIO;
      return -1;
    }
  return pid;
}

int
background_process_prepare_stdio ()
{
  for (int fd = 0; fd < 3; ++fd)
    {
      if (fcntl (fd, F_GETFD) >= 0)
	{
	  continue;
	}
      if (errno != EBADF)
	{
	  return -1;
	}
      int source = open ("/dev/null", O_RDWR);
      if (source < 0)
	{
	  return -1;
	}
      if (source != fd)
	{
	  int result = dup2 (source, fd);
	  int saved = errno;
	  close (source);
	  errno = saved;
	  if (result < 0)
	    {
	      return -1;
	    }
	}
    }
  return 0;
}

int
background_process_spawn_stdio (const char *path, const char *const args[], int output, int error)
{
  // Duplicate in the parent above all three destinations to prevent remap
  // collisions. EBADF means a deliberately closed output; reserve /dev/null.
  int sources[3] = {owned_fd (open ("/dev/null", O_RDWR | O_CLOEXEC)), -1, -1};
  int originals[2] = {output, error};
  for (int i = 0; i < 2; ++i)
    {
      sources[i + 1] = fcntl (originals[i], F_DUPFD_CLOEXEC, 16);
      if (sources[i + 1] < 0 && errno == EBADF)
	{
	  sources[i + 1] = owned_fd (open ("/dev/null", O_RDWR | O_CLOEXEC));
	}
    }
  int pid = -1;
  if (sources[0] >= 0 && sources[1] >= 0 && sources[2] >= 0)
    {
      // PL preserves its historical inherited SIGCHLD disposition.
      pid = spawn (path, args, sources, 3, false);
    }
  int saved = errno;
  for (int fd : sources)
    {
      if (fd >= 0)
	{
	  close (fd);
	}
    }
  errno = saved;
  return pid;
}

int
background_process_start (const char *path, const char *const args[], const char *relay_path,
			  const char *log_path, background_process &process)
{
  process.output_error = 0;
  int fds[14];
  for (int &fd : fds)
    {
      fd = -1;
    }
  int result = -1;
  int saved = 0;
  const char *relay_args[] = {relay_path, log_path, nullptr};
  int relay_sources[10];
  int server_sources[3];
  char marker[1024];
  char database[513];
  int marker_size;
  bool marker_truncated = false;

  fds[0] = owned_fd (open ("/dev/null", O_RDWR | O_CLOEXEC));
  fds[1] = owned_fd (console_log::open_lock (log_path));
  if (fds[0] < 0 || fds[1] < 0)
    {
      goto cleanup;
    }
  database[0] = '\0';
  if (args[1] != nullptr)
    {
      size_t length = strlen (args[1]);
      if (length > (sizeof (database) - 1) / 2)
	{
	  length = (sizeof (database) - 1) / 2;
	  marker_truncated = true;
	}
      for (size_t i = 0; i < length; ++i)
	{
	  snprintf (database + 2 * i, 3, "%02x", static_cast<unsigned char> (args[1][i]));
	}
    }
  marker_size = snprintf (marker, sizeof (marker), "\n[console start time=%lld launcher=%ld database-hex=%s%s]\n",
			  static_cast<long long> (time (nullptr)), static_cast<long> (getpid ()), database,
			  marker_truncated ? " (truncated)" : "");
  if (marker_size < 0 || marker_size >= static_cast<int> (sizeof (marker)))
    {
      errno = ENAMETOOLONG;
      goto cleanup;
    }
  if (!console_log::start (fds[1], log_path, marker, marker_size))
    {
      goto cleanup;
    }
  for (int i = 2; i < 14; i += 2)
    {
      if (owned_pipe (&fds[i]) != 0)
	{
	  goto cleanup;
	}
    }
  relay_sources[0] = relay_sources[1] = relay_sources[2] = fds[0];
  relay_sources[3] = fds[1];
  relay_sources[4] = fds[2];
  relay_sources[5] = fds[4];
  relay_sources[6] = fds[7];
  relay_sources[7] = fds[9];
  relay_sources[8] = fds[10];
  relay_sources[9] = fds[13];
  process.relay_pid = spawn (relay_path, relay_args, relay_sources, 10, true);
  if (process.relay_pid < 0)
    {
      goto cleanup;
    }
  server_sources[0] = fds[0];
  server_sources[1] = fds[3];
  server_sources[2] = fds[5];
  process.pid = spawn (path, args, server_sources, 3, true);
  if (process.pid < 0)
    {
      goto cleanup;
    }
  process.output[0] = fds[6];
  fds[6] = -1;
  process.output[1] = fds[8];
  fds[8] = -1;
  process.control = fds[11];
  fds[11] = -1;
  process.acknowledgement = fds[12];
  fds[12] = -1;
  result = 0;
cleanup:
  saved = errno;
  for (int fd : fds) if (fd >= 0)
      {
	close (fd);
      }
  if (result != 0 && process.relay_pid > 0)
    {
      int status;
      while (waitpid (process.relay_pid, &status, 0) < 0 && errno == EINTR) {}
    }
  errno = saved;
  return result;
}

void
background_process_wait (background_process &process, int milliseconds)
{
  struct timespec start;
  clock_gettime (CLOCK_MONOTONIC, &start);
  int remaining = milliseconds;
  do
    {
      pollfd inputs[2] = {{process.output[0], POLLIN, 0}, {process.output[1], POLLIN, 0}};
      int result = poll (inputs, 2, remaining);
      if (result < 0 && errno != EINTR)
	{
	  process.output_error = errno;
	  return;
	}
      // One bounded read per stream per turn: continuous stdout cannot starve
      // stderr or the caller's original registration/termination observation.
      for (int stream = 0; stream < 2; ++stream)
	{
	  if (inputs[stream].revents == 0)
	    {
	      continue;
	    }
	  char buffer[8192];
	  ssize_t count = read (inputs[stream].fd, buffer, sizeof (buffer));
	  if (count > 0)
	    {
	      if (!console_log::write_all (stream + 1, buffer, count))
		{
		  process.output_error = errno;
		}
	    }
	  else if (count == 0 || errno != EINTR)
	    {
	      if (count < 0)
		{
		  process.output_error = errno;
		}
	      close (process.output[stream]);
	      process.output[stream] = -1;
	    }
	}
      if (process.output[0] < 0 && process.output[1] < 0)
	{
	  return;
	}
      struct timespec now;
      clock_gettime (CLOCK_MONOTONIC, &now);
      remaining = milliseconds - ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
    }
  while (remaining > 0);
}

int
background_process_finish_start (background_process &process)
{
  // The relay snapshots producer queues once. Drain BOTH attempt pipes while
  // it completes that finite barrier, before reading its acknowledgement.
  close (process.control);
  process.control = -1;
  while (process.output[0] >= 0 || process.output[1] >= 0)
    {
      background_process_wait (process, 100);
    }
  unsigned char status = 1;
  ssize_t count;
  do
    {
      count = read (process.acknowledgement, &status, 1);
    }
  while (count < 0 && errno == EINTR);
  int saved = process.output_error != 0 ? process.output_error : EIO;
  close (process.acknowledgement);
  process.acknowledgement = -1;
  if (count != 1 || status != 0 || process.output_error != 0)
    {
      errno = saved;
      return -1;
    }
  return 0;
}
