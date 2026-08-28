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
#include "connection_defs.h"
#include "connection_sr.h"	// css_increment_num_conn
#include "db_client_type.hpp"
#include "environment_variable.h"
#include "error_manager.h"
#include "log_impl.h"		// logtb_set_tran_index_interrupt
#include "porting.h"
#include "system_parameter.h"

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
      int fd;
      char broker_name[BROKER_NAME_MAX];
      std::mutex send_mutex;	/* replies and async SESSION_ENDs interleave */
      std::thread thread;

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

      std::uint64_t channel_id = 0;
      bool found = false;
      {
	std::lock_guard<std::mutex> guard (m->registry_mutex);
	auto it = m->registry.find (token);
	if (it != m->registry.end ())
	  {
	    channel_id = it->second.channel_id;
	    m->registry.erase (it);
	    found = true;
	  }
      }
      m->registry_cv.notify_all ();
      if (!found)
	{
	  return;
	}

      /* frees the broker's slot (#117 D3); the channel may be gone (broker
       * restart) — the re-sync handshake reconciles the count then */
      std::shared_ptr<channel> ch;
      {
	std::lock_guard<std::mutex> guard (m->channels_mutex);
	auto it = m->channels.find (channel_id);
	if (it != m->channels.end ())
	  {
	    ch = it->second;
	  }
      }
      if (ch != NULL)
	{
	  token_body body;
	  body.token = token;
	  (void) send_message (*ch, msg_op::SESSION_END, &body, sizeof (body));
	}
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

      /* adopted connections take normal seats (#117 D5/D6); the decrement
       * runs in the session thread's css_free_conn */
      if (css_increment_num_conn (DB_CLIENT_TYPE_DEFAULT) != NO_ERROR)
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
      std::memcpy (params.broker_info, body.broker_info, sizeof (params.broker_info));
      std::memcpy (params.driver_header, body.driver_header, sizeof (params.driver_header));
      std::memcpy (params.db_info, body.db_info, sizeof (params.db_info));
      params.server_name = m.db_name;

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

      try
	{
	  std::thread session_thread (driver_session_run, std::move (params));
	  session_thread.detach ();
	}
      catch (const std::system_error &)
	{
	  /* never ACKed: erase directly — a SESSION_END for a token the
	   * broker never learned would corrupt its slot count */
	  {
	    std::lock_guard<std::mutex> guard (m.registry_mutex);
	    m.registry.erase (token);
	  }
	  m.registry_cv.notify_all ();
	  css_decrement_num_conn (DB_CLIENT_TYPE_DEFAULT);
	  close (client_fd);
	  reject_body reject;
	  reject.reason = (std::int32_t) reject_reason::CLIENTS_EXCEEDED;
	  (void) send_message (ch, msg_op::HANDOFF_REJECT, &reject, sizeof (reject));
	  return;
	}

      token_body ack;
      ack.token = token;
      (void) send_message (ch, msg_op::HANDOFF_ACK, &ack, sizeof (ack));
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
      resync_reply_body reply;
      reply.live_count = 0;
      {
	std::lock_guard<std::mutex> guard (m.registry_mutex);
	for (const auto &pair : m.registry)
	  {
	    if (std::strncmp (pair.second.broker_name, ch.broker_name, BROKER_NAME_MAX) == 0)
	      {
		reply.live_count++;
	      }
	  }
      }
      (void) send_message (ch, msg_op::RESYNC_REPLY, &reply, sizeof (reply));
    }

    /* ------------------------------------------------------------------ */
    /* channel and accept threads                                         */
    /* ------------------------------------------------------------------ */

    static void
    channel_thread_run (manager *m, std::shared_ptr<channel> ch)
    {
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

      close (ch->fd);
      std::lock_guard<std::mutex> guard (m->channels_mutex);
      m->channels.erase (ch->id);
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

	  auto ch = std::make_shared<channel> ();
	  ch->fd = fd;
	  {
	    std::lock_guard<std::mutex> guard (m->channels_mutex);
	    ch->id = m->next_channel_id++;
	    m->channels.emplace (ch->id, ch);
	  }
	  ch->thread = std::thread (channel_thread_run, m, ch);
	  ch->thread.detach ();
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
      if (bind (m->listen_fd, (struct sockaddr *) &addr, sizeof (addr)) < 0
	  || listen (m->listen_fd, 8) < 0)
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

      /* drop the control channels; their (detached) threads erase themselves */
      {
	std::lock_guard<std::mutex> guard (m->channels_mutex);
	for (auto &pair : m->channels)
	  {
	    shutdown (pair.second->fd, SHUT_RDWR);
	  }
      }

      /* wake every adopted session (their loops block on the client fd) and
       * wait for the sign-offs — sessions must unregister their trans while
       * the server infrastructure is still up */
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
	m->registry_cv.wait_for (lock, std::chrono::seconds (30), [m] { return m->registry.empty (); });
	assert (m->registry.empty ());
      }

      /* channel threads are detached and may still be erasing themselves;
       * give them a beat before the map goes away */
      for (int i = 0; i < 100; i++)
	{
	  {
	    std::lock_guard<std::mutex> guard (m->channels_mutex);
	    if (m->channels.empty ())
	      {
		break;
	      }
	  }
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      unlink (m->socket_path.c_str ());
      adoption_Manager = NULL;
      delete m;
    }
  }				/* namespace adoption */
}				/* namespace cubconn */

#endif /* SERVER_MODE */
