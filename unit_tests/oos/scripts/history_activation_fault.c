/*
 *
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


/* Linux syscall fault injection for the public activation utility. It knows
 * only the target file path, never the database header layout or marker value. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int
is_target (int fd)
{
  char link[64], path[4096];
  const char *target = getenv ("HISTORY_FAULT_TARGET");
  ssize_t len;
  if (target == NULL)
    {
      return 0;
    }
  snprintf (link, sizeof (link), "/proc/self/fd/%d", fd);
  len = readlink (link, path, sizeof (path) - 1);
  if (len < 0)
    {
      return 0;
    }
  path[len] = '\0';
  return strcmp (target, path) == 0;
}

static int
inject (const char *phase)
{
  const char *requested = getenv ("HISTORY_FAULT_PHASE");
  char pid[32];
  int fd, length;
  if (requested == NULL || strcmp (requested, phase) != 0)
    {
      return 0;
    }
  fd = open ("/mnt/work/fault.pid", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    {
      _exit (120);
    }
  length = snprintf (pid, sizeof (pid), "%ld\n", (long) getpid ());
  syscall (SYS_write, fd, pid, length);
  close (fd);
  if (strstr (phase, "error") != NULL)
    {
      errno = EIO;
      return 1;
    }
  raise (SIGSTOP);
  return 0;
}

ssize_t
write (int fd, const void *buffer, size_t count)
{
  ssize_t result;
  ssize_t (*next_write) (int, const void *, size_t) = dlsym (RTLD_NEXT, "write");
  int target = is_target (fd);
  if (target)
    {
      inject ("write-before");
      if (inject ("write-error"))
	{
	  return -1;
	}
    }
  result = next_write (fd, buffer, count);
  if (target && result > 0)
    {
      inject ("write-after");
    }
  return result;
}

int
fsync (int fd)
{
  int result;
  int (*next_fsync) (int) = dlsym (RTLD_NEXT, "fsync");
  int target = is_target (fd);
  if (target)
    {
      inject ("sync-before");
      if (inject ("sync-error"))
	{
	  return -1;
	}
    }
  result = next_fsync (fd);
  if (target && result == 0)
    {
      inject ("sync-after");
    }
  return result;
}
