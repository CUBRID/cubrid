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


/*
 * cci_util.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef WINDOWS
#include <winsock2.h>
#include <windows.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cas_cci.h"
#include "cci_util.h"
#include "cci_handle_mng.h"
#include "cci_t_lob.h"

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/
#if defined(WINDOWS)
#define strtoll	_strtoi64
#endif

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

#ifdef UNICODE_DATA
static char *wstr2str (WCHAR * wstr, UINT CodePage);
static WCHAR *str2wstr (char *str, UINT CodePage);
#endif
static char is_float_str (char *str);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

int
ut_str_to_bigint (char *str, INT64 * value)
{
  char *end_p;
  INT64 bi_val;

  bi_val = strtoll (str, &end_p, 10);
  if (*end_p == 0 || *end_p == '.' || isspace ((int) *end_p))
    {
      *value = bi_val;
      return 0;
    }

  return (CCI_ER_TYPE_CONVERSION);
}

int
ut_str_to_int (char *str, int *value)
{
  char *end_p;
  int i_val;

  i_val = strtol (str, &end_p, 10);
  if (*end_p == 0 || *end_p == '.' || isspace ((int) *end_p))
    {
      *value = i_val;
      return 0;
    }

  return (CCI_ER_TYPE_CONVERSION);
}

int
ut_str_to_float (char *str, float *value)
{
  if (is_float_str (str))
    {
      sscanf (str, "%f", value);
      return 0;
    }

  return (CCI_ER_TYPE_CONVERSION);
}

int
ut_str_to_double (char *str, double *value)
{
  if (is_float_str (str))
    {
      sscanf (str, "%lf", value);
      return 0;
    }

  return (CCI_ER_TYPE_CONVERSION);
}

int
ut_str_to_date (char *str, T_CCI_DATE * value)
{
  char *p, *q;
  int yr, mon, day;

  p = str;
  q = strchr (p, '/');
  if (q == NULL)
    return CCI_ER_TYPE_CONVERSION;
  yr = atoi (p);
  p = q + 1;

  q = strchr (p, '/');
  if (q == NULL)
    return CCI_ER_TYPE_CONVERSION;
  mon = atoi (p);

  day = atoi (q + 1);

  memset (value, 0, sizeof (T_CCI_DATE));
  value->yr = yr;
  value->mon = mon;
  value->day = day;
  return 0;
}

int
ut_str_to_time (char *str, T_CCI_DATE * value)
{
  char *p, *q;
  int hh, mm, ss;

  if (str == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  p = str;
  q = strchr (p, ':');
  if (q == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  hh = atoi (p);
  p = q + 1;

  q = strchr (p, ':');
  if (q == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  mm = atoi (p);
  ss = atoi (q + 1);

  memset (value, 0, sizeof (T_CCI_DATE));
  value->hh = hh;
  value->mm = mm;
  value->ss = ss;
  return 0;
}

int
ut_str_to_mtime (char *str, T_CCI_DATE * value)
{
  char *p, *q;
  int hh, mm, ss, ms;

  if (str == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  p = str;
  q = strchr (p, ':');
  if (q == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  hh = atoi (p);
  p = q + 1;

  q = strchr (p, ':');
  if (q == NULL)
    {
      return CCI_ER_TYPE_CONVERSION;
    }
  mm = atoi (p);

  q = strchr (p, '.');
  if (q == NULL)
    {
      ss = atoi (p);
      ms = 0;
    }
  else
    {
      ss = atoi (p);
      ms = (int) (strtod (q, &p) * 1000 + 0.5);
      if (q == p)
	{
	  return CCI_ER_TYPE_CONVERSION;
	}
    }

  memset (value, 0, sizeof (T_CCI_DATE));
  value->hh = hh;
  value->mm = mm;
  value->ss = ss;
  value->ms = ms;

  return 0;
}

int
ut_str_to_timestamp (char *str, T_CCI_DATE * value)
{
  T_CCI_DATE date;
  T_CCI_DATE time;
  char *p;
  int err_code;

  p = strchr (str, ' ');

  if ((err_code = ut_str_to_date (str, &date)) < 0)
    {
      return err_code;
    }
  if ((err_code = ut_str_to_time (p, &time)) < 0)
    {
      return err_code;
    }

  value->yr = date.yr;
  value->mon = date.mon;
  value->day = date.day;
  value->hh = time.hh;
  value->mm = time.mm;
  value->ss = time.ss;

  return 0;
}

int
ut_str_to_datetime (char *str, T_CCI_DATE * value)
{
  T_CCI_DATE date;
  T_CCI_DATE mtime;
  char *p;
  int err_code;

  p = strchr (str, ' ');

  if ((err_code = ut_str_to_date (str, &date)) < 0)
    {
      return err_code;
    }
  if ((err_code = ut_str_to_mtime (p, &mtime)) < 0)
    {
      return err_code;
    }

  value->yr = date.yr;
  value->mon = date.mon;
  value->day = date.day;
  value->hh = mtime.hh;
  value->mm = mtime.mm;
  value->ss = mtime.ss;
  value->ms = mtime.ms;

  return 0;
}

int
ut_str_to_oid (char *str, T_OBJECT * value)
{
  char *p = str;
  char *end_p;
  int id;

  if (p == NULL)
    return CCI_ER_TYPE_CONVERSION;

  if (*p != '@')
    return CCI_ER_TYPE_CONVERSION;

  p++;
  id = strtol (p, &end_p, 10);	/* page id */
  if (*end_p != '|')
    return CCI_ER_TYPE_CONVERSION;
  value->pageid = id;

  p = end_p + 1;
  id = strtol (p, &end_p, 10);	/* slot id */
  if (*end_p != '|')
    return CCI_ER_TYPE_CONVERSION;
  value->slotid = id;

  p = end_p + 1;
  id = strtol (p, &end_p, 10);	/* vol id */
  if (*end_p != '\0')
    return CCI_ER_TYPE_CONVERSION;
  value->volid = id;

  return 0;
}

