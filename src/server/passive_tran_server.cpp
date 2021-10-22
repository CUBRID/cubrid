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

#include "log_impl.h"
#include "system_parameter.h"

passive_tran_server pts_Gl;

bool
passive_tran_server::uses_remote_storage () const
{
  return m_uses_remote_storage;
}

bool
passive_tran_server::get_remote_storage_config ()
{
  m_uses_remote_storage = prm_get_bool_value (PRM_ID_REMOTE_STORAGE);
  return m_uses_remote_storage;
}

void
passive_tran_server::receive_saved_lsa (cubpacking::unpacker &upk)
{
  std::string message;
  log_lsa saved_lsa;

  upk.unpack_string (message);
  assert (sizeof (log_lsa) == message.size ());
  std::memcpy (&saved_lsa, message.c_str (), sizeof (log_lsa));

  if (log_Gl.m_max_ps_flushed_lsa < saved_lsa)
    {
      log_Gl.update_max_ps_flushed_lsa (saved_lsa);
    }

  if (prm_get_bool_value (PRM_ID_ER_LOG_COMMIT_CONFIRM))
    {
      _er_log_debug (ARG_FILE_LINE, "[COMMIT CONFIRM] Received LSA = %lld|%d.\n", LSA_AS_ARGS (&saved_lsa));
    }
}
