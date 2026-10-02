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
#endif
#include <sys/stat.h>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// All preparation happens before fork. Sources are above every destination.
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
  for (unsigned fd = first; fd <= last && fd < limit; ++fd)
    {
      close (fd);
    }
}

static int
spawn (const char *path, const char *const args[], const int *sources, int count)
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
      maximum = limit.rlim_max;
    }
  pid_t pid = fork ();
  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);
      int error = 0;
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
background_process_start (const char *path, const char *const args[], const char *relay_path,
			  const char *log_path, background_process &process)
{
  int fds[12];
  for (int &fd : fds)
    {
      fd = -1;
    }
  int result = -1;
  int saved = 0;
  const char *relay_args[] = {relay_path, nullptr};
  int relay_sources[10];
  int server_sources[3];
  struct stat log_stat;
  char marker[1024];
  char database[513];
  int marker_size;
  bool marker_truncated = false;

  fds[0] = owned_fd (open ("/dev/null", O_RDWR | O_CLOEXEC));
  fds[1] = owned_fd (open (log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
  if (fds[0] < 0 || fds[1] < 0)
    {
      goto cleanup;
    }
  if (fstat (fds[1], &log_stat) != 0)
    {
      goto cleanup;
    }
  if (!S_ISREG (log_stat.st_mode))
    {
      errno = EINVAL;
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
  {
    ssize_t count;
    do
      {
	count = write (fds[1], marker, marker_size);
      }
    while (count < 0 && errno == EINTR);
    if (count != marker_size)
      {
	if (count >= 0)
	  {
	    errno = EIO;
	  }
	goto cleanup;
      }
  }
  for (int i = 2; i < 6; i += 2)
    {
      if (owned_pipe (&fds[i]) != 0)
	{
	  goto cleanup;
	}
    }
  for (int i = 6; i < 8; ++i)
    {
      char name[] = "/tmp/cubrid-console-XXXXXX";
#if defined(LINUX)
      int fd = mkostemp (name, O_CLOEXEC);
#else
      int fd = mkstemp (name);
#endif
      if (fd < 0)
	{
	  goto cleanup;
	}
      unlink (name);
      fds[i] = owned_fd (fd);
      if (fds[i] < 0)
	{
	  goto cleanup;
	}
    }
  if (owned_pipe (&fds[8]) != 0 || owned_pipe (&fds[10]) != 0)
    {
      goto cleanup;
    }
  relay_sources[0] = relay_sources[1] = relay_sources[2] = fds[0];
  relay_sources[3] = fds[1];
  relay_sources[4] = fds[2];
  relay_sources[5] = fds[4];
  relay_sources[6] = fds[6];
  relay_sources[7] = fds[7];
  relay_sources[8] = fds[8];
  relay_sources[9] = fds[11];
  process.relay_pid = spawn (relay_path, relay_args, relay_sources, 10);
  if (process.relay_pid < 0)
    {
      goto cleanup;
    }
  server_sources[0] = fds[0];
  server_sources[1] = fds[3];
  server_sources[2] = fds[5];
  process.pid = spawn (path, args, server_sources, 3);
  if (process.pid < 0)
    {
      goto cleanup;
    }
  process.output[0] = fds[6];
  fds[6] = -1;
  process.output[1] = fds[7];
  fds[7] = -1;
  process.control = fds[9];
  fds[9] = -1;
  process.acknowledgement = fds[10];
  fds[10] = -1;
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

int
background_process_finish_start (background_process &process)
{
  // Closing the control pipe requests a finite snapshot and also handles a
  // caller dying without an explicit completion request.
  close (process.control);
  process.control = -1;
  unsigned char status = 1;
  ssize_t count;
  do
    {
      count = read (process.acknowledgement, &status, 1);
    }
  while (count < 0 && errno == EINTR);
  int saved_error = count < 0 ? errno : EIO;
  close (process.acknowledgement);
  process.acknowledgement = -1;
  int result = count == 1 && status == 0 ? 0 : -1;
  if (result == 0)
    {
      saved_error = 0;
    }
  for (int stream = 0; stream < 2; ++stream)
    {
      if (lseek (process.output[stream], 0, SEEK_SET) < 0)
	{
	  if (saved_error == 0)
	    {
	      saved_error = errno;
	    }
	  result = -1;
	  close (process.output[stream]);
	  process.output[stream] = -1;
	  continue;
	}
      char buffer[8192];
      while (true)
	{
	  do
	    {
	      count = read (process.output[stream], buffer, sizeof (buffer));
	    }
	  while (count < 0 && errno == EINTR);
	  if (count <= 0)
	    {
	      if (count < 0)
		{
		  if (saved_error == 0)
		    {
		      saved_error = errno;
		    }
		  result = -1;
		}
	      break;
	    }
	  size_t offset = 0;
	  while (offset < static_cast<size_t> (count))
	    {
	      ssize_t written = write (stream + 1, buffer + offset, count - offset);
	      if (written < 0 && errno == EINTR)
		{
		  continue;
		}
	      if (written <= 0)
		{
		  if (saved_error == 0)
		    {
		      saved_error = written < 0 ? errno : EIO;
		    }
		  result = -1;
		  break;
		}
	      offset += written;
	    }
	  if (offset != static_cast<size_t> (count))
	    {
	      break;
	    }
	}
      close (process.output[stream]);
      process.output[stream] = -1;
    }
  if (saved_error != 0)
    {
      errno = saved_error;
    }
  return result;
}