void
ut_int_to_str (INT64 value, char *str)
{
  sprintf (str, "%lld", (long long) value);
}

void
ut_float_to_str (float value, char *str)
{
  sprintf (str, "%f", value);
}

void
ut_double_to_str (double value, char *str)
{
  sprintf (str, "%.16f", value);
}

void
ut_date_to_str (T_CCI_DATE * value, T_CCI_U_TYPE u_type, char *str)
{
  if (u_type == CCI_U_TYPE_DATE)
    {
      sprintf (str, "%04d-%02d-%02d", value->yr, value->mon, value->day);
    }
  else if (u_type == CCI_U_TYPE_TIME)
    {
      sprintf (str, "%02d:%02d:%02d", value->hh, value->mm, value->ss);
    }
  else if (u_type == CCI_U_TYPE_TIMESTAMP)
    {
      sprintf (str, "%04d-%02d-%02d %02d:%02d:%02d",
	       value->yr, value->mon, value->day,
	       value->hh, value->mm, value->ss);
    }
  else
    {				/* u_type == CCI_U_TYPE_DATETIME */
      sprintf (str, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
	       value->yr, value->mon, value->day,
	       value->hh, value->mm, value->ss, value->ms);
    }
}

void
ut_oid_to_str (T_OBJECT * oid, char *str)
{
  sprintf (str, "@%d|%d|%d", oid->pageid, oid->slotid, oid->volid);
}

void
ut_lob_to_str (T_LOB * lob, char *str)
{
#if 0
  sprintf (str, "%s:%s",
	   (lob->type == CCI_U_TYPE_BLOB
	    ? "BLOB" : (lob->type == CCI_U_TYPE_CLOB
			? "CLOB" : "????")), lob->handle + 16);
#else
  strcpy (str, lob->handle + 16);
#endif
}

