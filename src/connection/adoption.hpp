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
/*
 * adoption.hpp - broker->server connection adoption endpoint (stage B1, #117)
 *
 * The server listens on a per-database UNIX domain socket.  A broker keeps one
 * persistent control channel per database on it and multiplexes fd handoffs
 * (SCM_RIGHTS), cancel/status forwarding, session-end notifications and the
 * restart re-sync handshake over that channel (#117 D1/D3/D4/D7).
 *
 * The wire structs below are the control-channel protocol v1.  Both ends run
 * on the same host over AF_UNIX, so fields are host byte order.
 */

#ifndef _ADOPTION_HPP_
#define _ADOPTION_HPP_

#if !defined (SERVER_MODE) && !defined (ADOPTION_PROTOCOL_ONLY)
#error adoption.hpp is a SERVER_MODE header (brokers include it with ADOPTION_PROTOCOL_ONLY)
#endif

#include <cstddef>
#include <cstdint>

namespace cubconn
{
  namespace adoption
  {
    /* ------------------------------------------------------------------ */
    /* control-channel protocol v1                                        */
    /* ------------------------------------------------------------------ */

    static const std::uint32_t PROTO_MAGIC = 0x41444F50;	/* "ADOP" */
    static const std::uint32_t PROTO_VERSION = 1;

    enum class msg_op : std::uint32_t
    {
      /* broker -> server */
      HELLO = 1,		/* open a control channel; body: hello_body */
      HANDOFF = 2,		/* adopt a driver connection; body: handoff_body + SCM_RIGHTS fd */
      CANCEL = 3,		/* query cancel; body: token_body */
      STATUS = 4,		/* "ST" status probe; body: token_body */
      RESYNC = 5,		/* restart re-sync; no body */

      /* local client -> server (wf122/B5 D1: broker-independent thin csql) */
      DIRECT_CONNECT = 6,	/* this very fd becomes the driver connection;
				 * body: direct_connect_body; same-uid only
				 * (SO_PEERCRED).  Success answers with the CAS
				 * connect reply on this fd; failure with a
				 * HANDOFF_REJECT frame. */

      /* server -> broker */
      HELLO_ACK = 9,		/* body: hello_ack_body */
      HANDOFF_ACK = 10,		/* body: token_body (server-issued cancel token) */
      HANDOFF_REJECT = 11,	/* body: reject_body */
      STATUS_REPLY = 12,	/* body: status_reply_body */
      RESYNC_REPLY = 13,	/* body: resync_reply_body */
      SESSION_END = 14		/* async; body: token_body (frees a broker slot, #117 D3) */
    };

    struct msg_header
    {
      std::uint32_t magic;
      std::uint32_t op;
      std::uint32_t length;	/* payload bytes following this header */
    };

    static const std::size_t BROKER_NAME_MAX = 32;

    struct hello_body
    {
      std::uint32_t proto_version;
      char broker_name[BROKER_NAME_MAX];	/* NUL-terminated */
    };

    struct hello_ack_body
    {
      std::uint32_t proto_version;
    };

    /* driver-facing sizes; kept in sync with cas_protocol.h by static_asserts
     * in adoption.cpp (this header must stay includable from the broker) */
    static const std::size_t DRIVER_HEADER_SIZE = 10;	/* SRV_CON_CLIENT_INFO_SIZE */
    static const std::size_t DRIVER_DB_INFO_SIZE = 628;	/* SRV_CON_DB_INFO_SIZE (V12) */
    static const std::size_t DRIVER_BROKER_INFO_SIZE = 8;	/* BROKER_INFO_SIZE */

    struct handoff_body
    {
      std::uint32_t client_ip;	/* network byte order, as read from the peer */
      std::uint16_t client_port;
      std::uint8_t access_mode;	/* broker ACCESS_MODE: RW/RO/SO (#121 D1) */
      std::uint8_t replica_only;	/* broker REPLICA_ONLY flag (#121 D1) */
      std::int32_t slot_idx;	/* broker slot index; echoed in the connect reply */
      char broker_info[DRIVER_BROKER_INFO_SIZE];	/* echoed in the connect reply */
      char driver_header[DRIVER_HEADER_SIZE];	/* the peeked 10-byte client header */
      char db_info[DRIVER_DB_INFO_SIZE];	/* the peeked db_info packet */
      char pad[2];
    };

