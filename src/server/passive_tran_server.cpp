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

#include "passive_tran_server.hpp"

// non-owning "shadow" pointer of globally visible ps_Gl
passive_tran_server *pts_Gl = nullptr;

void
init_passive_tran_server_shadow_ptr (passive_tran_server *ptr)
{
  assert (pts_Gl == nullptr);
  assert (ptr != nullptr);

  pts_Gl = ptr;
}

void
reset_passive_tran_server_shadow_ptr ()
{
  assert (pts_Gl != nullptr);

  pts_Gl = nullptr;
}

bool
passive_tran_server::uses_remote_storage () const
{
  return true;
}

bool
passive_tran_server::get_remote_storage_config ()
{
  return true;
}

void
passive_tran_server::on_boot ()
{
  assert (is_passive_transaction_server ());
}
