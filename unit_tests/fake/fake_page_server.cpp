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

//#include "fake_page_server.hpp"

cublog::replicator replicator_GL;
page_server ps_Gl;

namespace cublog
{
  void
  replicator::wait_past_target_lsa (const log_lsa &lsa)
  {
  }
}

cublog::replicator &
page_server::get_replicator ()
{
  return replicator_GL;
}
