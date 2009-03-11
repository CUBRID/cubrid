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

#ifdef SOLARIS
#include <sys/sockio.h>
#endif

#include "broker_send_fd.h"

#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))
#endif

#define FALSE 0
#define TRUE 1
#define SYSV

int
send_fd (int server_fd, int client_fd, int rid)
{
  struct iovec iov[1];
  struct msghdr msg;
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
  static struct cmsghdr *cmptr = NULL;
#endif

  /* Pass the fd to the server */
  iov[0].iov_base = (char *) &rid;
  iov[0].iov_len = sizeof (int);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_namelen = 0;
  msg.msg_name = (caddr_t) 0;
#if !defined(LINUX) && !defined(ALPHA_LINUX) && !defined(UNIXWARE7)
  msg.msg_accrights = (caddr_t) & client_fd;
  msg.msg_accrightslen = sizeof (client_fd);
#else
  if (cmptr == NULL && (cmptr = malloc (CONTROLLEN)) == NULL)
    exit (99);
  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CONTROLLEN;
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
  *(int *) CMSG_DATA (cmptr) = client_fd;
#endif

  if (sendmsg (server_fd, &msg, 0) < 0)
    {
      return (FALSE);
    }
  return (TRUE);
}
