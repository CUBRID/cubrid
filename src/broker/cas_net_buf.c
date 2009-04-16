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
 * cas_net_buf.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

#include "cas.h"
#include "cas_common.h"
#include "cas_net_buf.h"
#include "cas_util.h"

static int net_buf_realloc (T_NET_BUF * net_buf, int size);

void
net_buf_init (T_NET_BUF * net_buf)
{
  memset (net_buf, 0, sizeof (T_NET_BUF));
}

void
net_buf_clear (T_NET_BUF * net_buf)
{
  int alloc_size;
  char *data;

  /* save alloc info */
  alloc_size = net_buf->alloc_size;
  data = net_buf->data;

  FREE_MEM (net_buf->post_send_file);
  net_buf_init (net_buf);

  /* restore alloc info */
  net_buf->alloc_size = alloc_size;
  net_buf->data = data;
}

void
net_buf_destroy (T_NET_BUF * net_buf)
{
  FREE_MEM (net_buf->data);
  net_buf_clear (net_buf);
}

int
net_buf_cp_post_send_file (T_NET_BUF * net_buf, int size, char *filename)
{
  FREE_MEM (net_buf->post_send_file);
  ALLOC_COPY (net_buf->post_send_file, filename);
  if (net_buf->post_send_file == NULL)
    {
      net_buf->err_code = CAS_ER_NO_MORE_MEMORY;
      return CAS_ER_NO_MORE_MEMORY;
    }
  net_buf->post_file_size = size;
  return 0;
}

int
net_buf_cp_byte (T_NET_BUF * net_buf, char ch)
{
  if (NET_BUF_FREE_SIZE (net_buf) < 1 && net_buf_realloc (net_buf, 1) < 0)
    return CAS_ER_NO_MORE_MEMORY;

  *(NET_BUF_CURR_PTR (net_buf)) = ch;	/* do not call memcpy(); simply assign */
  net_buf->data_size += 1;
  return 0;
}

int
net_buf_cp_str (T_NET_BUF * net_buf, const char *buf, int size)
{
  if (size <= 0)
    return 0;

  if (NET_BUF_FREE_SIZE (net_buf) < size
      && net_buf_realloc (net_buf, size) < 0)
    return CAS_ER_NO_MORE_MEMORY;

  memcpy (NET_BUF_CURR_PTR (net_buf), buf, size);
  net_buf->data_size += size;
  return 0;
}

int
net_buf_cp_int (T_NET_BUF * net_buf, int value, int *begin_offset)
{
  if (NET_BUF_FREE_SIZE (net_buf) < 4 && net_buf_realloc (net_buf, 4) < 0)
    {
      if (begin_offset)
	*begin_offset = -1;
      return CAS_ER_NO_MORE_MEMORY;
    }

  value = htonl (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, 4);

  if (begin_offset)
    *begin_offset = net_buf->data_size;

  net_buf->data_size += 4;
  return 0;
}

void
net_buf_overwrite_int (T_NET_BUF * net_buf, int offset, int value)
{
  if (net_buf->data == NULL || offset < 0)
    return;
  value = htonl (value);
  memcpy (net_buf->data + NET_BUF_HEADER_SIZE + offset, &value, 4);
}

int
net_buf_cp_float (T_NET_BUF * net_buf, float value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < 4 && net_buf_realloc (net_buf, 4) < 0)
    return CAS_ER_NO_MORE_MEMORY;
  value = net_htonf (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, 4);
  net_buf->data_size += 4;

  return 0;
}

int
net_buf_cp_double (T_NET_BUF * net_buf, double value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < 8 && net_buf_realloc (net_buf, 8) < 0)
    return CAS_ER_NO_MORE_MEMORY;
  value = net_htond (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, 8);
  net_buf->data_size += 8;

  return 0;
}

int
net_buf_cp_short (T_NET_BUF * net_buf, short value)
{
  if (NET_BUF_FREE_SIZE (net_buf) < 2 && net_buf_realloc (net_buf, 2) < 0)
    return CAS_ER_NO_MORE_MEMORY;
  value = htons (value);
  memcpy (NET_BUF_CURR_PTR (net_buf), &value, 2);
  net_buf->data_size += 2;

  return 0;
}

void
net_buf_error_msg_set (T_NET_BUF * net_buf, int err_code, char *err_str,
		       char *file, int line)
{
#ifdef CAS_DEBUG
  char msg_buf[1024];
#endif

  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);

#ifdef CAS_DEBUG
  sprintf (msg_buf, "%s:%d ", file, line);
  net_buf_cp_str (net_buf, msg_buf, strlen (msg_buf));
#endif

  if (err_str == NULL)
    net_buf_cp_byte (net_buf, '\0');
  else
    net_buf_cp_str (net_buf, err_str, strlen (err_str) + 1);
}

#ifndef BYTE_ORDER_BIG_ENDIAN
float
net_htonf (float from)
{
  float to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
}

double
net_htond (double from)
{
  double to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}
#endif

void
net_buf_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec,
			 char *name)
{
  net_buf_cp_byte (net_buf, ut);
  net_buf_cp_short (net_buf, scale);
  net_buf_cp_int (net_buf, prec, NULL);
  if (name == NULL)
    {
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      char *tmp_str;

      ALLOC_COPY (tmp_str, name);
      if (tmp_str == NULL)
	{
	  net_buf_cp_int (net_buf, 1, NULL);
	  net_buf_cp_byte (net_buf, '\0');
	}
      else
	{
	  ut_trim (tmp_str);
	  net_buf_cp_int (net_buf, strlen (tmp_str) + 1, NULL);
	  net_buf_cp_str (net_buf, tmp_str, strlen (tmp_str) + 1);
	  FREE_MEM (tmp_str);
	}
    }
}

static int
net_buf_realloc (T_NET_BUF * net_buf, int size)
{
  if (NET_BUF_FREE_SIZE (net_buf) < size)
    {
      int extra, new_alloc_size;

      /* realloc unit is 64 Kbyte */
      extra = (size + NET_BUF_EXTRA_SIZE - 1) / NET_BUF_EXTRA_SIZE;
      new_alloc_size = net_buf->alloc_size + extra * NET_BUF_EXTRA_SIZE;
      net_buf->data = (char *) REALLOC (net_buf->data, new_alloc_size);
      if (net_buf->data == NULL)
	{
	  net_buf->alloc_size = 0;
	  net_buf->err_code = CAS_ER_NO_MORE_MEMORY;
	  return -1;
	}

      net_buf->alloc_size = new_alloc_size;
    }

  return 0;
}
