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
 * broker_direct.cpp - broker side of the server connection handoff (stage B1)
 *
 * Three moving parts, all connection-setup cold path:
 *
 * - peek engine: one epoll thread completing each parked client's db_info
 *   packet non-blockingly (#117 D2 — no head-of-line blocking), absorbing
 *   HEALTH_CHECK_DUMMY_DB, then enqueueing the job into the existing shm job
 *   queue (aging/rejection semantics untouched, #117 D3).
 * - channel manager: one persistent control channel per database (dial on
 *   demand to the server's adoption socket), a reader thread per channel that
 *   routes async SESSION_ENDs (slot release) and request replies.
 * - token table: server-issued cancel token -> {dbname, client addr}; the
 *   anti-spoof check the pid scan used to do (#117 D4/D8).
 */

#define ADOPTION_PROTOCOL_ONLY

#include "broker_direct.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "adoption.hpp"
#include "cas_common.h"		/* FN_STATUS_* */
#include "cas_error.h"
#include "cas_protocol.h"
#include "environment_variable.h"

namespace brd
{
  namespace adopt = cubconn::adoption;

  static const int DB_INFO_PEEK_TIMEOUT_SEC = 60;	/* inherited header-read budget */
  static const int CHANNEL_REPLY_TIMEOUT_SEC = 5;

  /* cold-path diagnostics: set BRD_DEBUG_LOG=<path> in the broker's
   * environment to trace handoff/cancel decisions */
  static void
  brd_debug (const char *fmt, ...)
  {
    static FILE *fp = NULL;
    static bool tried = false;
    if (!tried)
      {
	tried = true;
	const char *path = getenv ("BRD_DEBUG_LOG");
	if (path != NULL && path[0] != '\0')
	  {
	    fp = fopen (path, "a");
	  }
      }
    if (fp == NULL)
      {
	return;
      }
    va_list ap;
    va_start (ap, fmt);
    vfprintf (fp, fmt, ap);
    va_end (ap);
    fputc ('\n', fp);
    fflush (fp);
  }

  /* ------------------------------------------------------------------ */
  /* small wire helpers                                                 */
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
	    return -1;
	  }
	p += n;
	len -= (std::size_t) n;
      }
    return 0;
  }

  static int
  recv_all (int fd, void *buf, std::size_t len)
  {
    char *p = static_cast<char *> (buf);
    while (len > 0)
      {
	ssize_t n = recv (fd, p, len, 0);
	if (n <= 0)
	  {
	    if (n < 0 && errno == EINTR)
	      {
		continue;
	      }
	    return -1;
	  }
	p += n;
	len -= (std::size_t) n;
      }
    return 0;
  }

  /* send_error_to_driver's conversion logic (broker.c keeps its own static);
   * direct mode is V12-single so the renewed-code branch always applies, but
   * the check stays faithful */
  static void
  send_error_code_to_driver (SOCKET fd, int error, const char *driver_info)
  {
    int write_val;
    if (error == 0)
      {
	write_val = 0;
      }
    else if ((driver_info[SRV_CON_MSG_IDX_PROTO_VERSION] & CAS_PROTO_INDICATOR)
	     && (driver_info[SRV_CON_MSG_IDX_FUNCTION_FLAG] & BROKER_RENEWED_ERROR_CODE))
      {
	write_val = htonl (error);
      }
    else
      {
	write_val = htonl (CAS_CONV_ERROR_TO_OLD (error));
      }
    (void) send_all (fd, &write_val, sizeof (int));
  }

  /* ------------------------------------------------------------------ */
  /* state                                                              */
  /* ------------------------------------------------------------------ */

  struct parked_client
  {
    SOCKET fd;
    T_MAX_HEAP_NODE job;
    char db_info[adopt::DRIVER_DB_INFO_SIZE];
    std::size_t got;
    time_t deadline;
  };

  struct channel;

  struct token_info
  {
    std::string db_name;
    unsigned char clt_ip[4];
    unsigned short clt_port;
  };

  struct manager
  {
    std::string broker_name;
    std::string ssl_db;		/* DIRECT_HANDOFF_SSL_DB: the route for SSL clients (B2-D9) */
    int max_slots = 0;
    /* read live at handoff time (ACCESS_MODE stays dynamic, #121 D1/B4) and
     * written for the front metrics + slot mirror (#116 D10) */
    T_SHM_APPL_SERVER *shm = NULL;

    /* job queue plumbing owned by broker.c */
    T_MAX_HEAP_NODE *job_queue = NULL;
    int job_queue_size = 0;
    pthread_mutex_t *job_queue_mutex = NULL;
    pthread_cond_t *job_queue_cond = NULL;

    /* peek engine */
    int epoll_fd = -1;
    int wakeup_fd = -1;		/* eventfd: new parked fds + shutdown */
    std::thread peek_thread;
    std::mutex park_inbox_mutex;
    std::deque<parked_client *> park_inbox;
    std::unordered_map<parked_client *, bool> parked;	/* peek thread only: deadline sweep */
    std::atomic<bool> stopping { false };

    /* completed db_info packets, keyed by job id, consumed by dispatch */
    std::mutex db_info_mutex;
    std::unordered_map<int, std::unique_ptr<char[]>> db_info_by_job;

    /* handoff slots: ACK -1 is done by the dispatch thread, SESSION_END +1
     * by channel readers (#117 D3) */
    std::atomic<int> slots_used { 0 };

    /* channels + tokens.  Token counters are per-server, so two databases
     * behind one broker may issue equal token values — the table is a
     * multimap keyed by token and disambiguated by database (B4). */
    std::mutex channels_mutex;
    std::unordered_map<std::string, std::shared_ptr<channel>> channels;
    std::mutex dial_mutex;	/* single-flight channel creation (codex F7) */
    std::mutex tokens_mutex;
    std::unordered_multimap<unsigned int, token_info> tokens;
    /* SESSION_ENDs that arrived before their HANDOFF_ACK was processed
     * (reader vs dispatch ordering); the ACK consumes them (codex F1) */
    std::set<std::pair<unsigned int, std::string>> orphan_ends;
  };

  static manager *brd_Manager = NULL;

  struct channel
  {
    std::string db_name;
    int fd = -1;
    std::thread reader;
    std::mutex send_mutex;

    /* one outstanding request/reply at a time */
    std::mutex request_mutex;
    std::mutex reply_mutex;
    std::condition_variable reply_cv;
    bool reply_ready = false;
    adopt::msg_header reply_header;
    /* sized at dial: RESYNC replies carry up to max_slots trailing token
     * entries (codex F2) */
    std::vector<char> reply_body;

    std::atomic<bool> dead { false };
  };

  /* ------------------------------------------------------------------ */
  /* token table                                                        */
  /* ------------------------------------------------------------------ */

  /* front rejection: count it (#116 D10) and answer the driver.  Callers on
   * the receiver, peek, and dispatch threads share the counter — atomic. */
  static void
  reject_client (manager &m, SOCKET fd, int error, const char *driver_info)
  {
    __atomic_add_fetch (&m.shm->brd_num_rejected, 1, __ATOMIC_RELAXED);
    send_error_code_to_driver (fd, error, driver_info);
  }

  /* slot accounting, mirrored into shm for `cubrid broker status` (#116 D10) */
  static void
  slots_acquire (manager &m, int count)
  {
    /* mirror the value THIS transition produced (codex F6: a separate load
     * interleaves with concurrent updates and publishes a stale count) */
    int now = m.slots_used.fetch_add (count) + count;
    m.shm->brd_slots_used = now;
  }

  static void
  slots_release (manager &m)
  {
    /* floor at 0: a pre-restart session's end can race the re-sync that
     * would have counted it */
    int used = m.slots_used.load ();
    while (used > 0 && !m.slots_used.compare_exchange_weak (used, used - 1))
      ;
    m.shm->brd_slots_used = (used > 0) ? used - 1 : 0;
  }

  static void
  tokens_drop_for_db (manager &m, const std::string &db_name)
  {
    /* erase and release under the same lock the ACK path holds while it
     * inserts+acquires: either order then nets to a consistent count
     * (codex F4 — releasing outside let a late ACK acquire a slot for a
     * token this loop had already dropped) */
    std::lock_guard<std::mutex> guard (m.tokens_mutex);
    for (auto it = m.tokens.begin (); it != m.tokens.end ();)
      {
	if (it->second.db_name == db_name)
	  {
	    it = m.tokens.erase (it);
	    /* the sessions died with their server: free their slots (#117
	     * D7 — otherwise every server restart leaks slots until the
	     * pool starves) */
	    slots_release (m);
	  }
	else
	  {
	    ++it;
	  }
      }
  }

  /* ------------------------------------------------------------------ */
  /* channel manager                                                    */
  /* ------------------------------------------------------------------ */

  static void
  channel_reader_run (manager *m, std::shared_ptr<channel> ch)
  {
    for (;;)
      {
	adopt::msg_header header;
	if (recv_all (ch->fd, &header, sizeof (header)) != 0)
	  {
	    break;
	  }
	if (header.magic != adopt::PROTO_MAGIC || header.length > ch->reply_body.size ())
	  {
	    break;
	  }
	std::vector<char> body (ch->reply_body.size ());
	if (header.length > 0 && recv_all (ch->fd, body.data (), header.length) != 0)
	  {
	    break;
	  }

	if ((adopt::msg_op) header.op == adopt::msg_op::SESSION_END)
	  {
	    if (header.length == sizeof (adopt::token_body))
	      {
		adopt::token_body tb;
		std::memcpy (&tb, body.data (), sizeof (tb));
		bool known = false;
		{
		  std::lock_guard<std::mutex> guard (m->tokens_mutex);
		  auto range = m->tokens.equal_range (tb.token);
		  for (auto it = range.first; it != range.second; ++it)
		    {
		      /* this channel's database scopes the erase — equal
		       * token values may be live for other databases */
		      if (it->second.db_name == ch->db_name)
			{
			  m->tokens.erase (it);
			  known = true;
			  break;
			}
		    }
		  if (known)
		    {
		      slots_release (*m);
		    }
		  else
		    {
		      /* the dispatch thread has not processed the ACK yet:
		       * park the end for the ACK to consume (codex F1) */
		      m->orphan_ends.insert (std::make_pair (tb.token, ch->db_name));
		    }
		}
	      }
	    continue;
	  }

	/* request reply: hand to the waiting requester */
	{
	  std::lock_guard<std::mutex> guard (ch->reply_mutex);
	  ch->reply_header = header;
	  std::memcpy (ch->reply_body.data (), body.data (), header.length);
	  ch->reply_ready = true;
	}
	ch->reply_cv.notify_one ();
      }

    /* channel death: server restart or protocol violation (#117 D7) — the
     * db is dialed again on the next dispatch; its sessions are gone */
    ch->dead.store (true);
    ch->reply_cv.notify_all ();
    /* invalidate under send_mutex so no request/cancel thread can write a
     * closed (possibly reused) descriptor (codex F6) */
    {
      std::lock_guard<std::mutex> guard (ch->send_mutex);
      close (ch->fd);
      ch->fd = -1;
    }
    {
      std::lock_guard<std::mutex> guard (m->channels_mutex);
      auto it = m->channels.find (ch->db_name);
      if (it != m->channels.end () && it->second.get () == ch.get ())
	{
	  m->channels.erase (it);
	}
    }
    tokens_drop_for_db (*m, ch->db_name);
  }

  /* send a request and wait for its reply (SESSION_ENDs are routed past the
   * mailbox by the reader).  Returns 0 and fills reply on success. */
  static int
  channel_request (channel &ch, adopt::msg_op op, const void *body, std::size_t body_len,
		   const void *handoff_payload, std::size_t handoff_payload_len, int handoff_fd,
		   adopt::msg_header *reply_header, void *reply_body, std::size_t reply_body_size)
  {
    std::lock_guard<std::mutex> request_guard (ch.request_mutex);

    {
      std::lock_guard<std::mutex> guard (ch.reply_mutex);
      ch.reply_ready = false;
    }

    adopt::msg_header header;
    header.magic = adopt::PROTO_MAGIC;
    header.op = (std::uint32_t) op;
    header.length = (std::uint32_t) (body_len + handoff_payload_len);

    {
      std::lock_guard<std::mutex> send_guard (ch.send_mutex);
      if (ch.fd < 0)
	{
	  return -1;		/* invalidated by the reader (codex F6) */
	}
      if (handoff_fd >= 0)
	{
	  /* single sendmsg so SCM_RIGHTS rides with the message head */
	  struct iovec iov[3];
	  int iovcnt = 0;
	  iov[iovcnt].iov_base = &header;
	  iov[iovcnt++].iov_len = sizeof (header);
	  if (body_len > 0)
	    {
	      iov[iovcnt].iov_base = const_cast<void *> (body);
	      iov[iovcnt++].iov_len = body_len;
	    }
	  if (handoff_payload_len > 0)
	    {
	      iov[iovcnt].iov_base = const_cast<void *> (handoff_payload);
	      iov[iovcnt++].iov_len = handoff_payload_len;
	    }

	  char control[CMSG_SPACE (sizeof (int))];
	  std::memset (control, 0, sizeof (control));
	  struct msghdr msg;
	  std::memset (&msg, 0, sizeof (msg));
	  msg.msg_iov = iov;
	  msg.msg_iovlen = iovcnt;
	  msg.msg_control = control;
	  msg.msg_controllen = sizeof (control);
	  struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msg);
	  cmsg->cmsg_level = SOL_SOCKET;
	  cmsg->cmsg_type = SCM_RIGHTS;
	  cmsg->cmsg_len = CMSG_LEN (sizeof (int));
	  std::memcpy (CMSG_DATA (cmsg), &handoff_fd, sizeof (int));

	  ssize_t sent;
	  do
	    {
	      sent = sendmsg (ch.fd, &msg, MSG_NOSIGNAL);
	    }
	  while (sent < 0 && errno == EINTR);	/* nothing transferred yet (codex F8) */
	  std::size_t total = sizeof (header) + body_len + handoff_payload_len;
	  if (sent < 0)
	    {
	      return -1;
	    }
	  /* a short sendmsg still delivered the fd; finish the byte stream */
	  if ((std::size_t) sent < total)
	    {
	      char tail[sizeof (adopt::msg_header) + sizeof (adopt::handoff_body)];
	      char *w = tail;
	      std::memcpy (w, &header, sizeof (header));
	      w += sizeof (header);
	      if (body_len > 0)
		{
		  std::memcpy (w, body, body_len);
		  w += body_len;
		}
	      if (handoff_payload_len > 0)
		{
		  std::memcpy (w, handoff_payload, handoff_payload_len);
		  w += handoff_payload_len;
		}
	      if (send_all (ch.fd, tail + sent, total - (std::size_t) sent) != 0)
		{
		  return -1;
		}
	    }
	}
      else
	{
	  if (send_all (ch.fd, &header, sizeof (header)) != 0)
	    {
	      return -1;
	    }
	  if (body_len > 0 && send_all (ch.fd, body, body_len) != 0)
	    {
	      return -1;
	    }
	}
    }

    std::unique_lock<std::mutex> lock (ch.reply_mutex);
    if (!ch.reply_cv.wait_for (lock, std::chrono::seconds (CHANNEL_REPLY_TIMEOUT_SEC),
			       [&ch] { return ch.reply_ready || ch.dead.load (); }))
      {
	/* a late reply must never satisfy a LATER request on this channel:
	 * kill it — the next use redials (codex F5) */
	ch.dead.store (true);
	{
	  std::lock_guard<std::mutex> send_guard (ch.send_mutex);
	  if (ch.fd >= 0)
	    {
	      shutdown (ch.fd, SHUT_RDWR);	/* the reader wakes and cleans up */
	    }
	}
	return -1;
      }
    if (!ch.reply_ready)
      {
	return -1;		/* channel died */
      }
    *reply_header = ch.reply_header;
    if (ch.reply_header.length > 0 && reply_body != NULL)
      {
	if (ch.reply_header.length > reply_body_size)
	  {
	    /* a reply bigger than the caller's buffer is a protocol error —
	     * refuse it instead of truncating silently (codex F2 rework) */
	    assert (false);
	    return -1;
	  }
	std::memcpy (reply_body, ch.reply_body.data (), ch.reply_header.length);
      }
    return 0;
  }

  /* fire-and-forget (CANCEL) */
  static int
  channel_send (channel &ch, adopt::msg_op op, const void *body, std::size_t body_len)
  {
    adopt::msg_header header;
    header.magic = adopt::PROTO_MAGIC;
    header.op = (std::uint32_t) op;
    header.length = (std::uint32_t) body_len;

    std::lock_guard<std::mutex> guard (ch.send_mutex);
    if (ch.fd < 0)
      {
	return -1;		/* invalidated by the reader (codex F6) */
      }
    if (send_all (ch.fd, &header, sizeof (header)) != 0)
      {
	return -1;
      }
    return body_len > 0 ? send_all (ch.fd, body, body_len) : 0;
  }

  /* must byte-match the server's get_adoption_domain_path (adoption.cpp) */
  static std::string
  adoption_socket_path (const char *db_name)
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

  /* dial + HELLO + RESYNC; returns NULL when the server is not up (#117 D5:
   * the caller rejects the driver retryably) */
  static std::shared_ptr<channel>
  channel_get_or_dial (manager &m, const std::string &db_name)
  {
    /* single-flight: without this, two threads can dial the same db, apply
     * RESYNC accounting twice, and orphan one live channel (codex F7) */
    std::lock_guard<std::mutex> dial_guard (m.dial_mutex);

    {
      std::lock_guard<std::mutex> guard (m.channels_mutex);
      auto it = m.channels.find (db_name);
      if (it != m.channels.end () && !it->second->dead.load ())
	{
	  return it->second;
	}
    }

    std::string path = adoption_socket_path (db_name.c_str ());
    struct sockaddr_un addr;
    if (path.length () >= sizeof (addr.sun_path))
      {
	return NULL;
      }
    int fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
      {
	return NULL;
      }
    std::memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    std::strcpy (addr.sun_path, path.c_str ());
    if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0)
      {
	close (fd);
	return NULL;
      }

    auto ch = std::make_shared<channel> ();
    ch->db_name = db_name;
    ch->fd = fd;
    /* big enough for a RESYNC reply carrying max_slots token entries */
    ch->reply_body.resize (64 + (std::size_t) m.max_slots * sizeof (adopt::resync_token_body));
    ch->reader = std::thread (channel_reader_run, &m, ch);
    ch->reader.detach ();

    adopt::hello_body hello;
    std::memset (&hello, 0, sizeof (hello));
    hello.proto_version = adopt::PROTO_VERSION;
    strncpy (hello.broker_name, m.broker_name.c_str (), sizeof (hello.broker_name) - 1);

    adopt::msg_header reply_header;
    std::vector<char> reply_body (ch->reply_body.size ());
    if (channel_request (*ch, adopt::msg_op::HELLO, &hello, sizeof (hello), NULL, 0, -1,
			 &reply_header, reply_body.data (), reply_body.size ()) != 0
	|| (adopt::msg_op) reply_header.op != adopt::msg_op::HELLO_ACK)
      {
	ch->dead.store (true);
	shutdown (fd, SHUT_RDWR);
	return NULL;
      }

    /* restart re-sync (#117 D7): sessions surviving a broker restart occupy
     * slots the new incarnation doesn't know about.  The reply carries the
     * survivors' tokens (codex F2) — rebuild the table with them, or every
     * survivor's later SESSION_END is an unknown token and its slot leaks. */
    if (channel_request (*ch, adopt::msg_op::RESYNC, NULL, 0, NULL, 0, -1,
			 &reply_header, reply_body.data (), reply_body.size ()) == 0
	&& (adopt::msg_op) reply_header.op == adopt::msg_op::RESYNC_REPLY
	&& reply_header.length >= sizeof (adopt::resync_reply_body))
      {
	adopt::resync_reply_body resync;
	std::memcpy (&resync, reply_body.data (), sizeof (resync));
	std::size_t trailing = (reply_header.length - sizeof (resync)) / sizeof (adopt::resync_token_body);
	std::size_t n = (trailing < (std::size_t) resync.live_count) ? trailing : (std::size_t) resync.live_count;
	{
	  std::lock_guard<std::mutex> guard (m.tokens_mutex);
	  for (std::size_t i = 0; i < n; i++)
	    {
	      adopt::resync_token_body t;
	      std::memcpy (&t, reply_body.data () + sizeof (resync) + i * sizeof (t), sizeof (t));
	      token_info ti;
	      ti.db_name = db_name;
	      std::memcpy (ti.clt_ip, &t.client_ip, 4);
	      ti.clt_port = 0;	/* unknown post-restart: cancel requires the ip to match */
	      m.tokens.emplace (t.token, ti);
	    }
	}
	if (resync.live_count > 0)
	  {
	    slots_acquire (m, (int) resync.live_count);
	  }
      }

    std::lock_guard<std::mutex> guard (m.channels_mutex);
    m.channels[db_name] = ch;
    return ch;
  }

  /* ------------------------------------------------------------------ */
  /* peek engine                                                        */
  /* ------------------------------------------------------------------ */

  static void
  park_finish_health_check (parked_client *pc)
  {
    /* cas_common_main.c:225-233 verbatim: int 0, then active cas_info */
    int zero = 0;
    char cas_info[CAS_INFO_SIZE] =
    { CAS_INFO_STATUS_ACTIVE, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT, CAS_INFO_RESERVED_DEFAULT };
    (void) send_all (pc->fd, &zero, sizeof (int));
    (void) send_all (pc->fd, cas_info, sizeof (cas_info));
  }

  /* db_info complete: absorb health checks, else enqueue the job with the
   * packet stashed for the dispatch thread */
  static void
  park_complete (manager &m, parked_client *pc)
  {
    char db_name[33];
    std::size_t len = strnlen (pc->db_info, 32);
    std::memcpy (db_name, pc->db_info, len);
    db_name[len] = '\0';

    if (std::strcmp (db_name, HEALTH_CHECK_DUMMY_DB) == 0)
      {
	park_finish_health_check (pc);
	close (pc->fd);
	delete pc;
	return;
      }

    {
      std::lock_guard<std::mutex> guard (m.db_info_mutex);
      std::unique_ptr<char[]> stash (new char[adopt::DRIVER_DB_INFO_SIZE]);
      std::memcpy (stash.get (), pc->db_info, adopt::DRIVER_DB_INFO_SIZE);
      m.db_info_by_job[pc->job.id] = std::move (stash);
    }

    /* same enqueue discipline as receiver_thr_f: spin on a transiently full
     * heap (the receiver's pre-check already rejected a full queue) */
    pc->job.clt_sock_fd = pc->fd;
    for (;;)
      {
	pthread_mutex_lock (m.job_queue_mutex);
	if (max_heap_insert (m.job_queue, m.job_queue_size, &pc->job) < 0)
	  {
	    pthread_mutex_unlock (m.job_queue_mutex);
	    if (m.stopping.load ())
	      {
		break;
	      }
	    std::this_thread::sleep_for (std::chrono::milliseconds (100));
	  }
	else
	  {
	    pthread_cond_signal (m.job_queue_cond);
	    pthread_mutex_unlock (m.job_queue_mutex);
	    break;
	  }
      }
    delete pc;
  }

  static void
  park_drop (manager &m, parked_client *pc)
  {
    (void) m;
    close (pc->fd);
    delete pc;
  }

  static void
  peek_thread_run (manager *m)
  {
    for (;;)
      {
	struct epoll_event events[64];
	int n = epoll_wait (m->epoll_fd, events, 64, 1000);
	if (m->stopping.load ())
	  {
	    break;
	  }

	for (int i = 0; i < n; i++)
	  {
	    if (events[i].data.ptr == NULL)
	      {
		/* wakeup eventfd: drain and admit the inbox */
		std::uint64_t v;
		(void) read (m->wakeup_fd, &v, sizeof (v));
		std::deque<parked_client *> inbox;
		{
		  std::lock_guard<std::mutex> guard (m->park_inbox_mutex);
		  inbox.swap (m->park_inbox);
		}
		for (parked_client *pc : inbox)
		  {
		    struct epoll_event ev;
		    ev.events = EPOLLIN;
		    ev.data.ptr = pc;
		    if (epoll_ctl (m->epoll_fd, EPOLL_CTL_ADD, pc->fd, &ev) != 0)
		      {
			park_drop (*m, pc);
		      }
		    else
		      {
			m->parked[pc] = true;
		      }
		  }
		continue;
	      }

	    parked_client *pc = static_cast<parked_client *> (events[i].data.ptr);
	    bool done = false, drop = false;
	    for (;;)
	      {
		ssize_t r = recv (pc->fd, pc->db_info + pc->got, adopt::DRIVER_DB_INFO_SIZE - pc->got, 0);
		if (r > 0)
		  {
		    pc->got += (std::size_t) r;
		    if (pc->got == adopt::DRIVER_DB_INFO_SIZE)
		      {
			done = true;
			break;
		      }
		    continue;
		  }
		if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
		  {
		    drop = true;
		  }
		break;
	      }
	    if (done || drop)
	      {
		(void) epoll_ctl (m->epoll_fd, EPOLL_CTL_DEL, pc->fd, NULL);
		m->parked.erase (pc);
		if (done)
		  {
		    park_complete (*m, pc);
		  }
		else
		  {
		    park_drop (*m, pc);
		  }
	      }
	  }

	/* deadline sweep: a driver that never completes db_info is dropped,
	 * inheriting the old 60s header-read budget */
	time_t now = time (NULL);
	for (auto it = m->parked.begin (); it != m->parked.end ();)
	  {
	    parked_client *pc = it->first;
	    if (pc->deadline <= now)
	      {
		(void) epoll_ctl (m->epoll_fd, EPOLL_CTL_DEL, pc->fd, NULL);
		it = m->parked.erase (it);
		park_drop (*m, pc);
	      }
	    else
	      {
		++it;
	      }
	  }
      }

    /* shutdown: drop what is still parked */
    for (auto &pair : m->parked)
      {
	park_drop (*m, pair.first);
      }
    m->parked.clear ();
    close (m->epoll_fd);
    close (m->wakeup_fd);
  }
}				/* namespace brd */

