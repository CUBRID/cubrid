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
 * driver_session.cpp - adopted driver connection session thread (stage B1)
 *
 * Faithful translation of the CAS per-connection sequence (cas_common_main.c
 * main loop body + cas.c connect reply) onto a server thread.  The session
 * skeleton (thread-manager ritual, csc bracket, socketless conn entry, boot,
 * session adoption, teardown order) is inherited verbatim from the tracer's
 * in_process_session (server_compile_tracer.cpp, stage S0-A8 harness).
 *
 * B1 scope: connect handshake (reply with the server-issued cancel token in
 * the CAS pid slot) and connection teardown.  The request loop answers every
 * function code with CAS_ER_NOT_IMPLEMENTED and closes — the CAS function
 * dispatch lands with the b1-cas-speaker PR (B1-D9).
 */

#if defined (SERVER_MODE)

#include "driver_session.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstring>

#include "cas_error.h"
#include "cas_protocol.h"
#include "client_session_context.hpp"
#include "connection_defs.h"
#include "connection_sr.h"
#include "db.h"
#include "db_client_type.hpp"
#include "error_manager.h"
#include "network_interface_cl.h"	// boot_unregister_client
#include "session.h"		// session_adopt_client_context
#include "storage_common.h"	// NULL_TRAN_INDEX
#include "system_parameter.h"	// PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "transaction_cl.h"	// tm_Tran_index

extern thread_local unsigned int db_on_server;	/* network_interface_sr.cpp */

namespace cubconn
{
  namespace adoption
  {
    /* the adoption protocol mirrors these cas_protocol.h facts; a driver
     * dialect change must be a deliberate protocol bump, not a drift */
    static_assert (DRIVER_HEADER_SIZE == SRV_CON_CLIENT_INFO_SIZE, "driver header size drift");
    static_assert (DRIVER_DB_INFO_SIZE == SRV_CON_DB_INFO_SIZE, "db_info size drift");
    static_assert (DRIVER_BROKER_INFO_SIZE == BROKER_INFO_SIZE, "broker_info size drift");

    static const std::size_t CONNECT_REPLY_BUF_SIZE =
	    sizeof (int) + CAS_INFO_SIZE + CAS_CONNECTION_REPLY_SIZE;
    static_assert (CAS_CONNECTION_REPLY_SIZE == 36, "V4 connect reply layout drift");

    /* request bodies larger than this are a protocol violation, not a query */
    static const std::uint32_t REQUEST_BODY_MAX = 16 * 1024 * 1024;

    /* ------------------------------------------------------------------ */
    /* blocking wire helpers (the session thread owns the fd)             */
    /* ------------------------------------------------------------------ */

    static int
    write_full (int fd, const void *buf, std::size_t len)
    {
      const char *p = static_cast<const char *> (buf);
      while (len > 0)
	{
	  ssize_t n = send (fd, p, len, MSG_NOSIGNAL);
	  if (n < 0)
	    {
	      if (errno == EINTR)
		{
		  continue;
		}
	      return ER_FAILED;
	    }
	  p += n;
	  len -= (std::size_t) n;
	}
      return NO_ERROR;
    }

    static int
    read_full (int fd, void *buf, std::size_t len)
    {
      char *p = static_cast<char *> (buf);
      while (len > 0)
	{
	  ssize_t n = recv (fd, p, len, 0);
	  if (n == 0)
	    {
	      return ER_FAILED;	/* peer closed */
	    }
	  if (n < 0)
	    {
	      if (errno == EINTR)
		{
		  continue;
		}
	      return ER_FAILED;
	    }
	  p += n;
	  len -= (std::size_t) n;
	}
      return NO_ERROR;
    }

    /* ------------------------------------------------------------------ */
    /* pure helpers (unit-tested)                                         */
    /* ------------------------------------------------------------------ */

    static void
    copy_field (char *dst, std::size_t dst_size, const char *src, std::size_t src_size)
    {
      /* the driver is not trusted to NUL-terminate (default stance) */
      std::size_t n = strnlen (src, src_size);
      assert (dst_size > src_size);
      std::memcpy (dst, src, n);
      dst[n] = '\0';
    }

