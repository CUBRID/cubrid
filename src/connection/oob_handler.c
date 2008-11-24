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
 * oob_handler.c - Support for Out Of Band messages
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>

#include "porting.h"
#include "connection_defs.h"
#include "connection_cl.h"
#include "connection_less.h"
#include "client_support.h"
#include "oob_handler.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

static OOB_HANDLER_FUNCTION oob_Handlers[OOB_LAST_ENUM] = { NULL };
static void (*oob_Previous_sigurg_handler) (int) = NULL;

#if !defined(WINDOWS)
static void css_oob_handler (int sig);
#endif

/*
 * css_set_oob_handler() - register a handler function for a specific OOB
 *                         message
 *   return: void
 *   oob_type(in): The enum type of the OOB message
 *   handler(in): The function to call when oob_type message arrives
 */
void
css_set_oob_handler (OOB_ENUM_TYPE oob_type, OOB_HANDLER_FUNCTION handler)
{
  oob_Handlers[oob_type] = handler;
}

#if !defined(WINDOWS)
/*
 * css_oob_handler() - registered with the system to handle the SIGURG signal
 *   return: void
 *   sig(in): signal to handle
 *
 * Note: It will call the client function defined by the css_set_oob_handler
 *       if one exists, otherwise it will call the default handler.
 */
static void
css_oob_handler (int sig)
{
  CSS_CONN_ENTRY *conn;
  unsigned int eid;
  char interrupt_byte = '\0';

  if (sig != SIGURG)
    {
      return;
    }

  conn = css_find_exception_conn ();
  if (conn != NULL)
    {
      if (css_read_broadcast_information (conn->fd, &interrupt_byte))
	{
	  eid = css_return_eid_from_conn (conn, &css_Client_anchor, 0);
	  if (eid != 0)
	    {
	      if (interrupt_byte <= OOB_LAST_ENUM
		  && oob_Handlers[(unsigned int) interrupt_byte])
		{
		  (*oob_Handlers[(unsigned int) interrupt_byte]) (eid);
		  return;
		}
	    }
	}
    }
  else if (oob_Previous_sigurg_handler != NULL)
    {
      (*oob_Previous_sigurg_handler) (sig);
    }
}
#endif

/*
 * css_init_oob_handler() - initializes the signal handler
 *   return: void
 *
 * Note: It also saves the previous signal handler in case the SIGURG was not
 *       from a comm system socket.
 */
void
css_init_oob_handler (void)
{
#if !defined(WINDOWS)
  oob_Previous_sigurg_handler =
    os_set_signal_handler (SIGURG, css_oob_handler);

  /* reset if previously ignored */
  if (oob_Previous_sigurg_handler == SIG_IGN)
    {
      oob_Previous_sigurg_handler = NULL;
    }
#endif /* !WINDOWS */
}
