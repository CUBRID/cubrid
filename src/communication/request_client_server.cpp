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

#include "request_client_server.hpp"

#include "error_manager.h"
#include "system_parameter.h"

namespace cubcomm
{
  static void
  er_log_sendrecv_request (const char *sendrcv_str, const channel &chn, int msgid, size_t size)
  {
    if (prm_get_bool_value (PRM_ID_ER_LOG_COMM_REQUEST))
      {
	_er_log_debug (ARG_FILE_LINE, "[COMM REQUEST][%s] %s request with id = %d and message size = %d.\n",
		       chn.get_channel_id ().c_str (), sendrcv_str, msgid, size);
      }
  }

  void
  er_log_send_request (const channel &chn, int msgid, size_t size)
  {
    er_log_sendrecv_request ("Send", chn, msgid, size);
  }

  void
  er_log_recv_request (const channel &chn, int msgid, size_t size)
  {
    er_log_sendrecv_request ("Receive", chn, msgid, size);
  }

  static void
  er_log_sendrecv_fail (const char *sendrcv_str, const channel &chn, css_error_code err)
  {
    if (prm_get_bool_value (PRM_ID_ER_LOG_COMM_REQUEST))
      {
	_er_log_debug (ARG_FILE_LINE, "[COMM REQUEST][%s] %s failed with error %d.\n",
		       chn.get_channel_id ().c_str (), sendrcv_str, static_cast<int> (err));
      }
  }

  void
  er_log_send_fail (const channel &chn, css_error_code err)
  {
    return er_log_sendrecv_fail ("Send", chn, err);
  }

  void
  er_log_recv_fail (const channel &chn, css_error_code err)
  {
    return er_log_sendrecv_fail ("Receive", chn, err);
  }
}
