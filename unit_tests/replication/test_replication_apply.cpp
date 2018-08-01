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
#include <thread>

namespace test_replication_apply
{

  void start_server_func (void)
    {
      net_server_start ("basic");
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
    locator_repl_start_tran (&thread_ref);
    locator_repl_apply_sbr (&thread_ref, m_statement.c_str ());
    locator_repl_end_tran (&thread_ref, true);
  }

private:
  std::string m_statement;
};


  int test_apply_sbr (void)
  {
    int res = NO_ERROR;

    std::thread server_thread_start (start_server_func);

    sleep (10);

    cubthread::entry thread_arg;

    css_push_external_task (thread_arg, NULL, new test_sbr_task ("CREATE TABLE tt(i1 string);"));


    server_thread_start.join ();

    return res;
  }

 


 

}
