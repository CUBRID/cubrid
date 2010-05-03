/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * repl_ag_sock.c : Define functions which are related to the communication
 *                  module. (The repl_agent side..)
 */

#ident "$Id$"

#include <arpa/inet.h>
#include <sys/socket.h>

#include "utility.h"
#include "repl_support.h"
#include "repl_agent.h"
#include "repl_tp.h"
#include "connection_cl.h"

#define NUM_OF_CONNECTION_TRIES  5

extern int debug_Dump_info;

static int ag_srv_sock;
static struct sockaddr_in ag_srv_sock_name;
static unsigned int ag_srv_sock_name_len;
static fd_set ag_srv_read_set;

static int repl_ag_srv_new_connection (void);

/*
 * repl_ag_sock_set_buf() - set the recv buffer size
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *    client_sock   : the socket descriptor
 *    size          : the size of receive buffer
 *
 * Note:
 *       set Recv buffer size of socket.
 *
 *       The default page size is 4K(4096), but sometimes the page size of
 *       master database can be 8K or some other values..
 *       We tries to set the send/recv buffer's max size as the same value of
 *       the master size if it's less than the pagesize.
 *       But, we don't know the page size of master db
 *       before we fetch the log header.
 *       So, we set the RECV/SEND buffer size as 4K for the log header fetch
 *       step. If the real pagesize is different with 4K, then we reset the
 *       buffer size.
 *
 *        called by RECV thread
 */
int
repl_ag_sock_reset_recv_buf (int client_sock, int size)
{
  int sock_opt_size = 0;
  int recv_buf_size = 0;
  int error = NO_ERROR;

  /* check the default value of recv buffer size */
  sock_opt_size = DB_SIZEOF (recv_buf_size);
  if (getsockopt (client_sock, SOL_SOCKET,
		  SO_RCVBUF, &recv_buf_size,
		  (socklen_t *) & sock_opt_size) < 0)
    REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);

  /*
   * if the size of recv buffer is smaller than the page size ,
   * change the size of buffer
   */
  if (recv_buf_size < size)
    {
      recv_buf_size = size;
      if (setsockopt (client_sock, SOL_SOCKET,
		      SO_RCVBUF, &recv_buf_size,
		      DB_SIZEOF (sock_opt_size)) < 0)
	REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
    }
  return error;
}

/*
 * repl_ag_sock_init() - Initialize the comm. stuffs of repl_agent
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx: index of master info array
 *
 * Note:
 *       Initialize client communications with the server over a socket.
 *
 *    called by RECV thread
 */
int
repl_ag_sock_init (int m_idx)
{
  MASTER_INFO *minfo;
  int error = NO_ERROR;
  int seconds = 1;

  minfo = mInfo[m_idx];

  /* Create the socket */
  if ((minfo->conn.client_sock =
       socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
    }

  error = repl_set_socket_tcp_nodelay (minfo->conn.client_sock);
  error = repl_ag_sock_reset_recv_buf (minfo->conn.client_sock,
				       minfo->io_pagesize);
  if (error != NO_ERROR)
    {
      return REPL_AGENT_SOCK_ERROR;
    }

  /* Make connection to the named server socket */
  minfo->conn.sock_name.sin_family = AF_INET;
  minfo->conn.sock_name.sin_addr.s_addr = inet_addr (minfo->conn.master_IP);
  minfo->conn.sock_name.sin_port = htons (minfo->conn.portnum);

  while (connect (minfo->conn.client_sock,
		  (struct sockaddr *) &(minfo->conn.sock_name),
		  DB_SIZEOF (minfo->conn.sock_name)) < 0)
    {
      if (minfo->pb->need_shutdown)
	{
	  REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_REPL_SERVER_CONNECT);
	}

      if (retry_Connect && (errno == ECONNREFUSED || errno == ENETUNREACH
			    || errno == EHOSTUNREACH))
	{
	  if (seconds % 60 == 0)
	    {
	      REPL_ERR_LOG (REPL_FILE_AG_TP, REPL_AGENT_REPL_SERVER_CONNECT);
	    }
	  sleep (1);
	  seconds++;
	}
      else
	{
	  REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_REPL_SERVER_CONNECT);
	}
    }

  minfo->conn.sock_name_len = DB_SIZEOF (minfo->conn.sock_name);
  return error;
}

