/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * dl_daemon.c - Daemon that scavenges for dynamic loader temp files.
 * This program is builded in bin/dl_daemon and spawned by dl_load_and_resolve.
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
