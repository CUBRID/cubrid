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
 * broker_log_util.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(WINDOWS)
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "broker_log_util.h"
#include "cas_cci.h"

static bool is_bind_with_size (char *buf, int *tot_val_size, int *info_size);

char *
ut_trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str; *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'); s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memmove (str, s, strlen (s) + 1);

  return (str);
}

void
ut_tolower (char *str)
{
  char *p;

  if (str == NULL)
    return;

  for (p = str; *p; p++)
    {
      if (*p >= 'A' && *p <= 'Z')
	*p = *p - 'A' + 'a';
    }
}

#if defined(BROKER_LOG_RUNNER)
static bool
is_bind_with_size (char *buf, int *tot_val_size, int *info_size)
{
  char *p;
  int type;
  int size;

  if (tot_val_size)
    {
      *tot_val_size = 0;
    }
  if (info_size)
    {
      *info_size = 0;
    }

  if (strncmp (buf, "B ", 1) != 0)
    {
      return false;
    }

  type = atoi (buf + 2);
  if ((type != CCI_U_TYPE_CHAR) && (type != CCI_U_TYPE_STRING) && (type != CCI_U_TYPE_NCHAR)
      && (type != CCI_U_TYPE_VARNCHAR) && (type != CCI_U_TYPE_BIT) && (type != CCI_U_TYPE_VARBIT)
      && (type != CCI_U_TYPE_ENUM) && (type != CCI_U_TYPE_JSON))
    {
      return false;
    }

  p = strchr (buf + 2, ' ');
  if (p == NULL)
    {
      goto error_on_val_size;
    }

  size = atoi (p);
  p = strchr (p + 1, ' ');
  if (p == NULL)
    {
      goto error_on_val_size;
    }

  if (info_size)
    {
      *info_size = (char *) (p + 1) - (char *) buf;
    }

  switch (type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
      {
	int len = strlen (p + 1);

	if (p[len] == '\n')
	  {
	    p[len] = 0;
	    len--;
	  }

	if (tot_val_size)
	  {
	    *tot_val_size = len + 1;
	  }
      }
      break;
    default:
      if (tot_val_size)
	{
	  *tot_val_size = size;
	}
      break;
    }

  return true;

error_on_val_size:
  if (tot_val_size)
    {
      *tot_val_size = -1;
    }
  return false;
}
#else /* BROKER_LOG_RUNNER */
static bool
is_bind_with_size (char *buf, int *tot_val_size, int *info_size)
{
  char *msg;
  char *p, *q;
  char size[256] = { 0, };
  char *value_p;
  char *size_begin;
  char *size_end;
  char *info_end;
  int len;

  if (info_size)
    {
      *info_size = 0;
    }
  if (tot_val_size)
    {
      *tot_val_size = 0;
    }

  msg = get_msg_start_ptr (buf);
  if (strncmp (msg, "bind ", 5) != 0)
    {
      return false;
    }

  p = strchr (msg, ':');
  if (p == NULL)
    {
      return false;
    }
  p += 2;

  if ((strncmp (p, "CHAR", 4) != 0) && (strncmp (p, "VARCHAR", 7) != 0) && (strncmp (p, "NCHAR", 5) != 0)
      && (strncmp (p, "VARNCHAR", 8) != 0) && (strncmp (p, "BIT", 3) != 0) && (strncmp (p, "VARBIT", 6) != 0))
    {
      return false;
    }

  q = strchr (p, ' ');
  if (q == NULL)
    {
      /* log error case or NULL bind type */
      return false;
    }

  *q = '\0';
  value_p = q + 1;

  size_begin = strstr (value_p, "(");
  if (size_begin == NULL)
    {
      goto error_on_val_size;
    }
  size_begin += 1;
  size_end = strstr (value_p, ")");
  if (size_end == NULL)
    {
      goto error_on_val_size;
    }

  info_end = size_end + 1;

  if (info_size)
    {
      *info_size = (char *) info_end - (char *) buf;
    }

  if ((strncmp (p, "CHAR", 4) != 0) || (strncmp (p, "VARCHAR", 7) != 0) || (strncmp (p, "NCHAR", 5) != 0)
      || (strncmp (p, "VARNCHAR", 8) != 0))
    {
      *tot_val_size = strlen (info_end);
    }
  else if (tot_val_size)
    {
      len = size_end - size_begin;
      if (len > (int) sizeof (size))
	{
	  goto error_on_val_size;
	}
      if (len > 0)
	{
	  memcpy (size, size_begin, len);
	  size[len] = '\0';
	}
      *tot_val_size = atoi (size);
      if (*tot_val_size < 0)
	{
	  goto error_on_val_size;
	}
    }

  return true;

error_on_val_size:
  if (info_size)
    {
      *info_size = -1;
    }
  if (tot_val_size)
    {
      *tot_val_size = -1;
    }
  return false;
}
#endif /* BROKER_LOG_RUNNER */

