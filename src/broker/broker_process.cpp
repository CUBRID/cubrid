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
#include "broker_process.hpp"
#if !defined(WINDOWS)
#include "background_process.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

extern char **environ;

struct broker_process_group::entry
{
  background_process process;
  background_process_output output;
  entry *next = nullptr;
};

// Build a private environment before fork. SOURCE_ENV retains its original
// order, and the per-process keys override it just as the old child putenv did.
static void
add_environment (std::vector<std::string> &values, const char *value)
{
  if (value == nullptr)
    {
      return;
    }
  const char *equal = strchr (value, '=');
  if (equal == nullptr)
    {
      return;
    }
  size_t key_size = equal - value + 1;
  for (auto &item : values)
    {
      if (item.compare (0, key_size, value, key_size) == 0)
	{
	  item = value;
	  return;
	}
    }
  values.emplace_back (value);
}

static int
start_process (const char *path, const char *name, char **source_env, int env_count,
	       const char *first, const char *second, const char *service, background_process *process, bool reset_sigchld)
{
  try
    {
      std::vector<std::string> values;
      for (char **env = environ; *env != nullptr; ++env)
	{
	  values.emplace_back (*env);
	}
      for (int i = 0; i < env_count; ++i)
	{
	  add_environment (values, source_env[i]);
	}
      add_environment (values, first);
      add_environment (values, second);
      std::vector<const char *> environment;
      for (auto &value : values)
	{
	  environment.push_back (value.c_str ());
	}
      environment.push_back (nullptr);
      const char *args[] = {name, nullptr};
      if (process == nullptr)
	{
	  return background_process_spawn_stdio (path, args, STDOUT_FILENO, STDERR_FILENO, environment.data (), true);
	}
      const char *root = getenv ("CUBRID");
      if (root == nullptr)
	{
	  errno = EINVAL;
	  return -1;
	}
      std::string relay = std::string (root) + "/bin/cub_console";
      std::string log = std::string (root) + "/log/" + service + "-console.log";
      if (background_process_start (path, args, relay.c_str (), log.c_str (), *process, environment.data (),
				    reset_sigchld) != 0)
	{
	  return -1;
	}
      return process->pid;
    }
  catch (...)
    {
      errno = ENOMEM;
      return -1;
    }
}

int
broker_process_group::start (const char *path, const char *name, char **env, int env_count,
			     const char *first, const char *second, const char *service, bool reset_sigchld, bool *exec_failed)
{
  if (exec_failed != nullptr)
    {
      *exec_failed = false;
    }
  entry *item;
  try
    {
      item = new entry;
    }
  catch (...)
    {
      m_error = -1;
      errno = ENOMEM;
      return -1;
    }
  int pid = start_process (path, name, env, env_count, first, second, service, &item->process, reset_sigchld);
  if (pid < 0)
    {
      if (exec_failed != nullptr)
	{
	  *exec_failed = item->process.exec_failed;
	}
      if (!item->process.exec_failed)
	{
	  m_error = -1;
	}
      int saved = errno;
      delete item;
      errno = saved;
      return -1;
    }
  item->next = m_first;
  m_first = item;
  return pid;
}

void
broker_process_group::wait (int milliseconds)
{
  struct timespec start, now;
  clock_gettime (CLOCK_MONOTONIC, &start);
  do
    {
      for (entry *item = m_first; item != nullptr; item = item->next)
	{
	  background_process_wait (item->process, 0, &item->output);
	}
      if (milliseconds == 0)
	{
	  break;
	}
      poll (nullptr, 0, milliseconds < 10 ? milliseconds : 10);
      clock_gettime (CLOCK_MONOTONIC, &now);
    }
  while ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 < milliseconds);
}

void
broker_process_pump (void *owner, int milliseconds)
{
  static_cast<broker_process_group *> (owner)->wait (milliseconds);
}

broker_process_group::~broker_process_group ()
{
  finish (0);
}

int
broker_process_group::finish (int result)
{
  if (m_error != 0)
    {
      result = -1;
    }
  m_error = 0;
  for (entry *item = m_first; item != nullptr; item = item->next)
    {
      if (item->process.control >= 0)
	{
	  close (item->process.control);
	}
      item->process.control = -1;
    }
  bool pending;
  do
    {
      wait (0);
      pending = false;
      for (entry *item = m_first; item != nullptr; item = item->next)
	{
	  pending |= item->process.output[0] >= 0 || item->process.output[1] >= 0;
	}
      if (pending)
	{
	  poll (nullptr, 0, 10);
	}
    }
  while (pending);
  while (m_first != nullptr)
    {
      entry *item = m_first;
      m_first = item->next;
      if (background_process_finish_start (item->process) != 0)
	{
	  perror ("broker console");
	  result = -1;
	}
      int status;
      while (waitpid (item->process.pid, &status, WNOHANG) < 0 && errno == EINTR) {}
      while (waitpid (item->process.relay_pid, &status, WNOHANG) < 0 && errno == EINTR) {}
      delete item;
    }
  return result;
}

int
broker_process_restart (const char *path, const char *name, const char *first, const char *second)
{
  return start_process (path, name, nullptr, 0, first, second, nullptr, nullptr, true);
}
#endif
