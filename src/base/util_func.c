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
 * util_func.c : miscellaneous utility functions
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include "util_func.h"
#include "porting.h"

/*
 * hashpjw() - returns hash value of given string
 *   return: hash value
 *   s(in)
 *
 * Note:
 *   This function is adapted from the hashpjw function in Aho, Sethi, and
 *   Ullman (Red Dragon), p. 436.  Unlike that version, this one does not
 *   mod the result; consequently, the result could be any 32-bit value.
 *   The caller should mod the result with a value appropriate for its
 *   environment.
 *
 */
unsigned int
hashpjw (const char *s)
{
  unsigned int h, g;

  assert (s != NULL);

  for (h = 0; *s != '\0'; ++s)
    {
      h = (h << 4) + (*s);

      g = (h & 0xf0000000);

      if (g != 0)
	{
	  h ^= g >> 24;
	  h ^= g;
	}
    }
  return h;
}

/*
 * util_compare_filepath -
 *   return:
 *   file1(in):
 *   file2(in):
 */
int
util_compare_filepath (const char *file1, const char *file2)
{
#if defined (WINDOWS)
  char path1[PATH_MAX], path2[PATH_MAX];
  char *p;

  if (GetLongPathName (file1, path1, sizeof (path1)) == 0
      || GetLongPathName (file2, path2, sizeof (path2)) == 0)
    {
      return (stricmp (file1, file2));
    }

  for (p = path1; *p; p++)
    if (*p == '/')
      *p = '\\';
  for (p = path2; *p; p++)
    if (*p == '/')
      *p = '\\';

  return (stricmp (path1, path2));
#else /* WINDOWS */
  return (strcmp (file1, file2));
#endif /* !WINDOWS */
}

/*
 * Signal Handling
 */

static void system_interrupt_handler (int sig);
static void system_quit_handler (int sig);

/*
 * user_interrupt_handler, user_quit_handler -
 *   These variables contain pointers to the user specified handler
 *   functions for the interrupt and quit signals.
 *   If they are NULL, no handlers have been defined.
 */
static SIG_HANDLER user_interrupt_handler = NULL;
static SIG_HANDLER user_quit_handler = NULL;

/*
 * system_interrupt_handler - Internal system handler for SIGINT
 *   return: none
 *   sig(in): signal no
 *
 * Note:  Calls the user interrupt handler after re-arming the signal handler.
 */
static void
system_interrupt_handler (int sig)
{
  (void) os_set_signal_handler (SIGINT, system_interrupt_handler);
  if (user_interrupt_handler != NULL)
    {
      (*user_interrupt_handler) ();
    }
}


/*
 * system_quit_handler - Internal system handler for SIGQUIT
 *   return: none
 *   sig(in): signal no
 *
 * Note: Calls the user quit handler after re-arming the signal handler.
 */
static void
system_quit_handler (int sig)
{
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGQUIT, system_quit_handler);
  if (user_quit_handler != NULL)
    (*user_quit_handler) ();
#endif
}

/*
 * util_disarm_signal_handlers - Disarms the user interrpt and quit handlers
 *                               if any were specified
 *   return: none
 */
void
util_disarm_signal_handlers (void)
{
  if (user_interrupt_handler != NULL)
    {
      user_interrupt_handler = NULL;
      if (os_set_signal_handler (SIGINT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGINT, SIG_DFL);
	}
    }
#if !defined(WINDOWS)
  if (user_quit_handler != NULL)
    {
      user_quit_handler = NULL;
      if (os_set_signal_handler (SIGQUIT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGQUIT, SIG_DFL);
	}
    }
#endif
}

/*
 * util_arm_signal_handlers - Install signal handlers for the two most
 *                            important signals SIGINT and SIGQUIT
 *   return: none
 *   sigint_handler(in): SIGINT signal handler
 *   sigquit_handler(in): SIGQUIT signal handler
 */
void
util_arm_signal_handlers (SIG_HANDLER sigint_handler,
			  SIG_HANDLER sigquit_handler)
{
  /* first disarm any existing handlers */
  util_disarm_signal_handlers ();

  if (sigint_handler != NULL)
    {
      if (os_set_signal_handler (SIGINT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGINT, system_interrupt_handler);
	  user_interrupt_handler = sigint_handler;
	}
    }
#if !defined(WINDOWS)
  if (sigquit_handler != NULL)
    {
      /* Is this kind of test necessary for the quit signal ? */
      if (os_set_signal_handler (SIGQUIT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGQUIT, system_quit_handler);
	  user_quit_handler = sigquit_handler;
	}
    }
#endif
}