int
ut_get_line (FILE * fp, T_STRING * t_str, char **out_str, int *lineno)
{
  char buf[1024];
  int out_str_len;
  bool is_first, bind_with_size = false;
  int tot_val_size = 0, info_size = 0;
  long position;

  t_string_clear (t_str);

  is_first = true;
  while (1)
    {
      memset (buf, 0, sizeof (buf));
      position = ftell (fp);
      if (fgets (buf, sizeof (buf), fp) == NULL)
	break;
      /* if it is (debug) line, skip it */
      if (strncmp (buf + 19, "(debug)", 7) == 0)
	{
	  continue;
	}
      if (is_first)
	{
	  bind_with_size = is_bind_with_size (buf, &tot_val_size, &info_size);
	  if (tot_val_size < 0 || info_size < 0 || (tot_val_size + info_size + 1) < 0)
	    {
	      fprintf (stderr, "log error\n");
	      return -1;
	    }
	  is_first = false;
	}

      if (bind_with_size)
	{
	  size_t rlen;
	  char *value = NULL;

	  value = (char *) MALLOC (info_size + tot_val_size + 1);
	  if (value == NULL)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      return -1;
	    }
	  fseek (fp, position, SEEK_SET);
	  rlen = fread ((void *) value, sizeof (char), info_size + tot_val_size, fp);
	  if (t_bind_string_add (t_str, value, info_size + tot_val_size, tot_val_size) < 0)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      FREE_MEM (value);
	      return -1;
	    }
	  FREE_MEM (value);
	  break;
	}
      else
	{
	  if (t_string_add (t_str, buf, (int) strlen (buf)) < 0)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      return -1;
	    }
	  if (buf[sizeof (buf) - 2] == '\0' || buf[sizeof (buf) - 2] == '\n')
	    break;
	}
    }

  out_str_len = t_string_len (t_str);
  if (out_str)
    *out_str = t_string_str (t_str);
  if (lineno)
    *lineno = *lineno + 1;
  return out_str_len;
}

int
is_cas_log (char *str)
{
  if (strlen (str) < CAS_LOG_MSG_INDEX)
    {
      return 0;
    }

  if (str[2] == '-' && str[5] == '-' && str[8] == ' ' && str[11] == ':' && str[14] == ':' && str[21] == ' ')
    {
      return CAS_LOG_BEGIN_WITH_YEAR;
    }
  else if (str[2] == '/' && str[5] == ' ' && str[8] == ':' && str[11] == ':' && str[18] == ' ')
    {
      return CAS_LOG_BEGIN_WITH_MONTH;
    }

  return 0;
}

char *
get_msg_start_ptr (char *linebuf)
{
  char *tmp_ptr;

  if (is_cas_log (linebuf) == CAS_LOG_BEGIN_WITH_YEAR)
    {
      tmp_ptr = linebuf + CAS_LOG_MSG_INDEX;
    }
  else
    {
      tmp_ptr = linebuf + CAS_LOG_MSG_INDEX - 3;
    }

  tmp_ptr = strchr (tmp_ptr, ' ');
  if (tmp_ptr == NULL)
    {
      return (char *) "";
    }
  else
    {
      return tmp_ptr + 1;
    }
}

#define  DATE_VALUE_COUNT 7
int
str_to_log_date_format (char *str, char *date_format_str)
{
  char *startp;
  char *endp;
  int i;
  int result = 0;
  int val;
  int date_val[DATE_VALUE_COUNT];

  for (i = 0; i < DATE_VALUE_COUNT; i++)
    date_val[i] = 0;

  for (i = 0, startp = str; i < DATE_VALUE_COUNT; i++)
    {
      result = str_to_int32 (&val, &endp, startp, 10);
      if (result != 0)
	{
	  goto error;
	}
      if (val < 0)
	{
	  val = 0;
	}
      else if (val > 999)
	{
	  val = 999;
	}
      date_val[i] = val;
      if (*endp == '\0')
	{
	  break;
	}
      startp = endp + 1;
      if (*startp == '\0')
	{
	  break;
	}
    }

  sprintf (date_format_str, "%02d-%02d-%02d %02d:%02d:%02d.%03d", date_val[0], date_val[1], date_val[2], date_val[3],
	   date_val[4], date_val[5], date_val[6]);
  return 0;

error:
  return -1;
}

char *
ut_get_execute_type (char *msg_p, int *prepare_flag, int *execute_flag)
{
  if (strncmp (msg_p, "execute ", 8) == 0)
    {
      *prepare_flag = 0;
      *execute_flag = 0;
      return (msg_p += 8);
    }
  else if (strncmp (msg_p, "execute_call ", 13) == 0)
    {
      *prepare_flag = 0x40;	/* CCI_PREPARE_CALL */
      *execute_flag = 0;
      return (msg_p += 13);
    }
  else if (strncmp (msg_p, "execute_all ", 12) == 0)
    {
      *prepare_flag = 0;
      *execute_flag = 2;	/* CCI_EXEC_QUERY_ALL */
      return (msg_p += 12);
    }
  else
    {
      return NULL;
    }
}

int
ut_check_log_valid_time (const char *log_date, const char *from_date, const char *to_date)
{
  if (from_date[0])
    {
      if (strncmp (log_date, from_date, DATE_STR_LEN) < 0)
	return -1;
    }
  if (to_date[0])
    {
      if (strncmp (to_date, log_date, DATE_STR_LEN) < 0)
	return -1;
    }

  return 0;
}

double
ut_diff_time (struct timeval *begin, struct timeval *end)
{
  double sec, usec;

  sec = (end->tv_sec - begin->tv_sec);
  usec = (double) (end->tv_usec - begin->tv_usec) / 1000000;
  return (sec + usec);
}
