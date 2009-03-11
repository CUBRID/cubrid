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
 * dl_daemon.c - Daemon that scavenges for dynamic loader temp files.
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <sys/param.h>
#include <errno.h>
#include <unistd.h>

#if defined(SIGTSTP)
#include <sys/file.h>
#include <sys/ioctl.h>
#endif /* SIGTSTP */

#include "porting.h"

static char dynload_Temporary_filename[PATH_MAX] = "";

static void dynload_install_handlers (void);
static void dynload_remove_temporary_file (void);

/*
 * dynload_remove_temporary_file() - temporary file check and remove
 *   return: none
 */
static void
dynload_remove_temporary_file (void)
{
  if (dynload_Temporary_filename[0] != 0)
    {
      int ret = access (dynload_Temporary_filename, R_OK);
      if (ret == 0)
	{
	  unlink (dynload_Temporary_filename);
	}
    }
}


/*
 * dynload_install_handlers() - Ignore the terminal stop signals.
 *   return: none
 */
static void
dynload_install_handlers (void)
{
#if defined(SIGTTOU)
  (void) os_set_signal_handler (SIGTTOU, SIG_IGN);
#endif /* SIGTTOU */
#if defined(SIGTTIN)
  (void) os_set_signal_handler (SIGTTIN, SIG_IGN);
#endif /* SIGTTIN */
#if defined(SIGTSTP)
  (void) os_set_signal_handler (SIGTSTP, SIG_IGN);
#endif /* SIGTSTP */

  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
}


/*
 * Whenever the dynamic loader changes its temp file (the one used to maintain
 * symbol table information), it informs the daemon. If the dynamic loader's
 * process dies before removing the file, the daemon will notice the fact and
 * remove the file itself.
 */

void
main (void)
{
  dynload_install_handlers ();

  while (read (0, dynload_Temporary_filename,
	       sizeof (dynload_Temporary_filename)) != 0)
    {
      ;				/* it reads from PIPE until the parent is ended */
    }

  dynload_remove_temporary_file ();
  exit (0);
}