/* -------------------------------------------------------------------- */
/* C facade                                                             */
/* -------------------------------------------------------------------- */

using namespace brd;

int
brd_init (const char *broker_name, int max_slots, T_SHM_APPL_SERVER *shm_appl, const char *ssl_db,
	  T_MAX_HEAP_NODE *job_queue, int job_queue_size, pthread_mutex_t *job_queue_mutex,
	  pthread_cond_t *job_queue_cond)
{
  assert (brd_Manager == NULL);
  manager *m = new manager ();
  m->broker_name = broker_name;
  m->max_slots = max_slots;
  m->shm = shm_appl;
  shm_appl->brd_num_accepted = 0;
  shm_appl->brd_num_handoffs = 0;
  shm_appl->brd_num_rejected = 0;
  shm_appl->brd_slots_used = 0;
  m->ssl_db = (ssl_db != NULL) ? ssl_db : "";
  m->job_queue = job_queue;
  m->job_queue_size = job_queue_size;
  m->job_queue_mutex = job_queue_mutex;
  m->job_queue_cond = job_queue_cond;

  m->epoll_fd = epoll_create1 (EPOLL_CLOEXEC);
  m->wakeup_fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.ptr = NULL;		/* NULL tags the wakeup fd */
  if (m->epoll_fd < 0 || m->wakeup_fd < 0 || epoll_ctl (m->epoll_fd, EPOLL_CTL_ADD, m->wakeup_fd, &ev) != 0)
    {
      if (m->epoll_fd >= 0)
	{
	  close (m->epoll_fd);
	}
      if (m->wakeup_fd >= 0)
	{
	  close (m->wakeup_fd);
	}
      delete m;
      return -1;
    }

  m->peek_thread = std::thread (peek_thread_run, m);
  brd_Manager = m;
  return 0;
}