/*
 * repl_ag_srv_sock_init() - Initialize the communication stuff of repl_agent
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *
 * Note:
 *     Initialize the communication stuffs of the repl_agent.
 *     create the initial socket, bind & listen...
 *
 *   called by MAIN Thread
 */
int
repl_ag_srv_sock_init (void)
{
  /* Initialize the connections db */
  FD_ZERO (&ag_srv_read_set);

  /* Create the socket to receive initial connection requests on */
  if ((ag_srv_sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);

  /*
   * get the send buffer size, if the size is smaller than the default
   * log page size, then we have to reset the send buffer size
   */
  if (repl_ag_sock_reset_recv_buf (ag_srv_sock, 1024) != NO_ERROR)
    return REPL_SERVER_SOCK_ERROR;

  ag_srv_sock_name_len = sizeof (ag_srv_sock_name);

  /* Bind the socket to the UNIX domain and a name */
  memset (&ag_srv_sock_name, 0, ag_srv_sock_name_len);
  ag_srv_sock_name.sin_family = AF_INET;
  ag_srv_sock_name.sin_port = htons (agent_status_port);
  ag_srv_sock_name.sin_addr.s_addr = htonl (INADDR_ANY);

  if ((bind (ag_srv_sock,
	     (struct sockaddr *) &ag_srv_sock_name,
	     sizeof (ag_srv_sock_name))) < 0)
    REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);

  /* Indicate to system to start listening on the socket */
  if ((listen (ag_srv_sock, 5)) < 0)
    REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);

  /* Add the socket to the server's set of active sockets */
  FD_SET (ag_srv_sock, &ag_srv_read_set);

  return NO_ERROR;
}

int
repl_ag_srv_wait_request (void)
{
  int error = NO_ERROR;
  fd_set read_selects;
  int nr;
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;

  /*
   * Set up the socket descriptor mask for the select.
   * copy srv_read_set, into the local copy
   */
  FD_ZERO (&read_selects);
  if (FD_ISSET (ag_srv_sock, &ag_srv_read_set))
    FD_SET (ag_srv_sock, &read_selects);

  /* Poll active connections using select() */
  if ((nr = select (FD_SETSIZE, &read_selects, (fd_set *) NULL,
		    (fd_set *) NULL, &timeout)) < 0)
    {
      return REPL_AGENT_SOCK_ERROR;
    }

  if (FD_ISSET (ag_srv_sock, &read_selects))
    {
      /* Handle the case of a new connection request on the named socket */
      error = repl_ag_srv_new_connection ();
    }

  return error;
}

static int
repl_ag_srv_new_connection (void)
{
  int i, j, k;
  int error = NO_ERROR;
  int ag_sock;
  int safe_pageid;
  int msg_length;
  char msg[1024];

  if ((ag_sock = accept (ag_srv_sock,
			 (struct sockaddr *) &ag_srv_sock_name,
			 &ag_srv_sock_name_len)) < 0)
    {
      REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
    }
  error = repl_ag_sock_reset_recv_buf (ag_sock, 1024);

  msg_length = sprintf (msg,
			"MasterID SlaveID ForRecovery Status FinalLSA ReplCnt CurRepl SavePageID\n");
  send (ag_sock, msg, msg_length, 0);
  for (i = 0; i < repl_Slave_num; i++)
    {
      for (j = 0; j < sInfo[i]->m_cnt; j++)
	{
	  for (k = 0; k < repl_Master_num; k++)
	    {
	      if (mInfo[k]->dbid == sInfo[i]->masters[j].m_id)
		{
		  safe_pageid = mInfo[k]->pb->max_pageid - 1;
		  sprintf (msg, "%8d %7d %11d %6c %4d,%4d %7d %7d %d\n",
			   sInfo[i]->masters[j].m_id,
			   sInfo[i]->masters[j].s_id,
			   sInfo[i]->masters[j].for_recovery,
			   sInfo[i]->masters[j].status,
			   sInfo[i]->masters[j].final_lsa.pageid,
			   sInfo[i]->masters[j].final_lsa.offset,
			   sInfo[i]->masters[j].repl_cnt,
			   sInfo[i]->masters[j].cur_repl, safe_pageid);
		  send (ag_sock, msg, strlen (msg), 0);
		  break;
		}
	    }
	}
    }

  close (ag_sock);

  return NO_ERROR;
}

