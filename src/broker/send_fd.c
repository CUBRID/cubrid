/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * send_fd.c - 
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

#include "send_fd.h"

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
