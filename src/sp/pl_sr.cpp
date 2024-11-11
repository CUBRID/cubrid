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

/*
 * pl_sr.cpp - PL Server Module Source
 */

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "boot_sr.h"
#endif

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <mutex>

#include "thread_manager.hpp"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_daemon.hpp"
#endif

#include "process_util.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// forward declaration
struct pl_monitor_entrty;

static void pl_entry_init (const char *name);
static int pl_check_status (pl_monitor_entrty *entry);

#if defined (SERVER_MODE)
static void pl_monitor (cubthread::entry &thread_ref);
#else
static void pl_monitor (void);
#endif
struct pl_monitor_entrty
{
  pl_monitor_entrty (const char *db)
    : binary_name {"cub_pl"}
    , db_name {db}
    , pid (-1)
    , hang {false}
    , lock {}
  {
    (void) envvar_bindir_file (executable_path, PATH_MAX, binary_name);
  }

  const char *binary_name;
  const char *db_name;
  int pid;
  bool hang;
  std::mutex lock;

  char executable_path[PATH_MAX];
};

static pl_monitor_entrty *pl_entry = NULL;
#if defined (SERVER_MODE)
static cubthread::daemon *pl_monitor_helper_daemon = NULL;
#endif

static void
pl_entry_init (const char *name)
{
  if (pl_entry == NULL)
    {
      pl_entry = new pl_monitor_entrty (name);
    }
}

/*
 * pl_check_status() - test if the status of pl server
 *   return: 0 if the pl server is running, otherwise 1
 *   pid(in): process id
 */
static int
pl_check_status (pl_monitor_entrty *entry)
{
  int status = 1; // stopped
  if (entry->pid > 0)
    {
      if (!is_terminated_process (entry->pid))
	{
	  // TODO [PL/CSQL]: hang check
	  // If process is running but ping command through UDS (TCP) does not respond, then it is considered as hang
	  /*
	  if (is_hang)
	  {
	    status = 2;
	  }
	  */
	  status = 0;
	}
    }
  return status;
}

static void
#if defined (SERVER_MODE)
pl_monitor (cubthread::entry &thread_ref)
#else
pl_monitor (void)
#endif
{
  int pid;
#if defined (SERVER_MODE)
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }
#endif

  if (pl_check_status (pl_entry) != 0)
    {
      int status;
      const char *argv[] = {pl_entry->binary_name, pl_entry->db_name, 0};
      pid = create_child_process (pl_entry->executable_path, argv, 0 /* do not wait */, NULL, NULL, NULL, &status);
      if (pid <= 0)
	{
	  // do nothing
	}
      else // parent
	{
	  pl_entry->pid = pid;
	  pl_entry->hang = false;
	}
    }
}

void
pl_monitor_init (const char *name)
{
  pl_entry_init (name);

#if defined (SERVER_MODE)
  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (1000));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (std::bind (pl_monitor,
      std::placeholders::_1));

  pl_monitor_helper_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "pl_monitor");
#else
  pl_monitor ();
#endif
}

/*
 * pl_monitor_destroy () - destroy pl monitor daemon threads
 */
void
pl_monitor_destroy ()
{
#if defined (SERVER_MODE)
  if (pl_monitor_helper_daemon)
    {
      cubthread::get_manager ()->destroy_daemon (pl_monitor_helper_daemon);
    }
#endif
  if (pl_entry != NULL)
    {
      delete pl_entry;
    }
}