void
brd_final (void)
{
  manager *m = brd_Manager;
  if (m == NULL)
    {
      return;
    }
  m->stopping.store (true);
  std::uint64_t one = 1;
  (void) write (m->wakeup_fd, &one, sizeof (one));
  if (m->peek_thread.joinable ())
    {
      m->peek_thread.join ();
    }
  brd_Manager = NULL;
  /* channels/threads are detached; the process is exiting */
}

void
brd_park_client (SOCKET clt_sock_fd, const T_MAX_HEAP_NODE *job)
{
  manager *m = brd_Manager;
  if (m == NULL)
    {
      close (clt_sock_fd);
      return;
    }

  /* the ack the CAS used to send before reading db_info
   * (cas_common_main.c:179) now comes from the broker (B1-D7) */
  int zero = 0;
  if (send_all (clt_sock_fd, &zero, sizeof (int)) != 0)
    {
      close (clt_sock_fd);
      return;
    }
  m->shm->brd_num_accepted++;	/* receiver thread only (#116 D10) */

  if (IS_SSL_CLIENT (job->driver_info))
    {
      /* an SSL client's db_info is encrypted — no peek is possible.  Route
       * to the configured DIRECT_HANDOFF_SSL_DB with a synthesized db_info
       * (dbname only); the server terminates TLS and reads the real packet
       * itself (B2-D9).  Health checks are absorbed server-side too. */
      if (m->ssl_db.empty ())
	{
	  /* conf validation prevents this; fail the connect cleanly */
	  reject_client (*m, clt_sock_fd, CAS_ER_SSL_TYPE_NOT_ALLOWED, job->driver_info);
	  close (clt_sock_fd);
	  return;
	}
      parked_client *ssl_pc = new parked_client ();
      ssl_pc->fd = clt_sock_fd;
      ssl_pc->job = *job;
      ssl_pc->got = 0;
      ssl_pc->deadline = 0;
      std::memset (ssl_pc->db_info, 0, sizeof (ssl_pc->db_info));
      std::strncpy (ssl_pc->db_info, m->ssl_db.c_str (), 32);
      park_complete (*m, ssl_pc);
      return;
    }

  int flags = fcntl (clt_sock_fd, F_GETFL, 0);
  (void) fcntl (clt_sock_fd, F_SETFL, flags | O_NONBLOCK);

  parked_client *pc = new parked_client ();
  pc->fd = clt_sock_fd;
  pc->job = *job;
  pc->got = 0;
  pc->deadline = time (NULL) + DB_INFO_PEEK_TIMEOUT_SEC;

  {
    std::lock_guard<std::mutex> guard (m->park_inbox_mutex);
    m->park_inbox.push_back (pc);
  }
  std::uint64_t one = 1;
  (void) write (m->wakeup_fd, &one, sizeof (one));
}