/*
 * repl_ag_sock_send_request() - send a request to the repl_server
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx: index of master info array
 *
 * Note:
 *    call chain
 *        - repl_ag_get_log_header() <- repl_tr_log_recv()
 *        - repl_ag_sock_request_agent_info() <- repl_tr_log_recv()
 *    called by log RECV thread
 */
static int
repl_ag_sock_send_request (int m_idx)
{
  int sent_len;
  int remain_len = COMM_REQ_BUF_SIZE;
  MASTER_INFO *minfo = mInfo[m_idx];
  char *current_ptr = minfo->conn.req_buf;

  while (remain_len > 0)
    {
      sent_len = send (minfo->conn.client_sock, current_ptr, remain_len, 0);
      if (sent_len <= 0)
	{
	  REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
	}
      remain_len -= sent_len;
      current_ptr += sent_len;
    }

  return NO_ERROR;
}

/*
 * repl_ag_sock_get_response() - Get a response buffer from the connection
 *                               to the server.
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx: index of master info array
 *   result(out) : the result value -  REPL_REQUEST_FAIL or ...
 *
 * Note:
 *        When the RECV thread tries to get log header from the master,
 *        it sends the request to the server, and gets the result and
 *        data buffer.
 *
 *    called by log RECV thread
 */
static int
repl_ag_sock_get_response (int m_idx, int *result, bool * in_archive)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  int error = NO_ERROR;
  int rc = -1;
  int length = COMM_RESP_BUF_SIZE;
  int result_in_archive;

  *result = REPL_REQUEST_FAIL;

  /* get result and data */
  length = length + minfo->io_pagesize;
  rc = css_net_recv (minfo->conn.client_sock, minfo->conn.resp_buffer,
		     &length, -1);

  if (rc != RECORD_TRUNCATED && rc != NO_ERRORS)
    {
      *result = REPL_REQUEST_FAIL;
      REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
    }

  sscanf (minfo->conn.resp_buffer + minfo->io_pagesize, "%d %d",
	  result, &result_in_archive);
  *in_archive = result_in_archive == 1 ? true : false;

  if (*result == REPL_REQUEST_NOPAGE)
    {
      return NO_ERROR;
    }
  else if (*result != REPL_REQUEST_SUCCESS)
    {
      REPL_ERR_LOG (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
      return NO_ERROR;
    }

  return error;
}

/*
 * repl_ag_sock_recv_agent_info() - get the unique agent id from the respl_server
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx: index of master info array
 *
 * Note:
 *    call chain: repl_ag_sock_request_agent_info() <- repl_tr_log_recv()
 *    called by log RECV thread
 */
static int
repl_ag_sock_recv_agent_info (int m_idx)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  int error = NO_ERROR;
  int rc = -1;
  int length = COMM_RESP_BUF_SIZE;
  int io_pagesize;

  rc = css_net_recv (minfo->conn.client_sock, minfo->conn.resp_buffer,
		     &length, -1);
  if (rc != NO_ERRORS)
    {
      REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);
    }

  sscanf (minfo->conn.resp_buffer, "%d %d", &minfo->agentid, &io_pagesize);

  if (minfo->agentid == -1)
    {
      REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_INTERNAL_ERROR);
    }
  else
    {
      minfo->io_pagesize = io_pagesize;
    }

  return error;
}

/*
 * repl_ag_sock_shutdown() - Shutdown client communications
 *   return: none
 *   m_idx: index of master info array
 *
 * Note:
 *      close the socket
 *
 *    called by log recv thread
 */
void
repl_ag_sock_shutdown (int m_idx)
{
  close (mInfo[m_idx]->conn.client_sock);
}

