/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "php.h"

int get_error_msg (int err_code, char *err_msg);
static int will_skip (char *str);
static int proc_err (char *str, int *code, char **msg);

int
get_error_msg (int err_code, char *err_msg)
{
  FILE *fp;
  char *err_path;
  char msg_file_path[256];
  char buf[1000];
  int code;
  char *msg;
  int i;

  // cubrid.err_path
  cfg_get_string ("cubrid.err_path", &err_path);
  if (!err_path)
    {
      strcpy (err_msg, "Cannot open error message");
      return 0;
    }

  sprintf (msg_file_path, "%s/cubrid_err.msg", err_path);
  if ((fp = fopen (msg_file_path, "rt")) == NULL)
    {
      return -1;
    }

  while (!feof (fp))
    {
      fgets (buf, 999, fp);
      if (!will_skip (buf))
	{
	  proc_err (buf, &code, &msg);
	  if (err_code == code)
	    {
	      i = 0;
	      while (msg[i])
		{
		  if (msg[i] == '\r' || msg[i] == '\n')
		    {
		      msg[i] = '\0';
		      break;
		    }
		  i++;
		}
	      strcpy (err_msg, msg);
	      break;
	    }
	}
    }
  fclose (fp);
  return 0;
}

int
will_skip (char *str)
{
  int i;
  int len;

  len = strlen (str);
  for (i = 0; i < len; i++)
    {
      if (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n')
	continue;
      if (str[i] == '#')
	{
	  return 1;
	}
      else
	{
	  return 0;
	}
    }
  return 1;
}

int
proc_err (char *str, int *code, char **msg)
{
  int i;
  int len;
  char code_buf[10];

  len = strlen (str);
  for (i = 0; i < len; i++)
    {
      if (str[i] == ' ' || str[i] == '\t')
	{
	  strncpy (code_buf, str, i + 1);
	  *code = atoi (code_buf);
	  while (str[i] == ' ' || str[i] == '\t')
	    i++;
	  *msg = str + i;
	  return 0;
	}
    }
  return -1;
}