void
brd_dispatch_job (T_MAX_HEAP_NODE *job)
{
  manager *m = brd_Manager;
  if (m == NULL)
    {
      return;
    }

  std::unique_ptr<char[]> db_info;
  {
    std::lock_guard<std::mutex> guard (m->db_info_mutex);
    auto it = m->db_info_by_job.find (job->id);
    if (it != m->db_info_by_job.end ())
      {
	db_info = std::move (it->second);
	m->db_info_by_job.erase (it);
      }
  }
  if (db_info == NULL)
    {
      reject_client (*m, job->clt_sock_fd, CAS_ER_FREE_SERVER, job->driver_info);
      close (job->clt_sock_fd);
      return;
    }

  char db_name[33];
  std::size_t len = strnlen (db_info.get (), 32);
  std::memcpy (db_name, db_info.get (), len);
  db_name[len] = '\0';

  for (;;)
    {
      /* admission wait: the driver keeps waiting in the queue's stead, same
       * as the idle-CAS wait it replaces (#117 D3) */
      while (m->slots_used.load () >= m->max_slots && !m->stopping.load ())
	{
	  std::this_thread::sleep_for (std::chrono::milliseconds (30));
	}
      if (m->stopping.load ())
	{
	  reject_client (*m, job->clt_sock_fd, CAS_ER_FREE_SERVER, job->driver_info);
	  close (job->clt_sock_fd);
	  return;
	}

      std::shared_ptr<channel> ch = channel_get_or_dial (*m, db_name);
      if (ch == NULL)
	{
	  /* server not up: immediate retryable rejection (#117 D5/D7) */
	  reject_client (*m, job->clt_sock_fd, CAS_ER_FREE_SERVER, job->driver_info);
	  close (job->clt_sock_fd);
	  return;
	}

      adopt::handoff_body body;
      std::memset (&body, 0, sizeof (body));
      std::memcpy (&body.client_ip, job->ip_addr, 4);
      body.client_port = job->port;
      body.access_mode = (std::uint8_t) m->shm->access_mode;
      body.replica_only = (std::uint8_t) (m->shm->replica_only_flag ? 1 : 0);
      body.slot_idx = 0;	/* per-slot identity retired with the CAS pool */
      /* broker-owned connect-reply facts (cas_bi_make_broker_info bytes 0-3);
       * the server overwrites its own bytes 4-7 (proto version, function
       * flags, system-param bits) when it builds the reply */
      body.broker_info[BROKER_INFO_DBMS_TYPE] = CAS_DBMS_CUBRID;
      body.broker_info[BROKER_INFO_KEEP_CONNECTION] = CAS_KEEP_CONNECTION_ON;
      body.broker_info[BROKER_INFO_STATEMENT_POOLING] =
	      m->shm->statement_pooling ? CAS_STATEMENT_POOLING_ON : CAS_STATEMENT_POOLING_OFF;
      body.broker_info[BROKER_INFO_CCI_PCONNECT] = m->shm->cci_pconnect ? CCI_PCONNECT_ON : CCI_PCONNECT_OFF;
      std::memcpy (body.driver_header, job->driver_info, adopt::DRIVER_HEADER_SIZE);
      std::memcpy (body.db_info, db_info.get (), adopt::DRIVER_DB_INFO_SIZE);

      adopt::msg_header reply_header;
      char reply_body[64];
      if (channel_request (*ch, adopt::msg_op::HANDOFF, &body, sizeof (body), NULL, 0, job->clt_sock_fd,
			   &reply_header, reply_body, sizeof (reply_body)) != 0)
	{
	  brd_debug ("handoff db=%s: request failed/timeout", db_name);
	  reject_client (*m, job->clt_sock_fd, CAS_ER_FREE_SERVER, job->driver_info);
	  close (job->clt_sock_fd);
	  return;
	}

      brd_debug ("handoff db=%s: reply op=%u len=%u", db_name, reply_header.op, reply_header.length);
      if ((adopt::msg_op) reply_header.op == adopt::msg_op::HANDOFF_ACK
	  && reply_header.length == sizeof (adopt::token_body))
	{
	  adopt::token_body ack;
	  std::memcpy (&ack, reply_body, sizeof (ack));
	  bool already_ended;
	  bool channel_died = false;
	  {
	    /* insert + acquire under the same lock tokens_drop_for_db holds
	     * for erase + release: either interleaving nets consistently.
	     * A dead channel means the reader already dropped this db's
	     * tokens/slots — a late insert would leak both (codex F4). */
	    std::lock_guard<std::mutex> guard (m->tokens_mutex);
	    already_ended = (m->orphan_ends.erase (std::make_pair (ack.token, std::string (db_name))) > 0);
	    channel_died = ch->dead.load ();
	    if (!already_ended && !channel_died)
	      {
		token_info ti;
		ti.db_name = db_name;
		std::memcpy (ti.clt_ip, job->ip_addr, 4);
		ti.clt_port = job->port;
		m->tokens.emplace (ack.token, ti);
		slots_acquire (*m, 1);
	      }
	  }
	  if (already_ended)
	    {
	      /* the session died before this thread processed the ACK; its
	       * SESSION_END was parked — the slot was never occupied (codex F1) */
	      brd_debug ("handoff db=%s: token=%u ended before ack", db_name, ack.token);
	    }
	  else if (channel_died)
	    {
	      /* the server (and the session) died between the ACK and here */
	      brd_debug ("handoff db=%s: token=%u acked on a dead channel", db_name, ack.token);
	    }
	  else
	    {
	      brd_debug ("handoff db=%s: token=%u stored (clt_port=%u)", db_name, ack.token, (unsigned) job->port);
	      m->shm->brd_num_handoffs++;
	    }
	  close (job->clt_sock_fd);	/* the server owns the connection now */
	  return;
	}

      if ((adopt::msg_op) reply_header.op == adopt::msg_op::HANDOFF_REJECT
	  && reply_header.length == sizeof (adopt::reject_body))
	{
	  adopt::reject_body reject;
	  std::memcpy (&reject, reply_body, sizeof (reject));
	  if ((adopt::reject_reason) reject.reason == adopt::reject_reason::CLIENTS_EXCEEDED)
	    {
	      /* server-side backstop tripped: wait and retry (#117 D3) */
	      std::this_thread::sleep_for (std::chrono::milliseconds (30));
	      continue;
	    }
	}

      reject_client (*m, job->clt_sock_fd, CAS_ER_FREE_SERVER, job->driver_info);
      close (job->clt_sock_fd);
      return;
    }
}