void
ut_bit_to_str (char *bit_str, int size, char *str)
{
  char ch, c;
  int i;

  for (i = 0; i < size; i++)
    {
      ch = bit_str[i];

      c = (ch >> 4) & 0x0f;
      if (c <= 9)
	str[2 * i] = c + '0';
      else
	str[2 * i] = c - 10 + 'A';

      c = ch & 0x0f;
      if (c <= 9)
	str[2 * i + 1] = c + '0';
      else
	str[2 * i + 1] = c - 10 + 'A';
    }
  str[2 * i] = 0;
}

int
ut_is_deleted_oid (T_OBJECT * oid)
{
  T_OBJECT del_oid;

  memset (&del_oid, 0xff, sizeof (del_oid));

  if (oid->pageid == del_oid.pageid &&
      oid->slotid == del_oid.slotid && oid->volid == del_oid.volid)
    {
      return CCI_ER_DELETED_TUPLE;
    }

  return 0;
}


#ifdef UNICODE_DATA
char *
ut_ansi_to_unicode (char *str)
{
  WCHAR *wstr;

  wstr = str2wstr (str, CP_ACP);
  str = wstr2str (wstr, CP_UTF8);
  FREE_MEM (wstr);
  return str;
}

char *
ut_unicode_to_ansi (char *str)
{
  WCHAR *wstr;

  wstr = str2wstr (str, CP_UTF8);
  str = wstr2str (wstr, CP_ACP);
  FREE_MEM (wstr);
  return str;
}
#endif

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

#ifdef UNICODE_DATA
static WCHAR *
str2wstr (char *str, UINT CodePage)
{
  int len;
  WCHAR *wstr;

  if (str == NULL)
    return NULL;

  len = (int) strlen (str) + 1;
  wstr = (WCHAR *) malloc (sizeof (WCHAR) * len);
  if (wstr == NULL)
    return NULL;
  memset (wstr, 0, sizeof (WCHAR) * len);

  MultiByteToWideChar (CodePage, 0, str, len, wstr, len);
  return wstr;
}

static char *
wstr2str (WCHAR * wstr, UINT CodePage)
{
  int len, buf_len;
  char *str;

  if (wstr == NULL)
    return NULL;

  len = wcslen (wstr) + 1;
  buf_len = len * 2 + 10;
  str = (char *) malloc (buf_len);
  if (str == NULL)
    return NULL;
  memset (str, 0, buf_len);

  WideCharToMultiByte (CodePage, 0, wstr, len, str, buf_len, NULL, NULL);
  return str;
}
#endif

static char
is_float_str (char *str)
{
  char *p;
  char ch;
  int state = 0;

  for (p = str; *p && state >= 0; p++)
    {
      ch = *p;
      switch (state)
	{
	case 0:
	  if (ch == '+' || ch == '-')
	    state = 1;
	  else if (ch == '.')
	    state = 3;
	  else if (ch >= '0' && ch <= '9')
	    state = 2;
	  else
	    state = -1;
	  break;
	case 1:
	  if (ch >= '0' && ch <= '9')
	    state = 2;
	  else
	    state = -1;
	  break;
	case 2:
	  if (ch == '.')
	    state = 3;
	  else if (ch == 'e' || ch == 'E')
	    state = 4;
	  else if (ch >= '0' && ch <= '9')
	    state = 2;
	  else
	    state = -1;
	  break;
	case 3:
	  if (ch >= '0' && ch <= '9')
	    state = 5;
	  else
	    state = -1;
	  break;
	case 4:
	  if (ch == '+' || ch == '-' || (ch >= '0' && ch <= '9'))
	    state = 6;
	  else
	    state = -1;
	  break;
	case 5:
	  if (ch == 'e' || ch == 'E')
	    state = 4;
	  else if (ch >= '0' && ch <= '9')
	    state = 5;
	  else
	    state = -1;
	  break;
	case 6:
	  if (ch >= '0' && ch <= '9')
	    state = 6;
	  else
	    state = -1;
	default:
	  break;
	}
    }

  if (state == 2 || state == 5 || state == 6)
    return 1;

  return 0;
}
