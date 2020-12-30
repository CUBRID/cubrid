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
 * broker_recv_fd.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>

#include "porting.h"
#include "cas_protocol.h"
#include "broker_recv_fd.h"
#include "broker_send_recv_msg.h"

#if defined(SOLARIS) || defined(UNIXWARE7)
#include <sys/sockio.h>
#elif defined(AIX) || defined(OSF1) || defined(LINUX) || defined(ALPHA_LINUX)
#include <sys/ioctl.h>
#endif

#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(AIX)
#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))
#endif

#define SYSV

/*
   client_version is only used in PROXY(SHARD).
   In CAS, client_version is set in shared memory.
 */
int
recv_fd (int fd, int *rid, char *driver_info)
{
  int new_fd = 0, rc;
  struct iovec iov[1];
  struct msghdr msg;
  int pid;
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(AIX)
  static struct cmsghdr *cmptr = NULL;
#endif
  struct sendmsg_s send_msg;

  iov[0].iov_base = (char *) &send_msg;
  iov[0].iov_len = sizeof (struct sendmsg_s);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = (caddr_t) NULL;
  msg.msg_namelen = 0;
#if !defined(LINUX) && !defined(ALPHA_LINUX) && !defined(UNIXWARE7) && !defined(AIX)
  msg.msg_accrights = (caddr_t) & new_fd;	/* address of descriptor */
  msg.msg_accrightslen = sizeof (new_fd);	/* receive 1 descriptor */
#else
  if (cmptr == NULL && (cmptr = (cmsghdr *) malloc (CONTROLLEN)) == NULL)
    exit (99);
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
#endif
  rc = recvmsg (fd, &msg, 0);

  if (rc < (signed int) (sizeof (int)))
    {
#ifdef _DEBUG
      printf ("recvmsg failed. errno = %d. str=%s\n", errno, strerror (errno));
#endif
      return (-1);
    }

  *rid = send_msg.rid;
  if (driver_info)
    {
      memcpy (driver_info, send_msg.driver_info, SRV_CON_CLIENT_INFO_SIZE);
    }

  pid = getpid ();
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(AIX)
  new_fd = *(int *) CMSG_DATA (cmptr);
#endif

#ifdef SYSV
  ioctl (new_fd, SIOCSPGRP, (caddr_t) & pid);
#elif !defined(VMS)
  fcntl (new_fd, F_SETOWN, pid);
#endif

  return (new_fd);
}