int
brd_cancel (unsigned int token, const unsigned char *clt_ip, unsigned short clt_port)
{
  manager *m = brd_Manager;
  if (m == NULL)
    {
      return -1;
    }

  std::string db_name;
  {
    std::lock_guard<std::mutex> guard (m->tokens_mutex);
    auto range = m->tokens.equal_range (token);
    if (range.first == range.second)
      {
	brd_debug ("cancel token=%u: unknown (table size %zu)", token, m->tokens.size ());
	return -1;
      }
    /* the anti-spoof check the pid scan used to make (broker.c:938-943)
     * doubles as the disambiguator when equal tokens are live for different
     * databases.  Preference order (codex F3 — OR-matching could route a
     * cancel to the wrong database's session): exact (ip AND port) first,
     * then ip-only (covers RESYNC-rebuilt entries, which carry port 0),
     * then the legacy lenient rule only when the token is unambiguous. */
    bool found = false;
    for (auto it = range.first; it != range.second; ++it)
      {
	if (clt_port > 0 && it->second.clt_port == clt_port && std::memcmp (it->second.clt_ip, clt_ip, 4) == 0)
	  {
	    db_name = it->second.db_name;
	    found = true;
	    break;
	  }
      }
    if (!found)
      {
	for (auto it = range.first; it != range.second; ++it)
	  {
	    if (std::memcmp (it->second.clt_ip, clt_ip, 4) == 0)
	      {
		db_name = it->second.db_name;
		found = true;
		break;
	      }
	  }
      }
    if (!found && std::next (range.first) == range.second)
      {
	auto it = range.first;
	if (! (clt_port > 0 && it->second.clt_port != clt_port && std::memcmp (it->second.clt_ip, clt_ip, 4) != 0))
	  {
	    db_name = it->second.db_name;
	    found = true;
	  }
      }
    if (!found)
      {
	brd_debug ("cancel token=%u: spoof check failed (got port %u)", token, (unsigned) clt_port);
	return -1;
      }
  }

  std::shared_ptr<channel> ch = channel_get_or_dial (*m, db_name);
  if (ch == NULL)
    {
      brd_debug ("cancel token=%u: no channel", token);
      return -1;
    }
  adopt::token_body body;
  body.token = token;
  return channel_send (*ch, adopt::msg_op::CANCEL, &body, sizeof (body)) == 0 ? 0 : -1;
}

