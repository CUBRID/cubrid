/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * oob.c - Support for Out Of Band messages
 *
 * OOB messages can be sent from the server to the client, or from the client
 * to the server. When the other side receives an OOB, an interrupt is
 * generated and the OOB handler is called by the system.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>

#include "porting.h"
#include "defs.h"
#include "general.h"
#include "connless.h"
#include "top.h"
#include "oob.h"
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
