/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * as_util.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cas_common.h"
#include "as_util.h"
#include "env_str_def.h"
#ifndef LIBCAS_FOR_JSP
#include "error.h"
#endif
#include "setup.h"
#include "file_name.h"
#include "er_html.h"

static char db_err_log_file[PATH_MAX];
static char as_pid_file_name[PATH_MAX] = "";

#ifndef CAS
#ifdef UTS_WWW
void
uw_ht_error_message (int error_code, int os_errno)
{
  char *http_host = getenv ("HTTP_HOST");
  char *script_name = getenv ("SCRIPT_NAME");
  char msg_buf[1024];

  if (http_host && script_name)
    {
      sprintf (msg_buf,
	       "Location: http://%s%s/error_msg?error_code=%d&os_error_code=%d\n\n",
	       http_host, script_name, error_code, os_errno);
      uw_write_to_client (msg_buf, strlen (msg_buf));
    }
  else
    {
      char *er_msg = NULL;
      char html_file[FILE_NAME_LEN];

      sprintf (msg_buf, "Content-type: text/html\n\n");
      uw_write_to_client (msg_buf, strlen (msg_buf));

      get_cubrid_file (FID_ER_HTML, html_file);
      if (read_er_html (html_file, error_code, os_errno, &er_msg) >= 0)
	{
	  uw_write_to_client (er_msg, strlen (er_msg));
	  FREE_MEM (er_msg);
	}
      else
	{
	  sprintf (msg_buf,
		   "<HTML>\n<HEAD>\n<TITLE> Error message from UniWeb </TITLE>\n</HEAD>\n");
	  uw_write_to_client (msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<BODY>\n<H2> Error message from UniWeb </H2>\n");
	  uw_write_to_client (msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<P> The error code is %d.\n", error_code);
	  uw_write_to_client (msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<P> %s\n",
		   uw_get_error_message (error_code, os_errno));
	  uw_write_to_client (msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "</BODY>\n</HTML>\n");
	  uw_write_to_client (msg_buf, strlen (msg_buf));
	}
    }
}
#else
void
uw_ht_error_message (int error_code, int os_errno)
{
  char *out_file_name;
  char *delimiter;
  char write_buf[2048];

  out_file_name = (char *) getenv (OUT_FILE_NAME_ENV_STR);
  delimiter = (char *) getenv (DELIMITER_ENV_STR);
  if (out_file_name != NULL)
    {
      V3_WRITE_HEADER_ERR ();
      sprintf (write_buf, "%s\n",
	       uw_get_error_message (error_code, os_errno));
      uw_write_to_client (write_buf, strlen (write_buf));
    }
  else if (delimiter != NULL)
    {
      sprintf (write_buf, "%s-1%s-1%s%s\n", delimiter, delimiter, delimiter,
	       uw_get_error_message (error_code, os_errno));
      uw_write_to_client (write_buf, strlen (write_buf));
    }
}
#endif
#endif /* ifndef CAS */

void
as_pid_file_create (char *br_name, int as_index)
{
  FILE *fp;
  char buf[PATH_MAX];

  sprintf (as_pid_file_name, "%s%s_%d.pid",
	   get_cubrid_file (FID_AS_PID_DIR, buf), br_name, as_index + 1);
  fp = fopen (as_pid_file_name, "w");
  if (fp)
    {
      fprintf (fp, "%d\n", (int) getpid ());
      fclose (fp);
    }
}

void
as_db_err_log_set (char *br_name, int as_index)
{
  char buf[PATH_MAX];

  sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d.err",
	   get_cubrid_file (FID_CUBRID_ERR_DIR, buf), br_name, as_index + 1);
  putenv (db_err_log_file);
}

int
as_get_my_as_info (char *br_name, int *as_index)
{
  char *p, *q, *dir_p;

  p = getenv (PORT_NAME_ENV_STR);
  if (p == NULL)
    return -1;

  q = strrchr (p, '.');
  if (q == NULL)
    return -1;

  dir_p = strrchr (p, '/');
  if (dir_p == NULL)
    return -1;

  *q = '\0';
  strcpy (br_name, dir_p + 1);
  *as_index = atoi (q + 1) - 1;
  *q = '.';

  return 0;
}