int
brd_status (unsigned int token)
{
  manager *m = brd_Manager;
  if (m == NULL)
    {
      return FN_STATUS_NONE;
    }

  std::string db_name;
  {
    std::lock_guard<std::mutex> guard (m->tokens_mutex);
    auto it = m->tokens.find (token);
    if (it == m->tokens.end ())
      {
	return FN_STATUS_NONE;
      }
    /* equal tokens across databases: the ST probe carries no client address
     * to disambiguate with — first match, as the legacy pid scan behaved */
    db_name = it->second.db_name;
  }

  std::shared_ptr<channel> ch = channel_get_or_dial (*m, db_name);
  if (ch == NULL)
    {
      return FN_STATUS_NONE;
    }
  adopt::token_body body;
  body.token = token;
  adopt::msg_header reply_header;
  char reply_body[64];
  if (channel_request (*ch, adopt::msg_op::STATUS, &body, sizeof (body), NULL, 0, -1,
		       &reply_header, reply_body, sizeof (reply_body)) != 0
      || (adopt::msg_op) reply_header.op != adopt::msg_op::STATUS_REPLY
      || reply_header.length != sizeof (adopt::status_reply_body))
    {
      return FN_STATUS_NONE;
    }
  adopt::status_reply_body status;
  std::memcpy (&status, reply_body, sizeof (status));
  return status.fn_status;
}