    int
    parse_db_info (const char *buf, std::size_t len, driver_conn_info &out)
    {
      if (buf == NULL || len != DRIVER_DB_INFO_SIZE)
	{
	  return ER_FAILED;
	}

      /* layout per cas_parse_db_info (cas_common_main.c): dbname[32],
       * user[32], passwd[32], url[512], session_id[20] */
      const char *p = buf;
      copy_field (out.db_name, sizeof (out.db_name), p, SRV_CON_DBNAME_SIZE);
      p += SRV_CON_DBNAME_SIZE;
      copy_field (out.db_user, sizeof (out.db_user), p, SRV_CON_DBUSER_SIZE);
      p += SRV_CON_DBUSER_SIZE;
      copy_field (out.db_passwd, sizeof (out.db_passwd), p, SRV_CON_DBPASSWD_SIZE);
      p += SRV_CON_DBPASSWD_SIZE;
      copy_field (out.url, sizeof (out.url), p, SRV_CON_URL_SIZE);
      p += SRV_CON_URL_SIZE;
      std::memcpy (out.session_id, p, SRV_CON_DBSESS_ID_SIZE);

      if (out.db_user[0] == '\0')
	{
	  std::strcpy (out.db_user, "PUBLIC");
	}
      out.is_health_check = (std::strcmp (out.db_name, HEALTH_CHECK_DUMMY_DB) == 0);
      return NO_ERROR;
    }

    int
    parse_driver_protocol (const char (&driver_header)[DRIVER_HEADER_SIZE])
    {
      unsigned char ver = (unsigned char) driver_header[SRV_CON_MSG_IDX_PROTO_VERSION];
      if ((ver & CAS_PROTO_INDICATOR) == 0)
	{
	  /* pre-9.0 major/minor/patch dialect — not carried over (#116 D3) */
	  return -1;
	}
      return (int) (ver & CAS_PROTO_VER_MASK);
    }

    std::size_t
    build_connect_reply (std::uint32_t token, std::int32_t slot_idx,
			 const char (&broker_info)[DRIVER_BROKER_INFO_SIZE],
			 const char *session_20b, char *out, std::size_t out_size)
    {
      /* cas_send_connect_reply_to_driver, V4+ layout only (V12-single wire):
       * len | cas_info | token (pid slot, #117 D4) | broker_info |
       * slot_idx + 1 (shm_as_index + 1 slot) | 20-byte driver session */
      if (out_size < CONNECT_REPLY_BUF_SIZE)
	{
	  assert (false);
	  return 0;
	}

      char *p = out;
      int v = htonl (CAS_CONNECTION_REPLY_SIZE);
      std::memcpy (p, &v, sizeof (int));
      p += sizeof (int);

      char cas_info[CAS_INFO_SIZE] =
	{ CAS_INFO_STATUS_ACTIVE, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT };
      std::memcpy (p, cas_info, CAS_INFO_SIZE);
      p += CAS_INFO_SIZE;

      v = htonl (token);
      std::memcpy (p, &v, CAS_PID_SIZE);
      p += CAS_PID_SIZE;
      std::memcpy (p, broker_info, DRIVER_BROKER_INFO_SIZE);
      p += DRIVER_BROKER_INFO_SIZE;
      v = htonl (slot_idx + 1);
      std::memcpy (p, &v, CAS_PID_SIZE);
      p += CAS_PID_SIZE;
      std::memcpy (p, session_20b, DRIVER_SESSION_SIZE);
      p += DRIVER_SESSION_SIZE;

      return (std::size_t) (p - out);
    }

    /* ------------------------------------------------------------------ */
    /* driver-facing replies                                              */
    /* ------------------------------------------------------------------ */

    /* net_write_error translation, V12-single: length-prefixed
     * cas_info | indicator | code | message.  No pre-V2 code conversion —
     * every V12 driver understands renewed error codes. */
    static void
    send_error_reply (int fd, char status, int indicator, int code, const char *msg)
    {
      assert (code != NO_ERROR);

      std::size_t msg_len = (msg != NULL && msg[0] != '\0') ? std::strlen (msg) + 1 : 0;
      int len = htonl ((int) (2 * sizeof (int) + msg_len));
      char cas_info[CAS_INFO_SIZE] =
	{ status, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT };

      if (write_full (fd, &len, sizeof (int)) != NO_ERROR || write_full (fd, cas_info, CAS_INFO_SIZE) != NO_ERROR)
	{
	  return;
	}
      int v = htonl (indicator);
      if (write_full (fd, &v, sizeof (int)) != NO_ERROR)
	{
	  return;
	}
      v = htonl (code);
      if (write_full (fd, &v, sizeof (int)) != NO_ERROR)
	{
	  return;
	}
      if (msg_len > 0)
	{
	  (void) write_full (fd, msg, msg_len);
	}
    }

