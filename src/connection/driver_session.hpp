/*
 *  Copyright 2016 CUBRID Corporation
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/*
 * driver_session.hpp - adopted driver connection session thread (stage B1)
 *
 * One dedicated thread per adopted driver connection runs the CAS session
 * sequence in-process (B1-D1/D2): thread-manager ritual, client session
 * context bracket for the connection's lifetime, socketless conn entry, boot
 * with the credentials from the peeked db_info, session adoption, the CAS
 * connect reply (with the server-issued cancel token in the pid slot, #117
 * D4), then the request loop.  Faithful translation of cas_common_main.c's
 * per-connection sequence; the CAS process becomes a server thread.
 */

#ifndef _DRIVER_SESSION_HPP_
#define _DRIVER_SESSION_HPP_

#if !defined (SERVER_MODE)
#error driver_session.hpp is a SERVER_MODE header
#endif

#include "adoption.hpp"

#include <cstdint>
#include <string>

namespace cubconn
{
  namespace adoption
  {
    /* parsed db_info packet (cas_parse_db_info translation) */
    struct driver_conn_info
    {
      char db_name[33];
      char db_user[33];
      char db_passwd[33];
      char url[513];
      char session_id[20];
      bool is_health_check;
    };

    /* pure helpers, unit-testable without a live server */

    /* parse a V12 db_info packet (DRIVER_DB_INFO_SIZE bytes); returns
     * NO_ERROR or ER_FAILED on malformed input */
    int parse_db_info (const char *buf, std::size_t len, driver_conn_info &out);

    /* driver protocol version from the 10-byte header; returns -1 when the
     * header does not carry the V1+ protocol indicator (pre-9.0 dialects are
     * not carried over, #116 D3) */
    int parse_driver_protocol (const char (&driver_header)[DRIVER_HEADER_SIZE]);

    /* build the V4+ CAS connect reply (36 payload bytes + 4-byte length
     * prefix + 4-byte cas_info = 44 bytes total).  session_20b is the
     * DRIVER_SESSION_SIZE driver session blob.  Returns bytes written. */
    std::size_t build_connect_reply (std::uint32_t token, std::int32_t slot_idx,
				     const char (&broker_info)[DRIVER_BROKER_INFO_SIZE],
				     const char *session_20b, char *out, std::size_t out_size);

    /* everything a session thread needs from the handoff */
    struct session_params
    {
      int client_fd;
      std::uint32_t token;
      std::int32_t slot_idx;
      std::uint32_t client_ip;	/* network byte order */
      std::uint16_t client_port;
      char broker_info[DRIVER_BROKER_INFO_SIZE];
      char driver_header[DRIVER_HEADER_SIZE];
      char db_info[DRIVER_DB_INFO_SIZE];
      std::string server_name;	/* this server's database name */
    };

    /* session thread body; owns client_fd and signs off via
     * registry_session_finished (token) on every exit path */
    void driver_session_run (session_params params);
  }				/* namespace adoption */
}				/* namespace cubconn */

#endif /* _DRIVER_SESSION_HPP_ */
