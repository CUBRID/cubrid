/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
uw_set_error_code (const char *file_name, int line_no, int error_code, int os_errno)
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
  static char err_msg_buf[2048];
  char *p;
  char err_msg[1024];

  if (error_code > 0 || error_code < UW_MAX_ERROR_CODE)
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
  snprintf (err_msg_buf, sizeof (err_msg_buf) - 1, "%s (OS error code = %d, %s)", err_msg, os_error_code, p);
  return (err_msg_buf);
}

#if defined (ENABLE_UNUSED_FUNCTION)
const char *
uw_error_message (int error_code)
{
  static char err_msg[1024];

  if (error_code < 0 || error_code > UW_MAX_ERROR_CODE)
    return ("No such error code.");
  else
    get_error_msg (error_code, err_msg);
  return (err_msg);
}

void
uw_error_message_r (int error_code, char *err_msg)
{
  if (error_code < 0 || error_code > UW_MAX_ERROR_CODE)
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
#endif /* ENABLE_UNUSED_FUNCTION */

static int
get_error_msg (int err_code, char *msg_buf)
{
  FILE *fp;
  char *p;
  int i;
  char buf[1024];
  char default_err_msg_file[BROKER_PATH_MAX];

  msg_buf[0] = '\0';
  /* temporarily calculation */
  err_code = UW_MIN_ERROR_CODE - err_code;

#ifdef DISPATCHER
  strcpy (default_err_msg_file, ERROR_MSG_FILE);
#else
  get_cubrid_file (FID_UV_ERR_MSG, default_err_msg_file, BROKER_PATH_MAX);
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