    /* cas_set_session_id translation: hand the driver-supplied session key
     * to the client half before boot so the server session is reattached
     * (or a fresh one created when the key is empty/stale) */
    static void
    apply_driver_session_id (const char (&session_20b)[20])
    {
      SESSION_ID id;
      std::memcpy (&id, session_20b + 8, sizeof (SESSION_ID));
      id = ntohl (id);
      db_set_server_session_key (session_20b);
      db_set_session_id (id);
    }

    /* cas_make_session_for_driver translation */
    static void
    make_session_for_driver (char *out)
    {
      std::size_t size = 0;
      SESSION_ID session;

      std::memcpy (out + size, db_get_server_session_key (), SERVER_SESSION_KEY_SIZE);
      size += SERVER_SESSION_KEY_SIZE;
      session = db_get_session_id ();
      session = htonl (session);
      std::memcpy (out + size, &session, sizeof (SESSION_ID));
      size += sizeof (SESSION_ID);
      std::memset (out + size, 0, DRIVER_SESSION_SIZE - size);
    }

    /* ------------------------------------------------------------------ */
    /* request loop (B1 skeleton)                                         */
    /* ------------------------------------------------------------------ */

    static void
    request_loop (int fd)
    {
      for (;;)
	{
	  /* MSG_HEADER: 4-byte body length + CAS_INFO_SIZE info bytes */
	  char header[sizeof (int) + CAS_INFO_SIZE];
	  if (read_full (fd, header, sizeof (header)) != NO_ERROR)
	    {
	      return;		/* disconnect */
	    }
	  int body_size;
	  std::memcpy (&body_size, header, sizeof (int));
	  body_size = ntohl (body_size);
	  if (body_size <= 0 || (std::uint32_t) body_size > REQUEST_BODY_MAX)
	    {
	      send_error_reply (fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
	      return;
	    }

	  char *body = new (std::nothrow) char[body_size];
	  if (body == NULL)
	    {
	      send_error_reply (fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_NO_MORE_MEMORY, NULL);
	      return;
	    }
	  int rc = read_full (fd, body, (std::size_t) body_size);
	  delete[] body;
	  if (rc != NO_ERROR)
	    {
	      return;
	    }

	  /* function dispatch lands with the b1-cas-speaker PR; until then
	   * every request is answered and the connection closed */
	  send_error_reply (fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_NOT_IMPLEMENTED, NULL);
	  return;
	}
    }

    /* ------------------------------------------------------------------ */
    /* the session thread                                                 */
    /* ------------------------------------------------------------------ */

    void
    driver_session_run (session_params params)
    {
      int err;
      bool registered = false;
      bool adopted = false;
      CSS_CONN_ENTRY *conn = NULL;
      client_session_context *ctx = NULL;
      driver_conn_info info;
      char session_blob[DRIVER_SESSION_SIZE];
      char reply[CONNECT_REPLY_BUF_SIZE];
      std::size_t reply_size;

      /* register this foreign thread with the thread manager (same ritual as
       * the tracer / connection_worker.cpp) */
      cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();
      if (entry_p == NULL)
	{
	  close (params.client_fd);
	  registry_session_finished (params.token);
	  return;
	}
      entry_p->register_id ();
      entry_p->type = TT_SERVER;
      entry_p->tran_index = -1;
      entry_p->m_status = cubthread::entry::status::TS_RUN;
      entry_p->shutdown = false;
      entry_p->get_error_context ().register_thread_local ();

      /* session-scoped client context; adopted by the server session after
       * registration, freed with it (session_state_uninit) */
      ctx = new client_session_context ();
      csc_activate (ctx);

      /* socketless conn entry: the CAS wire lives on params.client_fd, the
       * in-process client half anchors on thread_p->conn_entry (tracer S0) */
      conn = css_make_conn (INVALID_SOCKET);
      if (conn == NULL)
	{
	  send_error_reply (params.client_fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_FREE_SERVER, NULL);
	  goto retire;
	}
      /* pairs with the adoption channel's css_increment_num_conn: css_free_conn
       * decrements by conn->client_type on every exit path */
      conn->client_type = DB_CLIENT_TYPE_DEFAULT;
      css_insert_into_active_conn_list (conn);
      entry_p->conn_entry = conn;

      /* this thread now acts as the in-process client (D5) */
      db_on_server = 0;

      if (parse_db_info (params.db_info, sizeof (params.db_info), info) != NO_ERROR || info.is_health_check)
	{
	  /* the broker absorbs health checks and validates db_info; reaching
	   * here means a malformed handoff */
	  send_error_reply (params.client_fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION,
			    NULL);
	  goto retire;
	}

      if (parse_driver_protocol (params.driver_header) < PROTOCOL_V12)
	{
	  /* V12-single wire (#116 D3): older dialects are not spoken here */
	  send_error_reply (params.client_fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_NOT_IMPLEMENTED,
			    "this server accepts protocol V12 or later drivers");
	  goto retire;
	}

      /* the broker routed by dbname; a mismatch is its bug, not the driver's */
      assert (params.server_name == info.db_name);

      apply_driver_session_id (info.session_id);

      /* client-half boot with the driver's credentials; serialization is the
       * engine's own (boot_restart_client) since A5 */
      err = db_restart_ex ("driver_session", info.db_name, info.db_user, info.db_passwd, NULL, DB_CLIENT_TYPE_DEFAULT);
      if (err != NO_ERROR)
	{
	  /* cas_db_connect failure path: DBMS error straight to the driver */
	  send_error_reply (params.client_fd, CAS_INFO_STATUS_INACTIVE, DBMS_ERROR_INDICATOR, err,
			    er_msg () != NULL ? er_msg () : "");
	  goto retire;
	}
      registered = true;

      /* the session created during registration becomes the durable owner of
       * the client context (#123 D3) */
      if (session_adopt_client_context (entry_p, ctx) == NO_ERROR)
	{
	  adopted = true;
	}
      else
	{
	  send_error_reply (params.client_fd, CAS_INFO_STATUS_INACTIVE, CAS_ERROR_INDICATOR, CAS_ER_FREE_SERVER, NULL);
	  goto retire;
	}

      /* cancel arrives on the control channel as a tran interrupt (#117 D4) */
      registry_set_tran_index (params.token, tm_Tran_index);

      /* the broker filled its own connect-reply facts (bytes 0-3:
       * dbms/keep_con/statement pooling/pconnect); the server owns the
       * protocol bytes (cas_bi_make_broker_info split, B1-D5) */
      params.broker_info[BROKER_INFO_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
      params.broker_info[BROKER_INFO_FUNCTION_FLAG] = (char) (BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT);
      params.broker_info[BROKER_INFO_SYSTEM_PARAM] =
	      prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR) ? MASK_ORACLE_COMPAT_NUMBER_BEHAVIOR : 0;
      params.broker_info[BROKER_INFO_RESERVED3] = 0;

      make_session_for_driver (session_blob);
      reply_size = build_connect_reply (params.token, params.slot_idx, params.broker_info, session_blob,
					reply, sizeof (reply));
      if (write_full (params.client_fd, reply, reply_size) != NO_ERROR)
	{
	  goto retire;
	}

      request_loop (params.client_fd);

    retire:
      /* teardown order inherited from the tracer (S0): unregister the tran,
       * scrub the conn entry, close the bracket, then retire the thread */
      if (registered)
	{
	  (void) boot_unregister_client (tm_Tran_index);
	  tm_Tran_index = NULL_TRAN_INDEX;
	}
      if (conn != NULL)
	{
	  entry_p->conn_entry = NULL;
	  css_free_conn (conn);	/* decrements the conn-rule counter by client_type */
	}
      else
	{
	  /* never got a conn entry: pair the adoption channel's increment here */
	  css_decrement_num_conn (DB_CLIENT_TYPE_DEFAULT);
	}
      entry_p->tran_index = NULL_TRAN_INDEX;
      entry_p->m_status = cubthread::entry::status::TS_DEAD;
      csc_deactivate ();
      if (!adopted)
	{
	  csc_retire_and_delete (ctx);
	}
      entry_p->get_error_context ().deregister_thread_local ();
      entry_p->unregister_id ();
      cubthread::get_manager ()->retire_entry (*entry_p);

      close (params.client_fd);
      registry_session_finished (params.token);
    }
  }				/* namespace adoption */
}				/* namespace cubconn */

#endif /* SERVER_MODE */