    struct token_body
    {
      std::uint32_t token;
    };

    /* DIRECT_CONNECT payload (wf122/B5 D1/D2): a local csql declares its
     * DB_CLIENT_TYPE (csql family only — the server validates) and sends the
     * same two driver packets the broker would have peeked. */
    struct direct_connect_body
    {
      std::uint8_t client_type;	/* DB_CLIENT_TYPE, csql family only */
      std::uint8_t pad[3];
      char driver_header[DRIVER_HEADER_SIZE];
      char db_info[DRIVER_DB_INFO_SIZE];
    };

    enum class reject_reason : std::int32_t
    {
      CLIENTS_EXCEEDED = 1,	/* css_Conn_rules refused; broker requeues (#117 D3) */
      DBNAME_MISMATCH = 2,	/* routed to the wrong server */
      SHUTDOWN = 3,		/* server is going down */
      MALFORMED = 4,		/* bad handoff payload */
      UNSUPPORTED_DRIVER = 5,	/* protocol below V12 (#116 D3) */
      NOT_AUTHORIZED = 6	/* DIRECT_CONNECT peer/type refused (wf122/B5) */
    };

    struct reject_body
    {
      std::int32_t reason;
    };

    struct status_reply_body
    {
      std::int32_t fn_status;
    };

    struct resync_reply_body
    {
      std::uint32_t live_count;	/* adopted sessions of the requesting broker */
    };

    /* live_count of these follow resync_reply_body in the same reply (B4
     * codex F2): the surviving sessions' tokens, so a restarted broker can
     * rebuild its token table — without them every survivor's SESSION_END
     * lands as an unknown token and its slot leaks until the next restart.
     * client_port is not tracked server-side; rebuilt entries carry port 0
     * (the cancel disambiguator then requires the client ip to match). */
    struct resync_token_body
    {
      std::uint32_t token;
      std::uint32_t client_ip;	/* raw bytes as the handoff carried them */
    };

#if defined (SERVER_MODE)
    /* ------------------------------------------------------------------ */
    /* server-side endpoint lifecycle (called from css_init)              */
    /* ------------------------------------------------------------------ */

    /* create <TMP>/<prefix>_adopt_<db_name>, start the accept thread */
    int start (const char *db_name);
    /* close everything: channels, adopted sessions, listen socket */
    void stop (void);

    /* adopted-session registry hooks (driver_session.cpp) */
    void registry_set_tran_index (std::uint32_t token, int tran_index);

    /* SHOW SESSION STATUS (B2-D10): a copied row per live adopted session */
    struct session_stat_row
    {
      std::uint32_t token;
      int slot;
      char broker_name[BROKER_NAME_MAX];
      std::uint32_t client_ip;	/* network byte order */
      char db_user[32 + 1];
      unsigned int session_id;
      int tran_index;
      long long num_requests;
      long long num_transactions;
      long long num_queries;
      long long num_selects;
      long long num_inserts;
      long long num_updates;
      long long num_deletes;
      long long num_errors;
      long long num_long_queries;
      long long num_long_transactions;
      char last_activity[256];
    };
    /* the session thread publishes its CAS slot after slot begin; cleared
     * with the registry entry.  as_info_slot is a T_APPL_SERVER_INFO*. */
    void registry_set_session_stats (std::uint32_t token, void *as_info_slot, int slot_index,
				     std::uint32_t client_ip);
    std::size_t registry_stats_snapshot (session_stat_row *rows, std::size_t max_rows);
    void registry_set_fn_status (std::uint32_t token, int fn_status);
    /* session thread signs off: notify SESSION_END and drop the entry */
    void registry_session_finished (std::uint32_t token);
#endif
  }				/* namespace adoption */
}				/* namespace cubconn */

#endif /* _ADOPTION_HPP_ */
