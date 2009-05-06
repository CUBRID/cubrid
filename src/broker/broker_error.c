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
 * broker_error.c - Error code and messages.
 *                  This implements functions to manipulate error codes and
 *                  messages.
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "broker_error.h"
#include "broker_filename.h"

#if defined(WINDOWS) && defined(DISPATCHER)
#include "reg_get.h"
#endif

static int get_error_msg (int err_code, char *msg_buf);

static int cur_error_code = UW_ER_NO_ERROR;
static int cur_os_errno = 0;

/*
 * name:	uw_set_error_code - set an error code
 *
 * arguments:	file_name - file name of the error source
 *		line_no - line number of the error source
 *		error_code - error code to set
 *		os_errno - kernel error code
 *
 * returns/side-effects:
 *		void
 *
 * description:	set the error_code to the current error code
 *
 * note: UniWeb maintains only one error code for the most recent
 *	 error status. That is, this function will ignore the possible
 *	 previous error condition.
 */
void
uw_set_error_code (const char *file_name, int line_no, int error_code,
		   int os_errno)
{
  cur_error_code = error_code;
  cur_os_errno = os_errno;
}

/*
 * name:	uw_get_error_code - get the current error code
 *
 * arguments:	void
 *
 * returns/side-effects:
 *		returns the current error code
 *
 * description:	returns the current error code
 *
 * note: UniWeb maintains only one error code for the most recent
 *	 error status. That is, this function will not give any infor
 *	 on possible previous error condition.
 */
int
uw_get_error_code (void)
{
  return (cur_error_code);
}

int
uw_get_os_error_code (void)
{
  return (cur_os_errno);
}

/*
 * name:	uw_get_error_message - get the current error message
 *
 * arguments:	void
 *
 * returns/side-effects:
 *		returns the current error message
 *
 * description:	returns the current error message
 *
 * note: UniWeb maintains only one error code for the most recent
 *	 error status. That is, this function will not give any infor
 *	 on possible previous error condition.
 */
const char *
uw_get_error_message (int error_code, int os_error_code)
{
  static char err_msg_buf[1024];
  char *p;
  char err_msg[1024];

  if (error_code < 0 || error_code > MAX_ERROR_CODE)
    return ("No such error code.");
  if (os_error_code == 0)
    {
      get_error_msg (error_code, err_msg_buf);
      return (err_msg_buf);
    }
  p = strerror (os_error_code);
  if (p == NULL)
    {
      get_error_msg (error_code, err_msg_buf);
      return (err_msg_buf);
    }
  get_error_msg (error_code, err_msg);
  sprintf (err_msg_buf, "%s (OS error code = %d, %s)",
	   err_msg, os_error_code, p);
  return (err_msg_buf);
}

const char *
uw_error_message (int error_code)
{
  static char err_msg[1024];

  if (error_code < 0 || error_code > MAX_ERROR_CODE)
    return ("No such error code.");
  else
    get_error_msg (error_code, err_msg);
  return (err_msg);
}

void
uw_error_message_r (int error_code, char *err_msg)
{
  if (error_code < 0 || error_code > MAX_ERROR_CODE)
    strcpy (err_msg, "No such error code.");
  else
    get_error_msg (error_code, err_msg);
}

void
uw_os_err_msg (int os_err_code, char *err_msg)
{
  if (os_err_code > 0)
    strcpy (err_msg, strerror (os_err_code));
  else
    err_msg[0] = '\0';
}

static int
get_error_msg (int err_code, char *msg_buf)
{
  FILE *fp;
  char *p;
  int i;
  char buf[1024];
  char default_err_msg_file[PATH_MAX];

  msg_buf[0] = '\0';

#ifdef DISPATCHER
  strcpy (default_err_msg_file, ERROR_MSG_FILE);
#else
  get_cubrid_file (FID_UV_ERR_MSG, default_err_msg_file);
#endif

#if defined(WINDOWS) && defined(DISPATCHER)
  if (reg_get (REG_ERR_MSG, buf, sizeof (buf)) < 0)
    return -1;
  fp = fopen (buf, "r");
  if (fp == NULL)
    return -1;
#else
  p = getenv ("UW_ER_MSG");
  if (p == NULL)
    {
      fp = fopen (default_err_msg_file, "r");
    }
  else
    {
      fp = fopen (p, "r");
      if (fp == NULL)
	{
	  fp = fopen (default_err_msg_file, "r");
	}
    }
  if (fp == NULL)
    {
      return -1;
    }
#endif

  for (i = 0; i < err_code; i++)
    {
      if (fgets (buf, sizeof (buf), fp) == NULL)
	{
	  fclose (fp);
	  return -1;
	}
    }

  if (fgets (buf, sizeof (buf), fp) == NULL)
    {
      fclose (fp);
      return -1;
    }

  p = strchr (buf, ':');
  if ((p == NULL) || (atoi (buf) != err_code))
    {
      fclose (fp);
      return -1;
    }
  if (*(p + 1) == ' ')
    p++;
  strcpy (msg_buf, p + 1);
  fclose (fp);
  return 0;
}
