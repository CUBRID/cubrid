/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * pccurses.c : stuffs for WINDOWS port
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <conio.h>

/* 
 * popen()
 * 
 * Note: 
 *   Supposed to open a pipe for use with the pager. Return stdout instead.  
 *   
 */
void *
popen ()
{
  return stdout;
}

/* 
 * getpass() - get a password
 *   return: password string
 *   prompt(in): prompt message string
 */
char *
getpass (const char *prompt)
{
  int pwlen = 0;
  int c;
  static char password_buffer[80];

  fprintf (stdout, prompt);

  while (1)
    {
      c = getch ();
      if (c == '\r' || c == '\n')
	break;
      if (c == '\b')
	{			/* backspace */
	  if (pwlen > 0)
	    pwlen--;
	  continue;
	}
      if (pwlen < sizeof (password_buffer) - 1)
	password_buffer[pwlen++] = c;
    }
  password_buffer[pwlen] = '\0';
  return password_buffer;
}
