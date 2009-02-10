/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * daemon.c : daemonize a process for master
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "porting.h"
#include "error_manager.h"

/* 
 * css_daemon_start() - detach a process from login session context
 *   return: none
 */
void
css_daemon_start (void)
{
  int childpid, fd;
#if defined(HAVE_GETRLIMIT)
  struct rlimit rlp;
#endif /* HAVE_GETRLIMIT */
  int fd_max;
  int ppid = getpid ();


  /* If we were started by init (process 1) from the /etc/inittab file
   * there's no need to detatch.
   * This test is unreliable due to an unavoidable ambiguity
   * if the process is started by some other process and orphaned
   * (i.e., if the parent process terminates before we get here).
   */

  if (getppid () == 1)
    goto out;

  /*
   * Ignore the terminal stop signals (BSD).
   */

#ifdef SIGTTOU
  if (os_set_signal_handler (SIGTTOU, SIG_IGN) == SIG_ERR)
    {
      exit (0);
    }
#endif
#ifdef SIGTTIN
  if (os_set_signal_handler (SIGTTIN, SIG_IGN) == SIG_ERR)
    {
      exit (0);
    }
#endif
#ifdef SIGTSTP
  if (os_set_signal_handler (SIGTSTP, SIG_IGN) == SIG_ERR)
    {
      exit (0);
    }
#endif

  /*
   * Call fork and have the parent exit.
   * This does several things. First, if we were started as a simple shell 
   * command, having the parent terminate makes the shell think that the 
   * command is done. Second, the child process inherits the process group ID 
   * of the parent but gets a new process ID, so we are guaranteed that the 
   * child is not a process group leader, which is a prerequirement for the 
   * call to setsid
   */

  if ((childpid = fork ()) < 0)
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);
  else if (childpid > 0)
    {
      exit (0);			/* parent goes bye-bye */
    }
  else
    {
      /*
       * Wait until the parent process has finished. Coded with polling since
       * the parent should finish immediately. SO, it is unlikely that we are
       * going to loop at all.
       */
      while (getppid () == ppid)
	{
	  sleep (1);
	}
    }

  /*
   * Create a new session and make the child process the session leader of 
   * the new session, the process group leader of the new process group. 
   * The child process has no controlling terminal.
   */

  if (os_set_signal_handler (SIGHUP, SIG_IGN) == SIG_ERR)
    {
      exit (0);			/* fail to immune from pgrp leader death */
    }

  setsid ();

out:

  /* 
   * Close unneeded file descriptors which prevent the daemon from holding 
   * open any descriptors that it may have inherited from its parent which
   * could be a shell. For now, leave in/out/err open
   */

#if defined (HAVE_GETRLIMIT)
  fd_max = 0;
  if (getrlimit (RLIMIT_NOFILE, &rlp) == 0)
    fd_max = MIN (1024, rlp.rlim_cur);
#elif defined (OPEN_MAX)
  fd_max = OPEN_MAX;
#elif defined(HAVE_SYSCONF) && defined (_SC_OPEN_MAX)
  fd_max = sysconf (_SC_OPEN_MAX);
#else
#error "There's no known way to get the maxium number of file descriptors!"
#endif
  for (fd = 3; fd < fd_max; fd++)
    close (fd);

  errno = 0;			/* Reset errno from last close */


#if 0
  chdir ("/");
#endif

  /*
   * The file mode creation mask that is inherited could be set to deny 
   * certain permissions. Therfore, clear the file mode creation mask.
   */

  umask (0);
}
