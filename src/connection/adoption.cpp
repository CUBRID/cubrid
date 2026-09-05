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
 * adoption.cpp - broker->server connection adoption endpoint (stage B1, #117)
 *
 * The server end of the handoff contract: a per-database UNIX listen socket,
 * one thread per broker control channel, a token registry of adopted
 * sessions, and one detached session thread per adopted driver connection
 * (driver_session.cpp).  All of this is connection-setup cold path; the data
 * path is the session thread's own fd.
 */

#if defined (SERVER_MODE)

#include "adoption.hpp"
#include "driver_session.hpp"

#include <netinet/in.h>		/* htonl/INADDR_LOOPBACK (DIRECT_CONNECT, wf122/B5) */
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cas_dispatch.h"	// cas_server_speaker_boot_init
#include "cas_protocol.h"	// broker_info byte values for DIRECT_CONNECT (wf122/B5)
#include "boot.h"		// BOOT_CSQL_CLIENT_TYPE (wf122/B5 D2)
#include "connection_defs.h"
#include "connection_sr.h"	// css_increment_num_conn
#include "db_client_type.hpp"
#include "environment_variable.h"
#include "error_manager.h"
#include "log_impl.h"		// logtb_set_tran_index_interrupt
#include "porting.h"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn
{
  namespace adoption
  {
    /* fn_status vocabulary of the "ST" probe (cas_common.h); the broker
     * forwards the raw int to the driver */
    static const std::int32_t FN_STATUS_PROBE_NONE = -2;
    static const std::int32_t FN_STATUS_PROBE_BUSY = 1;

    struct channel
    {
      std::uint64_t id;
      int fd;			/* -1 once invalidated; guarded by send_mutex */
      char broker_name[BROKER_NAME_MAX];
      std::mutex send_mutex;	/* replies and async SESSION_ENDs interleave; also guards fd close */
      std::thread thread;	/* joinable: stop()/the accept reaper joins it */
      std::atomic<bool> dead { false };

      channel () : id (0), fd (-1)
      {
	broker_name[0] = '\0';
      }
    };

    struct session_entry
    {
      std::uint32_t token;
      std::uint64_t channel_id;
      char broker_name[BROKER_NAME_MAX];
      int client_fd;
      int tran_index;
      std::int32_t fn_status;
      /* SHOW SESSION STATUS (B2-D10): the session thread's CAS slot.  The
       * registry_mutex guarantees POINTER lifetime only (the entry is
       * dropped before the thread retires, so a live entry's slot storage
       * is valid); the fields themselves are mutated by the session thread
       * with no synchronization, so the snapshot is best-effort — counters
       * and strings may be stale or torn, which monitoring tolerates */
      T_APPL_SERVER_INFO *stats_slot = NULL;
      int slot_index = -1;
      std::uint32_t client_ip = 0;
      bool direct = false;	/* DIRECT_CONNECT session: no broker slot, no SESSION_END (wf122/B5) */
    };

    struct manager
    {
      std::string db_name;
      std::string socket_path;
      int listen_fd = -1;
      std::thread accept_thread;
      std::atomic<bool> stopping { false };

      std::mutex channels_mutex;
      std::unordered_map<std::uint64_t, std::shared_ptr<channel>> channels;
      std::uint64_t next_channel_id = 1;

      std::mutex registry_mutex;
      std::condition_variable registry_cv;	/* signaled when a session signs off */
      std::unordered_map<std::uint32_t, session_entry> registry;
      std::uint32_t next_token = 1;

      /* sign-off callbacks still touching this manager after their registry
       * erase; stop() must not free the manager under them (codex review F2) */
      std::atomic<int> active_finishers { 0 };
    };

    static manager *adoption_Manager = NULL;

    /* ------------------------------------------------------------------ */
    /* wire helpers                                                       */
    /* ------------------------------------------------------------------ */

    static int
    send_all (int fd, const void *buf, std::size_t len)
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

    /* one framed control message under the channel's send mutex */
    static int
    send_message (channel &ch, msg_op op, const void *body, std::size_t body_len)
    {
      msg_header header;
      header.magic = PROTO_MAGIC;
      header.op = (std::uint32_t) op;
      header.length = (std::uint32_t) body_len;

      std::lock_guard<std::mutex> guard (ch.send_mutex);
      if (ch.fd < 0)
	{
	  return ER_FAILED;	/* invalidated under this mutex; never write a reused fd */
	}
      if (send_all (ch.fd, &header, sizeof (header)) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (body_len > 0 && send_all (ch.fd, body, body_len) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      return NO_ERROR;
    }

    /* receive len bytes; SCM_RIGHTS fds may ride on any segment (the broker
     * sends header+body+fd in one sendmsg, but the stream may fragment) */
    static int
    recv_with_fd (int fd, void *buf, std::size_t len, int *received_fd)
    {
      char *p = static_cast<char *> (buf);
      while (len > 0)
	{
	  struct iovec iov;
	  iov.iov_base = p;
	  iov.iov_len = len;

	  char control[CMSG_SPACE (sizeof (int))];
	  struct msghdr msg;
	  std::memset (&msg, 0, sizeof (msg));
	  msg.msg_iov = &iov;
	  msg.msg_iovlen = 1;
	  msg.msg_control = control;
	  msg.msg_controllen = sizeof (control);

	  ssize_t n = recvmsg (fd, &msg, MSG_CMSG_CLOEXEC);
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

	  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL; cmsg = CMSG_NXTHDR (&msg, cmsg))
	    {
	      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS
		  && cmsg->cmsg_len >= CMSG_LEN (sizeof (int)))
		{
		  int new_fd;
		  std::memcpy (&new_fd, CMSG_DATA (cmsg), sizeof (int));
		  if (received_fd != NULL && *received_fd < 0)
		    {
		      *received_fd = new_fd;
		    }
		  else
		    {
		      close (new_fd);	/* unexpected extra fd: don't leak it */
		    }
		}
	    }

	  p += n;
	  len -= (std::size_t) n;
	}
      return NO_ERROR;
    }

    /* ------------------------------------------------------------------ */
    /* registry                                                           */
    /* ------------------------------------------------------------------ */

    void
    registry_set_tran_index (std::uint32_t token, int tran_index)
    {
      manager *m = adoption_Manager;
      if (m == NULL)
	{
	  return;
	}
      std::lock_guard<std::mutex> guard (m->registry_mutex);
      auto it = m->registry.find (token);
      if (it != m->registry.end ())
	{
	  it->second.tran_index = tran_index;
	}
    }

    void
    registry_set_session_stats (std::uint32_t token, void *as_info_slot, int slot_index, std::uint32_t client_ip)
    {
      manager *m = adoption_Manager;
      if (m == NULL)
	{
	  return;
	}
      std::lock_guard<std::mutex> guard (m->registry_mutex);
      auto it = m->registry.find (token);
      if (it != m->registry.end ())
	{
	  it->second.stats_slot = (T_APPL_SERVER_INFO *) as_info_slot;
	  it->second.slot_index = slot_index;
	  it->second.client_ip = client_ip;
	}
    }

    std::size_t
    registry_stats_snapshot (session_stat_row *rows, std::size_t max_rows)
    {
      manager *m = adoption_Manager;
      std::size_t n = 0;

      if (m == NULL || rows == NULL)
	{
	  return 0;
	}
      std::lock_guard<std::mutex> guard (m->registry_mutex);
      for (const auto &pair : m->registry)
	{
	  const session_entry &e = pair.second;
	  if (e.stats_slot == NULL || n >= max_rows)
	    {
	      continue;
	    }
	  session_stat_row &r = rows[n++];
	  std::memset (&r, 0, sizeof (r));
	  r.token = e.token;
	  r.slot = e.slot_index;
	  std::memcpy (r.broker_name, e.broker_name, sizeof (r.broker_name));
	  r.broker_name[sizeof (r.broker_name) - 1] = '\0';
	  r.client_ip = e.client_ip;
	  std::strncpy (r.db_user, e.stats_slot->database_user, sizeof (r.db_user) - 1);
	  r.db_user[sizeof (r.db_user) - 1] = '\0';
	  r.session_id = e.stats_slot->session_id;
	  r.tran_index = e.tran_index;
	  r.num_requests = e.stats_slot->num_requests_received;
	  r.num_transactions = e.stats_slot->num_transactions_processed;
	  r.num_queries = e.stats_slot->num_queries_processed;
	  r.num_selects = e.stats_slot->num_select_queries;
	  r.num_inserts = e.stats_slot->num_insert_queries;
	  r.num_updates = e.stats_slot->num_update_queries;
	  r.num_deletes = e.stats_slot->num_delete_queries;
	  r.num_errors = e.stats_slot->num_error_queries;
	  r.num_long_queries = e.stats_slot->num_long_queries;
	  r.num_long_transactions = e.stats_slot->num_long_transactions;
	  std::strncpy (r.last_activity, e.stats_slot->log_msg, sizeof (r.last_activity) - 1);
	  r.last_activity[sizeof (r.last_activity) - 1] = '\0';
	}
      return n;
    }

    void
    registry_set_fn_status (std::uint32_t token, int fn_status)
    {
      manager *m = adoption_Manager;
      if (m == NULL)
	{
	  return;
	}
      std::lock_guard<std::mutex> guard (m->registry_mutex);
      auto it = m->registry.find (token);
      if (it != m->registry.end ())
	{
	  it->second.fn_status = fn_status;
	}
    }

    void
    registry_session_finished (std::uint32_t token)
    {
      manager *m = adoption_Manager;
      if (m == NULL)
	{
	  return;
	}
      m->active_finishers.fetch_add (1);

      std::uint64_t channel_id = 0;
      char owner_broker[BROKER_NAME_MAX];
      bool found = false;
      bool direct = false;
      owner_broker[0] = '\0';
      {
	std::lock_guard<std::mutex> guard (m->registry_mutex);
	auto it = m->registry.find (token);
	if (it != m->registry.end ())
	  {
	    channel_id = it->second.channel_id;
	    direct = it->second.direct;
	    std::memcpy (owner_broker, it->second.broker_name, sizeof (owner_broker));
	    m->registry.erase (it);
	    found = true;
	  }
      }
      m->registry_cv.notify_all ();
      if (!found || direct)
	{
	  /* a DIRECT_CONNECT session has no broker slot to free (wf122/B5) */
	  m->active_finishers.fetch_sub (1);
	  return;
	}

      /* frees the broker's slot (#117 D3) */
      std::shared_ptr<channel> ch;
      {
	std::lock_guard<std::mutex> guard (m->channels_mutex);
	auto it = m->channels.find (channel_id);
	if (it != m->channels.end ())
	  {
	    ch = it->second;
	  }
	else
	  {
	    /* the owning channel died (broker restart): the restarted broker
	     * rebuilt this token from the RESYNC reply, so deliver the END on
	     * any live channel of the same broker (codex F2, server half) —
	     * dropping it left the slot leaked until the next restart */
	    for (auto &pair : m->channels)
	      {
		if (std::strncmp (pair.second->broker_name, owner_broker, BROKER_NAME_MAX) == 0)
		  {
		    ch = pair.second;
		    break;
		  }
	      }
	  }
      }
      if (ch != NULL)
	{
	  token_body body;
	  body.token = token;
	  (void) send_message (*ch, msg_op::SESSION_END, &body, sizeof (body));
	}
      m->active_finishers.fetch_sub (1);
    }

    /* ------------------------------------------------------------------ */
    /* control-channel message handlers                                   */
    /* ------------------------------------------------------------------ */

    static void
    handle_handoff (manager &m, channel &ch, const handoff_body &body, int client_fd)
    {
      if (client_fd < 0)
	{
	  reject_body reject;
	  reject.reason = (std::int32_t) reject_reason::MALFORMED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return;
	}

      if (m.stopping.load ())
	{
	  reject_body reject;
	  reject.reason = (std::int32_t) reject_reason::SHUTDOWN;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  close (client_fd);
	  return;
	}

      /* the broker routed by the db_info dbname; verify before booting */
      char db_name[33];
      std::size_t name_len = strnlen (body.db_info, 32);
      std::memcpy (db_name, body.db_info, name_len);
      db_name[name_len] = '\0';
      if (m.db_name != db_name)
	{
	  reject_body reject;
	  reject.reason = (std::int32_t) reject_reason::DBNAME_MISMATCH;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  close (client_fd);
	  return;
	}

      /* the broker's ACCESS_MODE x REPLICA_ONLY becomes the session's client
       * type (#121 D1/D7); the decrement runs in the session thread's
       * css_free_conn, keyed by the same type via conn->client_type */
      int client_type = synthesize_client_type (body.access_mode, body.replica_only);
      {
	/* legacy parity (wf143 HA gate): a remote thin csql rides the broker
	 * TCP path, but it is the legacy csql client, not a driver behind a
	 * RW broker — a standby must admit it (reads work, writes get the
	 * server-side RO error), exactly as the fat csql did.  Its in-band
	 * mark is the fixed URL field of db_info (csql_wire.c). */
	const char *url = body.db_info + SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE;
	if (strncmp (url, "thin_csql", SRV_CON_URL_SIZE) == 0)
	  {
	    client_type = DB_CLIENT_TYPE_CSQL;
	  }
      }
      if (css_increment_num_conn ((BOOT_CLIENT_TYPE) client_type) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED, 1, NUM_NORMAL_TRANS);
	  reject_body reject;
	  reject.reason = (std::int32_t) reject_reason::CLIENTS_EXCEEDED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  close (client_fd);
	  return;
	}

      session_params params;
      params.client_fd = client_fd;
      params.slot_idx = body.slot_idx;
      params.client_ip = body.client_ip;
      params.client_port = body.client_port;
      params.client_type = client_type;
      std::memcpy (params.broker_info, body.broker_info, sizeof (params.broker_info));
      std::memcpy (params.driver_header, body.driver_header, sizeof (params.driver_header));
      std::memcpy (params.db_info, body.db_info, sizeof (params.db_info));
      params.server_name = m.db_name;
      params.broker_name = ch.broker_name;

      std::uint32_t token;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	do
	  {
	    token = m.next_token++;
	  }
	while (token == 0 || m.registry.count (token) > 0);

	session_entry entry;
	entry.token = token;
	entry.channel_id = ch.id;
	std::memcpy (entry.broker_name, ch.broker_name, sizeof (entry.broker_name));
	entry.client_fd = client_fd;
	entry.tran_index = NULL_TRAN_INDEX;
	entry.fn_status = FN_STATUS_PROBE_BUSY;
	m.registry.emplace (token, entry);
      }
      params.token = token;

      /* ACK strictly before the session may exist: a session that dies fast
       * would otherwise emit SESSION_END ahead of the ACK on this channel and
       * skew the broker's slot count (codex review F1) */
      token_body ack;
      ack.token = token;
      (void) send_message (ch, msg_op::HANDOFF_ACK, &ack, sizeof (ack));

      try
	{
	  std::thread session_thread (driver_session_run, std::move (params));
	  session_thread.detach ();
	}
      catch (const std::system_error &)
	{
	  css_decrement_num_conn ((BOOT_CLIENT_TYPE) client_type);
	  close (client_fd);
	  /* already ACKed: sign the token off like a session would */
	  registry_session_finished (token);
	  return;
	}
    }

    /* DIRECT_CONNECT (wf122/B5 D1/D2): a same-uid local csql turns its
     * control connection into the driver connection.  Returns true when fd
     * ownership moved to a session thread (the channel loop must then leave
     * the fd alone and exit); false leaves the channel as it was, with a
     * HANDOFF_REJECT frame sent on refusal. */
    /* the python probe and any external local client build this by hand */
    static_assert (sizeof (direct_connect_body) == 4 + DRIVER_HEADER_SIZE + DRIVER_DB_INFO_SIZE,
		   "direct_connect_body layout drifted");

    static bool
    handle_direct_connect (manager &m, channel &ch, const direct_connect_body &body)
    {
      reject_body reject;

      /* same-uid gate: the adoption socket becomes reachable by local
       * clients here; the broker ops above stay same-process-owner in
       * practice, this op enforces it (B5-D1) */
      struct ucred cred;
      socklen_t cred_len = sizeof (cred);
      if (getsockopt (ch.fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0 || cred.uid != geteuid ())
	{
	  reject.reason = (std::int32_t) reject_reason::NOT_AUTHORIZED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}

      /* only the csql family may declare its own type (B5-D2); drivers keep
       * arriving through the broker where the server synthesizes the type */
      int client_type = (int) body.client_type;
      if (!BOOT_CSQL_CLIENT_TYPE (client_type) || client_type == DB_CLIENT_TYPE_ADMIN_CSQL_REBUILD_CATALOG)
	{
	  reject.reason = (std::int32_t) reject_reason::NOT_AUTHORIZED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}

      if (m.stopping.load ())
	{
	  reject.reason = (std::int32_t) reject_reason::SHUTDOWN;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}

      char db_name[33];
      std::size_t name_len = strnlen (body.db_info, 32);
      std::memcpy (db_name, body.db_info, name_len);
      db_name[name_len] = '\0';
      if (m.db_name != db_name)
	{
	  reject.reason = (std::int32_t) reject_reason::DBNAME_MISMATCH;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}

      if (css_increment_num_conn ((BOOT_CLIENT_TYPE) client_type) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED, 1, NUM_NORMAL_TRANS);
	  reject.reason = (std::int32_t) reject_reason::CLIENTS_EXCEEDED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}

      /* codex review #6: transfer fd ownership to the session thread BEFORE
       * spawning it, so a fast-failing session can never close-and-let-reuse
       * a descriptor the channel loop / stop() still believes it owns.  The
       * channel keeps fd == -1 from here; on spawn failure we restore it. */
      int client_fd;
      {
	std::lock_guard<std::mutex> guard (ch.send_mutex);
	client_fd = ch.fd;
	ch.fd = -1;
      }

      session_params params;
      params.client_fd = client_fd;
      params.slot_idx = -1;	/* no broker slot */
      params.client_ip = htonl (INADDR_LOOPBACK);
      params.client_port = 0;
      params.client_type = client_type;
      params.broker_info[BROKER_INFO_DBMS_TYPE] = CAS_DBMS_CUBRID;
      params.broker_info[BROKER_INFO_KEEP_CONNECTION] = CAS_KEEP_CONNECTION_ON;
      params.broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_ON;
      params.broker_info[BROKER_INFO_CCI_PCONNECT] = CCI_PCONNECT_OFF;
      std::memset (params.broker_info + BROKER_INFO_PROTO_VERSION, 0,
		   sizeof (params.broker_info) - BROKER_INFO_PROTO_VERSION);
      std::memcpy (params.driver_header, body.driver_header, sizeof (params.driver_header));
      std::memcpy (params.db_info, body.db_info, sizeof (params.db_info));
      params.server_name = m.db_name;
      params.broker_name = "__direct__";
      params.direct = true;

      std::uint32_t token;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	do
	  {
	    token = m.next_token++;
	  }
	while (token == 0 || m.registry.count (token) > 0);

	session_entry entry;
	entry.token = token;
	entry.channel_id = ch.id;
	std::memcpy (entry.broker_name, params.broker_name.c_str (), params.broker_name.size () + 1);
	entry.client_fd = client_fd;
	entry.tran_index = NULL_TRAN_INDEX;
	entry.fn_status = FN_STATUS_PROBE_BUSY;
	entry.direct = true;
	m.registry.emplace (token, entry);
      }
      params.token = token;
      /* no HANDOFF_ACK: the client learns the token from the CAS connect
       * reply's pid slot, exactly like a broker-routed driver */

      try
	{
	  std::thread session_thread (driver_session_run, std::move (params));
	  session_thread.detach ();
	}
      catch (const std::system_error &)
	{
	  css_decrement_num_conn ((BOOT_CLIENT_TYPE) client_type);
	  registry_session_finished (token);
	  /* restore fd ownership to the channel so its exit path closes it */
	  {
	    std::lock_guard<std::mutex> guard (ch.send_mutex);
	    ch.fd = client_fd;
	  }
	  reject.reason = (std::int32_t) reject_reason::SHUTDOWN;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return false;
	}
      return true;
    }

    static void
    handle_cancel (manager &m, const token_body &body)
    {
      int tran_index = NULL_TRAN_INDEX;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	auto it = m.registry.find (body.token);
	if (it != m.registry.end ())
	  {
	    tran_index = it->second.tran_index;
	  }
      }
      if (tran_index != NULL_TRAN_INDEX)
	{
	  /* the query_cancel(SIGUSR1) successor (#117 D4): interrupt the
	   * session's transaction directly */
	  (void) logtb_set_tran_index_interrupt (NULL, tran_index, true);
	}
    }

    static void
    handle_status (manager &m, channel &ch, const token_body &body)
    {
      status_reply_body reply;
      reply.fn_status = FN_STATUS_PROBE_NONE;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	auto it = m.registry.find (body.token);
	if (it != m.registry.end ())
	  {
	    reply.fn_status = it->second.fn_status;
	  }
      }
      (void) send_message (ch, msg_op::STATUS_REPLY, &reply, sizeof (reply));
    }

    static void
    handle_resync (manager &m, channel &ch)
    {
      /* reply = resync_reply_body + live_count trailing resync_token_body
       * entries, so a restarted broker rebuilds its token table (codex F2) */
      std::vector<char> buf (sizeof (resync_reply_body));
      std::uint32_t live_count = 0;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	for (const auto &pair : m.registry)
	  {
	    if (std::strncmp (pair.second.broker_name, ch.broker_name, BROKER_NAME_MAX) == 0)
	      {
		resync_token_body t;
		t.token = pair.first;
		t.client_ip = pair.second.client_ip;
		const char *p = reinterpret_cast<const char *> (&t);
		buf.insert (buf.end (), p, p + sizeof (t));
		live_count++;
	      }
	  }
      }
      resync_reply_body reply;
      reply.live_count = live_count;
      std::memcpy (buf.data (), &reply, sizeof (reply));
      (void) send_message (ch, msg_op::RESYNC_REPLY, buf.data (), buf.size ());
    }

    /* ------------------------------------------------------------------ */
    /* channel and accept threads                                         */
    /* ------------------------------------------------------------------ */

    static void
    channel_thread_run (manager *m, std::shared_ptr<channel> ch)
    {
      /* CANCEL handling lands in engine code that uses the thread-local
       * error context (logtb interrupt notification) — register this foreign
       * thread like the session threads do */
      cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();
      if (entry_p != NULL)
	{
	  entry_p->register_id ();
	  entry_p->type = TT_SERVER;
	  entry_p->tran_index = -1;
	  entry_p->m_status = cubthread::entry::status::TS_RUN;
	  entry_p->shutdown = false;
	  entry_p->get_error_context ().register_thread_local ();
	}

      for (;;)
	{
	  msg_header header;
	  int handoff_fd = -1;

	  if (recv_with_fd (ch->fd, &header, sizeof (header), &handoff_fd) != NO_ERROR)
	    {
	      break;
	    }
	  if (header.magic != PROTO_MAGIC || header.length > sizeof (handoff_body))
	    {
	      if (handoff_fd >= 0)
		{
		  close (handoff_fd);
		}
	      break;		/* protocol violation: drop the channel */
	    }

	  char payload[sizeof (handoff_body)];
	  if (header.length > 0 && recv_with_fd (ch->fd, payload, header.length, &handoff_fd) != NO_ERROR)
	    {
	      if (handoff_fd >= 0)
		{
		  close (handoff_fd);
		}
	      break;
	    }

	  switch ((msg_op) header.op)
	    {
	    case msg_op::HELLO:
	      if (header.length == sizeof (hello_body))
		{
		  const hello_body *hello = reinterpret_cast<const hello_body *> (payload);
		  std::memcpy (ch->broker_name, hello->broker_name, BROKER_NAME_MAX);
		  ch->broker_name[BROKER_NAME_MAX - 1] = '\0';
		  hello_ack_body ack;
		  ack.proto_version = PROTO_VERSION;
		  (void) send_message (*ch, msg_op::HELLO_ACK, &ack, sizeof (ack));
		}
	      break;
	    case msg_op::HANDOFF:
	      if (header.length == sizeof (handoff_body))
		{
		  handoff_body body;
		  std::memcpy (&body, payload, sizeof (body));
		  handle_handoff (*m, *ch, body, handoff_fd);
		  handoff_fd = -1;	/* ownership transferred */
		}
	      else if (handoff_fd >= 0)
		{
		  close (handoff_fd);
		  handoff_fd = -1;
		}
	      break;
	    case msg_op::DIRECT_CONNECT:
	      if (header.length == sizeof (direct_connect_body))
		{
		  direct_connect_body body;
		  std::memcpy (&body, payload, sizeof (body));
		  if (handle_direct_connect (*m, *ch, body))
		    {
		      /* handle_direct_connect already transferred fd ownership
		       * (ch->fd == -1) to the session thread; just retire */
		      goto channel_done;
		    }
		}
	      break;
	    case msg_op::CANCEL:
	      if (header.length == sizeof (token_body))
		{
		  token_body body;
		  std::memcpy (&body, payload, sizeof (body));
		  handle_cancel (*m, body);
		}
	      break;
	    case msg_op::STATUS:
	      if (header.length == sizeof (token_body))
		{
		  token_body body;
		  std::memcpy (&body, payload, sizeof (body));
		  handle_status (*m, *ch, body);
		}
	      break;
	    case msg_op::RESYNC:
	      handle_resync (*m, *ch);
	      break;
	    default:
	      break;		/* unknown ops are skipped for forward compatibility */
	    }

	  if (handoff_fd >= 0)
	    {
	      close (handoff_fd);	/* fd arrived on a non-handoff message */
	    }
	}

channel_done:
      /* invalidate under send_mutex so no concurrent SESSION_END/reply can
       * write a closed (possibly reused) descriptor (codex review F4); the
       * map entry stays — the accept loop reaps dead channels, stop() joins
       * them (F3: the thread is joinable, never detached).  fd is already -1
       * when a DIRECT_CONNECT transferred it to a session thread. */
      {
	std::lock_guard<std::mutex> guard (ch->send_mutex);
	if (ch->fd >= 0)
	  {
	    close (ch->fd);
	  }
	ch->fd = -1;
      }
      ch->dead.store (true);

      if (entry_p != NULL)
	{
	  entry_p->tran_index = NULL_TRAN_INDEX;
	  entry_p->m_status = cubthread::entry::status::TS_DEAD;
	  entry_p->get_error_context ().deregister_thread_local ();
	  entry_p->unregister_id ();
	  cubthread::get_manager ()->retire_entry (*entry_p);
	}
    }

    static void
    accept_thread_run (manager *m)
    {
      for (;;)
	{
	  int fd = accept (m->listen_fd, NULL, NULL);
	  if (fd < 0)
	    {
	      if (errno == EINTR)
		{
		  continue;
		}
	      break;		/* listen socket closed: shutting down */
	    }
	  if (m->stopping.load ())
	    {
	      close (fd);
	      break;
	    }

	  std::shared_ptr<channel> ch;
	  try
	    {
	      ch = std::make_shared<channel> ();
	    }
	  catch (const std::bad_alloc &)
	    {
	      /* per-connection allocation failure is a refused connection,
	       * not std::terminate (reviewed: PR 7837) */
	      close (fd);
	      continue;
	    }
	  ch->fd = fd;
	  bool admitted = true;
	  {
	    std::lock_guard<std::mutex> guard (m->channels_mutex);
	    /* reap finished channel threads (a dead entry appears once per
	     * broker restart, so this stays tiny) */
	    for (auto it = m->channels.begin (); it != m->channels.end ();)
	      {
		if (it->second->dead.load ())
		  {
		    if (it->second->thread.joinable ())
		      {
			it->second->thread.join ();
		      }
		    it = m->channels.erase (it);
		  }
		else
		  {
		    ++it;
		  }
	      }
	    /* an unauthenticated local peer can connect repeatedly; bound the
	     * pre-admission channels so thread exhaustion cannot take the
	     * server down (reviewed: PR 7837) */
	    if (m->channels.size () >= (std::size_t) NUM_NORMAL_TRANS + 16)
	      {
		admitted = false;
	      }
	    else
	      {
		try
		  {
		    ch->id = m->next_channel_id++;
		    m->channels.emplace (ch->id, ch);
		  }
		catch (const std::bad_alloc &)
		  {
		    admitted = false;
		  }
	      }
	  }
	  if (!admitted)
	    {
	      close (fd);
	      ch->fd = -1;
	      continue;
	    }
	  try
	    {
	      ch->thread = std::thread (channel_thread_run, m, ch);
	    }
	  catch (const std::system_error &)
	    {
	      /* out of threads is a refused connection, not std::terminate */
	      {
		std::lock_guard<std::mutex> guard (m->channels_mutex);
		m->channels.erase (ch->id);
	      }
	      std::lock_guard<std::mutex> send_guard (ch->send_mutex);
	      close (ch->fd);
	      ch->fd = -1;
	    }
	}
    }

    /* ------------------------------------------------------------------ */
    /* lifecycle                                                          */
    /* ------------------------------------------------------------------ */

    /* <TMP>/<prefix>_adopt_<dbname> — same directory convention as the
     * master's well-known socket (css_get_master_domain_path) */
    static std::string
    get_adoption_domain_path (const char *db_name)
    {
      const char *cubrid_tmp = envvar_get ("TMP");
      if (cubrid_tmp == NULL || cubrid_tmp[0] == '\0')
	{
	  cubrid_tmp = "/tmp";
	}
      std::string path (cubrid_tmp);
      path += "/";
      path += envvar_prefix ();
      path += "_adopt_";
      path += db_name;
      return path;
    }

    int
    start (const char *db_name)
    {
      assert (adoption_Manager == NULL);

      manager *m = new manager ();
      m->db_name = db_name;
      m->socket_path = get_adoption_domain_path (db_name);

      struct sockaddr_un addr;
      if (m->socket_path.length () >= sizeof (addr.sun_path))
	{
	  er_log_debug (ARG_FILE_LINE, "adoption: socket path too long: %s\n", m->socket_path.c_str ());
	  delete m;
	  return ER_FAILED;
	}

      /* stale socket from a crashed server: remove it (tcp.c precedent) */
      struct stat stat_buf;
      if (stat (m->socket_path.c_str (), &stat_buf) == 0 && S_ISSOCK (stat_buf.st_mode))
	{
	  unlink (m->socket_path.c_str ());
	}

      m->listen_fd = socket (AF_UNIX, SOCK_STREAM, 0);
      if (m->listen_fd < 0)
	{
	  delete m;
	  return ER_FAILED;
	}

      std::memset (&addr, 0, sizeof (addr));
      addr.sun_family = AF_UNIX;
      std::strcpy (addr.sun_path, m->socket_path.c_str ());
      /* codex review #4: this endpoint now also serves local csql
       * (DIRECT_CONNECT).  The broker and every local csql run as the same
       * user as the server, so restrict the socket to owner-only — closing
       * the cross-UID spoof vector for ALL ops (HELLO/HANDOFF/RESYNC/CANCEL,
       * not just DIRECT_CONNECT's own SO_PEERCRED check).  fchmod before bind
       * would not stick on the inode; chmod the bound path with a tight
       * umask around bind to avoid the create-time window. */
      mode_t old_umask = umask (0077);
      int bind_rc = bind (m->listen_fd, (struct sockaddr *) &addr, sizeof (addr));
      umask (old_umask);
      if (bind_rc < 0 || chmod (m->socket_path.c_str (), S_IRUSR | S_IWUSR) < 0 || listen (m->listen_fd, 8) < 0)
	{
	  close (m->listen_fd);
	  delete m;
	  return ER_FAILED;
	}

      /* the folded CAS speaker's process-wide config stub (cas_server_support) */
      cas_server_speaker_boot_init (db_name);

      m->accept_thread = std::thread (accept_thread_run, m);
      adoption_Manager = m;
      return NO_ERROR;
    }

    void
    stop (void)
    {
      manager *m = adoption_Manager;
      if (m == NULL)
	{
	  return;
	}
      m->stopping.store (true);

      /* wake the accept thread */
      shutdown (m->listen_fd, SHUT_RDWR);
      close (m->listen_fd);
      if (m->accept_thread.joinable ())
	{
	  m->accept_thread.join ();
	}

      /* wake the control-channel readers; their (joinable) threads mark
       * themselves dead and are joined below (codex review F3) */
      {
	std::lock_guard<std::mutex> guard (m->channels_mutex);
	for (auto &pair : m->channels)
	  {
	    std::lock_guard<std::mutex> send_guard (pair.second->send_mutex);
	    if (pair.second->fd >= 0)
	      {
		shutdown (pair.second->fd, SHUT_RDWR);
	      }
	  }
      }

      /* wake every adopted session (their loops block on the client fd) and
       * wait for the sign-offs — sessions must unregister their trans while
       * the server infrastructure is still up */
      bool drained = false;
      {
	std::unique_lock<std::mutex> lock (m->registry_mutex);
	for (auto &pair : m->registry)
	  {
	    shutdown (pair.second.client_fd, SHUT_RDWR);
	    if (pair.second.tran_index != NULL_TRAN_INDEX)
	      {
		/* a session mid-request won't see the fd close until it
		 * returns to the wire; interrupt its transaction too */
		(void) logtb_set_tran_index_interrupt (NULL, pair.second.tran_index, true);
	      }
	  }
	drained = m->registry_cv.wait_for (lock, std::chrono::seconds (30), [m] { return m->registry.empty (); });
	assert (drained);
      }

      if (!drained)
	{
	  /* a stuck session still references the manager; freeing it here
	   * would be a use-after-free in release builds.  The process is
	   * shutting down — leak the manager instead of racing it. */
	  er_log_debug (ARG_FILE_LINE, "adoption: sessions still registered after 30s; leaking manager\n");
	  unlink (m->socket_path.c_str ());
	  adoption_Manager = NULL;
	  return;
	}

      /* a sign-off may still be inside the manager after its registry erase
       * (SESSION_END notify); don't free the manager under it (codex F2) */
      while (m->active_finishers.load () > 0)
	{
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      /* join the channel threads — nothing references the channels after
       * the sessions are gone and the finishers drained */
      {
	std::lock_guard<std::mutex> guard (m->channels_mutex);
	for (auto &pair : m->channels)
	  {
	    if (pair.second->thread.joinable ())
	      {
		pair.second->thread.join ();
	      }
	  }
	m->channels.clear ();
      }

      unlink (m->socket_path.c_str ());
      adoption_Manager = NULL;
      delete m;
    }
  }				/* namespace adoption */
}				/* namespace cubconn */

#endif /* SERVER_MODE */
