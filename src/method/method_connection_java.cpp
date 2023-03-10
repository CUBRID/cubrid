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

#include "method_connection_java.hpp"

namespace cubmethod
{
  int
  mcon_send_buffer_to_java (SOCKET socket, cubmem::block &blk)
  {
    int error = NO_ERROR;
    OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
    char *request = OR_ALIGNED_BUF_START (a_request);

    int request_size = (int) blk.dim;
    or_pack_int (request, request_size);

    int nbytes = jsp_writen (socket, request, OR_INT_SIZE);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
	return error;
      }

    nbytes = jsp_writen (socket, blk.ptr, blk.dim);
    if (nbytes != (int) blk.dim)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
      }
    return error;
  }

  int mcon_read_data_from_java (const SOCKET socket, cubmem::extensible_block &b)
  {
    int res_size = 0;
    int nbytes = 0;

    nbytes = jsp_readn_with_timeout (socket, (char *) &res_size, OR_INT_SIZE, -1);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	return er_errid ();
      }

    res_size = ntohl (res_size);
    if (res_size > 0)
      {
	cubmem::extensible_block ext_blk;
	ext_blk.extend_to (res_size);

	nbytes = jsp_readn_with_timeout (socket, ext_blk.get_ptr (), res_size, -1);
	if (nbytes != res_size)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    nbytes);
	    return er_errid ();
	  }

	b = std::move (ext_blk);
      }
    return NO_ERROR;
  }

  int mcon_read_data_from_java (const SOCKET socket, cubmem::block &b)
  {
    int res_size = 0;
    int nbytes = 0;

    nbytes = jsp_readn_with_timeout (socket, (char *) &res_size, OR_INT_SIZE, -1);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	return er_errid ();
      }

    res_size = ntohl (res_size);
    if (res_size > 0)
      {
	cubmem::extensible_block ext_blk;
	ext_blk.extend_to (res_size);

	nbytes = jsp_readn_with_timeout (socket, ext_blk.get_ptr (), res_size, -1);
	if (nbytes != res_size)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    nbytes);
	    return er_errid ();
	  }

	cubmem::block blk (res_size, ext_blk.release_ptr());
	b = std::move (blk);
      }

    return NO_ERROR;
  }

  int mcon_read_data_from_java (const SOCKET socket, cubmem::block &b, const mcon_callback_func &interrupt_func)
  {
    int res_size = 0;
    int nbytes = 0;
    do
      {
	int status = interrupt_func ();
	if (status != NO_ERROR)
	  {
	    return status;
	  }

	nbytes = jsp_readn (socket, (char *) &res_size, OR_INT_SIZE);
	if (nbytes < 0 && errno == ETIMEDOUT)
	  {
	    continue;
	  }
	else if (nbytes != OR_INT_SIZE)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	    return er_errid ();
	  }
      }
    while (nbytes < 0 && errno == ETIMEDOUT);

    res_size = ntohl (res_size);
    if (res_size > 0)
      {
	do
	  {
	    int status = interrupt_func ();
	    if (status != NO_ERROR)
	      {
		return status;
	      }

	    cubmem::extensible_block ext_blk;
	    ext_blk.extend_to (res_size);

	    nbytes = jsp_readn (socket, ext_blk.get_ptr (), res_size);
	    if (nbytes < 0 && errno == ETIMEDOUT)
	      {
		continue;
	      }
	    else if (nbytes != res_size)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
			nbytes);
		return er_errid ();
	      }

	    cubmem::block blk (res_size, ext_blk.release_ptr());
	    b = std::move (blk);
	  }
	while (nbytes < 0 && errno == ETIMEDOUT);
      }

    return NO_ERROR;
  }


} // namespace cubmethod