/*
 * repl_ag_srv_sock_shutdown() - Shutdown client communications
 *   return: none
 *
 * Note:
 *      close the socket
 *
 *    called by MAIN thread
 */
void
repl_ag_srv_sock_shutdown ()
{
  close (ag_srv_sock);
}

/*
 * repl_ag_sock_request_next_log_page() - send a req. to the repl_server to
 *                                        get the target log page.
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *    m_idx      : index of the master info array
 *    pageid     : the target page id
 *    from_disk  : true if the caller wants to read from the disk
 *                 false if the caller wants to read from the buffer
 *    result(out): the result value
 *
 * Note:
 *   sends a request to the repl_server to get the target log page
 *
 *    called by log RECV thread
 */
int
repl_ag_sock_request_next_log_page (int m_idx, PAGEID pageid, bool from_disk,
				    int *result, bool * in_archive)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  int error = NO_ERROR;

  /* make a request */
  sprintf (minfo->conn.req_buf, "%d %d %d",
	   from_disk ? REPL_MSG_READ_LOG : REPL_MSG_GET_NEXT_LOG,
	   minfo->agentid, pageid);

  /* send a request */
  if (repl_ag_sock_send_request (m_idx) != NO_ERROR)
    {
      *result = REPL_REQUEST_FAIL;
      return REPL_AGENT_SOCK_ERROR;
    }

  /* receive the result */
  if (repl_ag_sock_get_response (m_idx, result, in_archive) != NO_ERROR)
    {
      if (debug_Dump_info & REPL_DEBUG_ERROR_DETAIL)
	{
	  char msg[32];

	  snprintf (msg, 32, "NO PAGE: %d", pageid);
	  REPL_ERR_LOG_ONE_ARG (REPL_FILE_AG_SOCK, REPL_AGENT_INFO_MSG, msg);
	}
      *result = REPL_REQUEST_FAIL;
      return REPL_AGENT_SOCK_ERROR;
    }
  return error;
}

/*
 * repl_ag_sock_request_log_hdr() - sends a request to the repl_server to
 *                                  get the log header page
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx : index of master info array
 *
 * Note:
 *    called by log RECV thread
 */
int
repl_ag_sock_request_log_hdr (int m_idx)
{
  int error = NO_ERROR;
  int result = REPL_REQUEST_SUCCESS;
  MASTER_INFO *minfo = mInfo[m_idx];
  bool in_archive;

  /* make a request */
  sprintf (minfo->conn.req_buf, "%d %d %d", REPL_MSG_GET_LOG_HEADER,
	   minfo->agentid, minfo->copy_log.last_pageid);

  /* send req. to the repl_server */
  if ((error = repl_ag_sock_send_request (m_idx)) != NO_ERROR)
    {
      return error;
    }

  /* get the result */
  if ((error = repl_ag_sock_get_response (m_idx, &result,
					  &in_archive)) != NO_ERROR)
    return error;

  if (result == REPL_REQUEST_FAIL)
    REPL_ERR_RETURN (REPL_FILE_AG_SOCK, REPL_AGENT_SOCK_ERROR);

  return error;
}

/*
 * repl_ag_sock_request_agent_info() - Get the agent id
 *   return: NO_ERROR or REPL_SOCK_ERROR
 *   m_idx: the array index of the target master info
 *
 * Note:
 *      request the unique agent id to the repl_server
 *
 *    call chain: RECV
 *    called by RECV thread
 *    No one deosn't need to consider "mutex lock"
 */
int
repl_ag_sock_request_agent_info (int m_idx)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  int error = NO_ERROR;

  /* get the agent id from the server */
  /* step1 : make a request */
  sprintf (minfo->conn.req_buf, "%d %d", REPL_MSG_GET_AGENT_ID,
	   agent_status_port);

  /* step2 : send a request to repl_server */
  if ((error = repl_ag_sock_send_request (m_idx)) != NO_ERROR)
    return error;

  /* step3 : receive the result from repl_server */
  if ((error = repl_ag_sock_recv_agent_info (m_idx)) != NO_ERROR)
    {
      return error;
    }

  return error;
}
