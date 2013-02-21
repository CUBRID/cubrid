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

#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
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
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
  static struct cmsghdr *cmptr = NULL;
#endif
  struct sendmsg_s send_msg;

  iov[0].iov_base = (char *) &send_msg;
  iov[0].iov_len = sizeof (struct sendmsg_s);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = (caddr_t) NULL;
  msg.msg_namelen = 0;
#if !defined(LINUX) && !defined(ALPHA_LINUX) && !defined(UNIXWARE7)
  msg.msg_accrights = (caddr_t) & new_fd;	/* address of descriptor */
  msg.msg_accrightslen = sizeof (new_fd);	/* receive 1 descriptor */
#else
  if (cmptr == NULL && (cmptr = malloc (CONTROLLEN)) == NULL)
    exit (99);
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
#endif
  rc = recvmsg (fd, &msg, 0);

  if (rc < (signed int) (sizeof (int)))
    {
#ifdef _DEBUG
      printf ("recvmsg failed. errno = %d. str=%s\n", errno,
	      strerror (errno));
#endif
      return (-1);
    }

  *rid = send_msg.rid;
  if (driver_info)
    {
      memcpy (driver_info, send_msg.driver_info, SRV_CON_CLIENT_INFO_SIZE);
    }

  pid = getpid ();
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
  new_fd = *(int *) CMSG_DATA (cmptr);
#endif

#ifdef SYSV
  ioctl (new_fd, SIOCSPGRP, (caddr_t) & pid);
#elif !defined(VMS)
  fcntl (new_fd, F_SETOWN, pid);
#endif

  return (new_fd);
}
