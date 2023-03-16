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

#include "method_compile.hpp"

#include "jsp_comm.h"
#include "method_connection_sr.hpp"
#include "method_connection_java.hpp"
#include "byte_order.h"
#include "connection_support.h"

namespace cubmethod
{
  int invoke_compile (runtime_context &ctx, const std::string program, const bool &verbose, cubmem::extensible_block &blk)
  {
    connection *conn = ctx.get_connection_pool().claim();
    header header (DB_EMPTY_SESSION, SP_CODE_COMPILE, 0);

    SOCKET socket = conn->get_socket ();
    int error = mcon_send_data_to_java (socket, header, verbose, program);

    int nbytes = -1;

    int res_size = 0;
    nbytes = jsp_readn (socket, (char *) &res_size, OR_INT_SIZE);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }
    res_size = ntohl (res_size);

    blk.extend_to (res_size);

    nbytes = jsp_readn (socket, blk.get_ptr (), res_size);
    if (nbytes != res_size)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }

    ctx.get_connection_pool().retire (conn, true);

    return error;
  }
};