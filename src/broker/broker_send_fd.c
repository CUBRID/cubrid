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


/*
 * broker_send_fd.c -
 */

#ident "$Id$"

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#ifdef SOLARIS
#include <sys/sockio.h>
#endif

#include "porting.h"
#include "cas_protocol.h"
#include "broker_send_fd.h"
#include "broker_send_recv_msg.h"

#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(AIX)
#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))
#endif

#define SYSV

int
send_fd (int server_fd, int client_fd, int rid, char *driver_info)
{
  struct iovec iov[1];
  struct msghdr msg;
  int num_bytes;
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(AIX)
  static struct cmsghdr *cmptr = NULL;
#endif
  struct sendmsg_s send_msg;

  assert (driver_info != NULL);

  /* set send message */
  send_msg.rid = rid;
  memcpy (send_msg.driver_info, driver_info, SRV_CON_CLIENT_INFO_SIZE);

  /* Pass the fd to the server */
  iov[0].iov_base = (char *) &send_msg;
  iov[0].iov_len = sizeof (struct sendmsg_s);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_namelen = 0;
  msg.msg_name = (caddr_t) 0;
#if !defined(LINUX) && !defined(ALPHA_LINUX) && !defined(UNIXWARE7) && !defined(AIX)
  msg.msg_accrights = (caddr_t) & client_fd;
  msg.msg_accrightslen = sizeof (client_fd);
#else
  if (cmptr == NULL && (cmptr = (cmsghdr *) malloc (CONTROLLEN)) == NULL)
    exit (99);
  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CONTROLLEN;
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
  *(int *) CMSG_DATA (cmptr) = client_fd;
#endif

  num_bytes = sendmsg (server_fd, &msg, 0);

  if (num_bytes < (signed int) sizeof (int))
    {
      return (-1);
    }
  return (num_bytes);
}
