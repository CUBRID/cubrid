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

#include "test_replication_apply.hpp"
#include "locator_sr.h"
#include "network.h"
#include "server_support.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include <thread>

namespace test_replication_apply
{

  const char *test_db_name = "test_basic";

  void start_server_func (void)
  {
    net_server_start (test_db_name);
  }

  class test_sbr_task : public cubthread::entry_task
  {
    public:
      test_sbr_task (const char *statement)
	: m_statement (statement)
      {
      }

      void
      execute (context_type &thread_ref)
      {
	int error = NO_ERROR;
	error = locator_repl_start_tran (&thread_ref);
	if (error != NO_ERROR)
	  {
	    return;
	  }
	error = locator_repl_apply_sbr (&thread_ref, NULL, NULL, m_statement.c_str ());

	locator_repl_end_tran (&thread_ref, (error == NO_ERROR) ? true : false);
      }

    private:
      std::string m_statement;
  };

  class test_sbr_task_abort : public cubthread::entry_task
  {
    public:
      test_sbr_task_abort (const char *statement)
	: m_statement (statement)
      {
      }

      void
      execute (context_type &thread_ref)
      {
	int error = NO_ERROR;
	error = locator_repl_start_tran (&thread_ref);
	if (error != NO_ERROR)
	  {
	    return;
	  }
	error = locator_repl_apply_sbr (&thread_ref, NULL, NULL, m_statement.c_str ());

	locator_repl_end_tran (&thread_ref, false);
      }

    private:
      std::string m_statement;
  };

  int test_apply_sbr_internal (double wait_msec_before_shutdown)
  {
    int res = NO_ERROR;
    char command[PATH_MAX];
    char  *cubrid_databases_env_var;


    strcpy (command, "cubrid service stop");
    res = system (command);
    assert (res != -1);

    cubrid_databases_env_var = getenv ("CUBRID_DATABASES");
    assert (cubrid_databases_env_var != NULL);
    res = chdir (cubrid_databases_env_var);
    assert (res == 0);

    strcpy (command, "cubrid createdb -r ");
    strcat (command, test_db_name);
    strcat (command, " en_US");
    res = system (command);
    assert (res != -1);

    /* to start cub_master process : */
    strcpy (command, "cubrid service start");
    res = system (command);
    assert (res != -1);

    /* previous command may have started server, stop it */
    strcpy (command, "cubrid server stop ");
    strcat (command, test_db_name);
    res = system (command);
    assert (res != -1);

    std::thread server_thread_start (start_server_func);

    /* wait for server startup */
    thread_sleep (10 * 1000);

    cubthread::entry thread_arg;

    css_push_external_task (NULL, new test_sbr_task ("CREATE TABLE tt(i1 string);"));
    css_push_external_task (NULL, new test_sbr_task_abort ("CREATE TABLE tt2(i1 string);"));
    css_push_external_task (NULL, new test_sbr_task ("CREATE TABLE tt3(i1 string);"));

    thread_sleep (wait_msec_before_shutdown);

    /* to stop cub_master, which stops the cub_server loop */
    strcpy (command, "cubrid service stop");
    res = system (command);
    assert (res != -1);

    server_thread_start.join ();

    return res != -1;
  }


  int test_apply_sbr (void)
  {
    int res = test_apply_sbr_internal (1);

    return res;

  }



}
