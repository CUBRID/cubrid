/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * recv_fd.c - 
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

#include "recv_fd.h"

#if defined(SOLARIS) || defined(UNIXWARE7)
#include <sys/sockio.h>
#elif defined(AIX) || defined(OSF1) || defined(LINUX) || defined(ALPHA_LINUX)
#include <sys/ioctl.h>
#endif

#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))
#endif

#define FALSE 0
#define TRUE 1
#define SYSV

int
recv_fd (int fd, int *rid)
{
  int req_id;
  int new_fd = 0, rc;
  struct iovec iov[1];
  struct msghdr msg;
  int pid;
#if defined(LINUX) || defined(ALPHA_LINUX) || defined(UNIXWARE7)
  static struct cmsghdr *cmptr = NULL;
#endif

  iov[0].iov_base = (char *) &req_id;
  iov[0].iov_len = sizeof (int);
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

  if ((rc = recvmsg (fd, &msg, 0)) < 0)
    {
#ifdef _DEBUG
      printf ("recvmsg failed. errno = %d. str=%s\n", errno,
	      strerror (errno));
#endif
      return (-1);
    }

  *rid = req_id;

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
